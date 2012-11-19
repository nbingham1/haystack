/*
 * block.cpp
 *
 *  Created on: Oct 29, 2012
 *      Author: Ned Bingham
 */

#include "block.h"
#include "common.h"
#include "conditional.h"
#include "loop.h"
#include "parallel.h"
#include "rule.h"
#include "channel.h"

block::block()
{
	chp = "";
	_kind = "block";
}
block::block(string raw, map<string, keyword*> types, map<string, variable*> vars, map<string, state> init, string tab)
{
	_kind = "block";
	parse(raw, types, vars, init, tab);
}
block::~block()
{
	chp = "";
	_kind = "block";

	map<string, variable*>::iterator i;
	for (i = local.begin(); i != local.end(); i++)
	{
		if (i->second != NULL)
			delete i->second;
		i->second = NULL;
	}

	local.clear();

	list<instruction*>::iterator j;
	for (j = instrs.begin(); j != instrs.end(); j++)
	{
		if (*j != NULL)
			delete *j;
		*j = NULL;
	}

	instrs.clear();
}

block &block::operator=(block b)
{
	chp = b.chp;
	instrs = b.instrs;
	states = b.states;
	return *this;
}

void block::parse(string raw, map<string, keyword*> types, map<string, variable*> vars, map<string, state> init, string tab)
{
	result.clear();
	local.clear();
	global.clear();
	instrs.clear();
	states.clear();

	cout << tab << "Block: " << raw << endl;

	global = vars;
	chp = raw;

	string		raw_instr;	// chp of a sub block

	instruction *instr; 	// instruction parser
	variable	*v;			// variable instantiation parser

	map<string, state> current_state, change_state;

	list<instruction*>		::iterator	ii, ix;
	map<string, variable*>	::iterator	vi, vj;
	map<string, space>		::iterator	si, sj, sk;
	map<string, state>		::iterator	l, m;
	list<state>				::iterator	a, b;
	map<string, keyword*>	::iterator	t;
	list<list<variable*> >	::iterator	di;
	list<variable*>			::iterator	dvi;
	string					::iterator	i, j;
	size_t								ij, ik;

	list<map<string, state> >			tracer_changes;
	list<map<string, state> > :: iterator xi;
	map<string, variable*>				affected;
	list<list<variable*> >				delta_out;
	list<variable*> delta;

	map<string, size_t>					prgm_start;
	map<string, size_t>					prgm_ctr;
	map<string, string>					prgm_protocol;
	map<string, string>		::iterator	proti;
	map<string, size_t>		::iterator	pi;
	list<size_t>			::iterator  pj;

	size_t								k, p;
	state								tstate;

	string search0, search1, search2;

	bool para		= false;
	bool vdef		= false;
	bool first		= false;

	// Add the initial states into the affected variable list
	// This makes sure that evaluated guards pass through skip blocks
	for (l = init.begin(); l != init.end(); l++)
		if ((vi = vars.find(l->first)) != vars.end())
		{
			affected.insert(pair<string, variable*>(vi->first, vi->second));
			current_state.insert(pair<string, state>(l->first, l->second));
		}

	// Parse the instructions, making sure to stay in the current scope (outside of any bracket/parenthesis)
	int depth[3] = {0};
	ij = 0;
	for (i = chp.begin(), j = chp.begin(); i != chp.end()+1; i++)
	{
		if (*i == '(')
			depth[0]++;
		else if (*i == '[')
			depth[1]++;
		else if (*i == '{')
			depth[2]++;
		else if (*i == ')')
			depth[0]--;
		else if (*i == ']')
			depth[1]--;
		else if (*i == '}')
			depth[2]--;

		// We are in the current scope, and the current character
		// is a semicolon or the end of the chp string. This is
		// the end of a block.
		if (depth[0] == 0 && depth[1] == 0 && depth[2] == 0 && (*i == ';' || i == chp.end()))
		{
			// Get the block string.
			raw_instr = chp.substr(j-chp.begin(), i-j);

			instr = NULL;
			// This sub block is a set of parallel sub sub blocks. s0 || s1 || ... || sn
			if (para)
				instr = new parallel(raw_instr, types, global, current_state, tab+"\t");
			// This sub block has a specific order of operations. (s)
			else if (raw_instr[0] == '(' && raw_instr[raw_instr.length()-1] == ')')
				instr = new block(raw_instr.substr(1, raw_instr.length()-2), types, global, current_state, tab+"\t");
			// This sub block is a loop. *[g0->s0[]g1->s1[]...[]gn->sn] or *[g0->s0|g1->s1|...|gn->sn]
			else if (raw_instr[0] == '*' && raw_instr[1] == '[' && raw_instr[raw_instr.length()-1] == ']')
				instr = new loop(raw_instr, types, global, current_state, tab+"\t");
			// This sub block is a conditional. [g0->s0[]g1->s1[]...[]gn->sn] or [g0->s0|g1->s1|...|gn->sn]
			else if (raw_instr[0] == '[' && raw_instr[raw_instr.length()-1] == ']')
				instr = new conditional(raw_instr, types, global, current_state, tab+"\t");
			// This sub block is either a variable definition or an assignment instruction.
			else
			{
				vdef = false;
				for (t = types.begin(); t != types.end(); t++)
					if (raw_instr.find(t->first) != raw_instr.npos)
					{
						vdef = true;
						break;
					}

				// This sub block is a variable definition. keyword<bitwidth> name
				if (vdef)
				{
					v = new variable(raw_instr, tab);
					local.insert(pair<string, variable*>(v->name, v));
					global.insert(pair<string, variable*>(v->name, v));
				}
				// This sub block is an assignment instruction.
				else if (raw_instr.length() != 0)
					instr = new instruction(raw_instr, types, global, current_state, tab+"\t");
			}

			if (instr != NULL)
			{
				instrs.push_back(instr);


				// Now that we have parsed the sub block, we need to
				// check the resulting state space deltas of that sub block.
				// Loop through all of the affected variables.
				delta.clear();
				for (l = instr->result.begin(); l != instr->result.end(); l++)
				{
					// If this variable exists, then we check the resultant value against
					// its current bitwidth and adjust the bitwidth to fit the resultant value.
					// We also need to mark whether or not we need to generate a production rule
					// for this instruction.
					vi = global.find(l->first);
					if (vi == global.end() && l->first != "Unhandled")
						cout<< "Error: you are trying to call an instruction that operates on a variable not in this block's scope: " + l->first << endl;
					else if (vi != global.end())
					{
						if ((l->second.prs) && (l->second.data != vi->second->last.data))
						{
							vj = global.find(l->first.substr(0, l->first.find_first_of(".")));
							if (vj != global.end())
							{
								cout << "DELTA " << vj->first << endl;
								delta.push_back(vj->second);
							}
							else
								cout << "NO DELTA" << endl;
						}

						vi->second->last = l->second;
						if (affected.find(vi->first) == affected.end())
							affected.insert(pair<string, variable*>(vi->first, vi->second));
					}
				}
				delta_out.push_back(delta);

				// Fill in the state space based upon the recorded delta values from instruction parsing above.
				// Right now, we X out the input variables when an instruction changes an output value. This will
				// have to be modified in the future so that we only X out the input variables depending upon the
				// communication protocol.
				for(vi = affected.begin(); vi != affected.end(); vi++)
				{
					if (states.find(vi->first) == states.end())
					{
						states.insert(pair<string, space>(vi->first, space(vi->first, list<state>())));
						// The first state for every variable is always all X
						if ((l = init.find(vi->first)) != init.end())
						{
							states[vi->first].states.push_back(l->second);
							states[vi->first].var = vi->first;
						}
						else
						{
							states[vi->first].states.push_back(vi->second->reset);
							states[vi->first].var = vi->first;
						}
					}


					for (ii = instrs.begin(), di = delta_out.begin(), k = 0; ii != instrs.end() && di != delta_out.end(); ii++, di++, k++)
					{
						if (k >= states[vi->first].states.size()-1)
						{
							l = (*ii)->result.find(vi->first);
							vj = global.find(vi->first.substr(0, vi->first.find_first_of(".")));
							if (vj != global.end())
							{
								t = types.find(vj->second->type);
								pi = prgm_ctr.find(vj->first);
								proti = prgm_protocol.find(vj->second->name);

								if (l != (*ii)->result.end())
								{
									search0 = (*ii)->chp;
									while ((p = search0.find(vj->first + ".")) != search0.npos)
										search0 = search0.substr(0, p) + search0.substr(p + vj->first.length() + 1);

									if (pi == prgm_ctr.end())
									{
										if (t != types.end() && t->second->kind() == "channel")
										{
											prgm_ctr.insert(pair<string, size_t>(vj->first, 0));
											prgm_start.insert(pair<string, size_t>(vj->first, k));
											pi = prgm_ctr.find(vj->first);
											cout << tab << "New Program Counter: " << vj->second->name << " 0" << endl;
										}
									}
									else
									{

										pi->second = k - prgm_start.find(vj->first)->second;
										cout << tab << "Increment Program Counter: " << vj->second->name << " " << pi->second << endl;
									}

									if (pi != prgm_ctr.end() && t != types.end() && t->second->kind() == "channel" && (proti == prgm_protocol.end() || proti->second == "?"))
									{
										// TODO Set to instruction at counter index
										search1 = (*((channel*)t->second)->send.def.instrs.begin())->chp;
										search2 = (*((channel*)t->second)->recv.def.instrs.begin())->chp;
										if (search1.find(search0) != search1.npos && search2.find(search0) != search2.npos)
										{
											cout << tab << "Send: " << search0 << " in " << search1 << " " << search1.find(search0) << endl;
											cout << tab << "Recv: " << search0 << " in " << search2 << " " << search2.find(search0) << endl;
											prgm_protocol.insert(pair<string, string>(vj->first, "?"));
										}
										else if (search1.find(search0) != search1.npos)
										{
											cout << tab << "Send: " << search0 << " in " << search1 << " " << search1.find(search0) << endl;
											prgm_protocol.insert(pair<string, string>(vj->first, "send"));
										}
										else if (search2.find(search0) != search2.npos)
										{
											cout << tab << "Recv: " << search0 << " in " << search2 << " " << search2.find(search0) << endl;
											prgm_protocol.insert(pair<string, string>(vj->first, "recv"));
										}
									}
								}

								proti = prgm_protocol.find(vj->second->name);
								cout << "Examining variable: " << vi->first << endl;

								if (l != (*ii)->result.end() && l->second.data != "NA")
								{
									cout << "TAKING PATH A" << endl;
									states[vi->first].states.push_back(l->second);
								}
								else if (di->size() > 0 && !states[vi->first].states.rbegin()->prs)
								{
									cout << "TAKING PATH B" << endl;
									// Use channel send and recv functions to determine whether or not we need to X out the state
									cout << tab << "Maybe X Out: " << vi->first << " " << pi->second << endl;
									if (t != types.end() && t->second->kind() == "channel" && pi != prgm_ctr.end())
									{
										p = pi->second;

										if (proti == prgm_protocol.end())
											cout << "Ok... Another place that has issues" << endl;
										else
										{
											cout << "Finding Change Set: " << prgm_start.find(vj->first)->second << endl;
											if (proti->second == "send")
											{
												tracer_changes = ((channel*)t->second)->recv.def.changes;
												for (pj = waits.begin(); pj != waits.end() && prgm_start.find(vj->first)->second > *pj; pj++);
												for (xi = tracer_changes.begin(); pj != waits.end() && xi != tracer_changes.end() && p > *pj; pj++, xi++)
												{
													cout << "Finding Change Set: " << p << " " << *pj << endl;
													for (m = xi->begin(); m != xi->end(); m++)
														cout << m->first << " " << m->second << endl;
												}
											}
											else if (proti->second == "recv")
											{
												tracer_changes = ((channel*)t->second)->send.def.changes;
												for (pj = waits.begin(); pj != waits.end() && prgm_start.find(vj->first)->second > *pj; pj++);
												for (xi = tracer_changes.begin(); pj != waits.end() && xi != tracer_changes.end() && p > *pj; pj++, xi++)
												{
													cout << "Finding Change Set: " << p << " " << *pj << endl;
													for (m = xi->begin(); m != xi->end(); m++)
														cout << m->first << " " << m->second << endl;
												}
											}

											cout << "Finding Change Set: " << p << " " << *pj << endl;

											cout << "BLARG! " << proti->second << " " << vi->first << endl;
											if (xi != tracer_changes.end())
											{
												for (m = xi->begin(); m != xi->end(); m++)
													cout << m->first << " " << m->second << endl;

												m = xi->find(vi->first.substr(vi->first.find_first_of(".")+1));
												if (m != xi->end())
												{
													states[vi->first].states.push_back(*(states[vi->first].states.rbegin()) || m->second);
													cout << "Setting: " << m->first << " " << m->second << " " << endl;
												}
												else
												{
													states[vi->first].states.push_back(*(states[vi->first].states.rbegin()));
													cout << "Fail 1: " << vi->first.substr(vi->first.find_first_of(".")+1) << " " << ((channel*)t->second)->send.def.chp << endl;
												}
											}
											else
											{
												cout << "FUCK!" << endl;
												states[vi->first].states.push_back(*(states[vi->first].states.rbegin()));
											}
										}
									}
									else
									{
										states[vi->first].states.push_back(*(states[vi->first].states.rbegin()));
										cout << "Fail 2 " << (t != types.end()) << " " << (t->second->kind() == "channel") << " " << (pi != prgm_ctr.end()) << endl;
									}
								}
								// there is no delta in the output variables or this is an output variable
								else
								{
									cout << "TAKING PATH C " << di->size() << " " << states[vi->first].states.rbegin()->prs << endl;
									states[vi->first].states.push_back(*(states[vi->first].states.rbegin()));
								}

								current_state[vi->first] = *states[vi->first].states.rbegin();
							}
							else
								cout << "Something is royally fucked up." << endl;
						}
					}
				}

				// This counts wait instructions and figures out
				// what states changes in between wait instructions
				if (instr->kind() == "conditional" || i == chp.end())
				{
					change_state.clear();
					if (instr->kind() != "conditional" && i == chp.end())
						ij++;

					for (vi = affected.begin(); vi != affected.end(); vi++)
					{
						if ((si = states.find(vi->first)) != states.end())
						{
							first = true;
							for (ik = 0, a = si->second.states.begin(); waits.size() > 0 && ik <= (*waits.rbegin()) && a != si->second.states.end(); ik++, a++);
							for (; ((instr->kind() != "conditional" && i == chp.end()) || ik <= ij) && a != si->second.states.end(); ik++, a++)
							{
								tstate = first ? *a : tstate || *a;

								first = false;
							}

							change_state.insert(pair<string, state>(vi->first, tstate));
						}
					}

					waits.push_back(ij);
					changes.push_back(change_state);
				}

				ij++;

			}
			j = i+1;
			para = false;
		}
		// We are in the current scope, and the current character
		// is a parallel bar or the end of the chp string. This is
		// the middle of a parallel sub block.
		else if (depth[0] == 0 && depth[1] == 0 && depth[2] == 0 && ((*i == '|' && *(i+1) == '|') || i == chp.end()))
			para = true;
	}

	change_state.clear();
	for (vi = affected.begin(); vi != affected.end(); vi++)
	{
		l = changes.begin()->find(vi->first);
		si = states.find(vi->first);
		if (l != changes.begin()->end() && si != states.end())
		{
			l->second = l->second || (*si->second.states.rbegin());
			change_state.insert(pair<string, state>(vi->first, l->second));
		}
		else if (l != changes.begin()->end())
			change_state.insert(pair<string, state>(vi->first, l->second));
		else if (si != states.end())
		{
			changes.begin()->insert(pair<string, state>(vi->first, (*si->second.states.rbegin())));
			change_state.insert(pair<string, state>(vi->first, (*si->second.states.rbegin())));
		}
	}
	changes.push_back(change_state);

	cout << endl;

	cout << tab << chp << endl;
	for(si = states.begin(); si != states.end(); si++)
	{
		cout << tab << si->second << endl;
		result.insert(pair<string, state>(si->first, *(si->second.states.rbegin())));
	}
	cout << tab << "Waits: ";
	for (pj = waits.begin(); pj != waits.end(); pj++)
		cout << *pj << " ";
	cout << endl;

	cout << tab << "Changes: ";
	for (xi = changes.begin(); xi != changes.end(); xi++)
	{
		for (m = xi->begin(); m != xi->end(); m++)
			cout << m->first << ":" << m->second << " ";
		cout << ", ";
	}
	cout << endl;

	cout << tab << "Result:\t";

	for (l = result.begin(); l != result.end(); l++)
		cout << "{" << l->first << " = " << l->second << "} ";
	cout << endl;

	rules = production_rule(states, global, tab);

	list<rule>::iterator ri;
	list<int> state_locations; 	//See where_state_var's return for details of how this is being used.
	list<int>::iterator li;
	list<int> temp;

	for (ri = rules.begin(); ri != rules.end(); ri++){
		cout << tab << *ri << endl;
		cout << tab << "Production rules vs. desired functionality: ";
		temp = where_state_var(ri->left, ri->right, tab);
		state_locations.merge(temp);
	}

	list<instruction*>::iterator inst_setter;
//	instruction *state_inst_up, *state_inst_down;
	int highest_state_name = 0;
	int search_done = -1;
	int how_many_added = 0;
	int how_many_inserted = 0;
	list<pair<string,int> > to_insertl;
	//Loop through our list of desired state variable insertions.
	for (li = state_locations.begin(); li != state_locations.end(); li++){
			//Find the lowest variable name not in globals (so no conflicts)
			search_done = -1;
			highest_state_name = 0;
			while (global.find("sv"+to_string(highest_state_name)) != global.end()){
				cout << "sv"<<to_string(highest_state_name)<< " used, try sv" << highest_state_name+1 << endl;
				highest_state_name += 1;
			}
			cout << "adding variable sv" << to_string(highest_state_name) << endl;
			v = new variable("int<1>sv" + to_string(highest_state_name), tab);
			//Add to globals
			cout <<v->name<<endl;
			global.insert(pair<string, variable*>(v->name, v));
			//Add to locals
			local.insert(pair<string, variable*>(v->name, v));


			//Make an instruction for state up
			cout << tab << "Up instruction added at" << *li << endl;
			to_insertl.push_back(pair<string,int>(v->name+":=1",*li));
			raw = "int<1>" + v->name + ":=0;" + v->name + ":=0;" + raw;
			how_many_added++;
			cout << endl<< "up instr added "<< v->name << ":=1" << endl;
			how_many_added++;

			//cout << "Down instruction added at " << *(++li) << endl;
			//Make an instruction for state down
			//to_insertl.push_back(pair<string,int>(v->name+":=0",*li));

			//cout << "down instr added " << v->name + ":=0" <<endl<<endl;

	}

	//Print out our to_insert
	list<pair<string,int> >::iterator instr_adderl;
	raw = ";" +raw+";";
	for(instr_adderl = to_insertl.begin(); instr_adderl != to_insertl.end();instr_adderl++)
	{
		cout << instr_adderl->first << "-->" << instr_adderl->second << endl;
		int insertion_location = 0;
		for(int counter = 0; counter < (instr_adderl->second+how_many_inserted+how_many_added-1); counter++){
			insertion_location = raw.find(";",insertion_location+1);
		}
		if(insertion_location < 0)
			cout << "Everything is wrong and unholy" << endl;
		else{
			cout << "insertion location " << insertion_location << endl;
			cout << "That is the " << instr_adderl->second+how_many_inserted-1 << "th ';' = " << instr_adderl->second << "+" << how_many_inserted << "-1" << endl;
			raw = raw.substr(0, insertion_location+1) + instr_adderl->first + ";" + raw.substr(insertion_location+1);
			how_many_inserted++;
		}
	}
	if(how_many_inserted > 0)
	{
		cout << endl << "The chp with inserted state vars:"<<endl;
		cout<<raw<<endl;
	}


	if(how_many_added > 0){		//If we added a state variable...
		//Now that we have the corrected instruction stream, rerun parse!

		raw = raw.substr(1,raw.end()-raw.begin() - 2);
		cout << "Reparsing on " << raw << endl;
		/*
		//But no memory leaks for me!
		chp = "";
		_kind = "block";

		map<string, variable*>::iterator i;
		for (i = local.begin(); i != local.end(); i++)
		{
			if (i->second != NULL)
				delete i->second;
			i->second = NULL;
		}

		local.clear();

		list<instruction*>::iterator j;
		for (j = instrs.begin(); j != instrs.end(); j++)
		{
			if (*j != NULL)
				delete *j;
			*j = NULL;
		}

		instrs.clear();
		*/

		cout << "=REDO==REDO==REDO==REDO==REDO=" << endl;		//Let the user know we are trashing the above block.
		//parse(raw, types, vars, init, tab);
	}else{
		cout << tab << "=GOOD==GOOD==GOOD==GOOD==GOOD=" << endl;		//This block is done correctly.
	}


}

list<rule> production_rule(map<string, space> states, map<string, variable*> global, string tab)
{
	// Generate the production rules
	map<string, space> invars;
	map<string, space>		::iterator	si, sj;
	int bi0, bi1, o;
	int scount, ccount;
	int mscount, mcount;
	space tempspace, setspace;
	string invar;
	rule r, f;
	list<rule> rules;
	bool first, found;

	for (si = states.begin(); si != states.end(); si++)
	{
		for (bi0 = 0; bi0 < global.find(si->first)->second->width; bi0++)
		{
			cout << tab << "================Production Rule================" << endl;
			f.right = up(si->second[bi0]);
			cout << tab << "+++++++++++++++++++++++++++++++++++++++++++++++" << endl;
			cout << tab << f.right << "\t" << count(f.right) << "\t" << strict_count(f.right) << endl;
			for (o = 0; o < delta_count(f.right); o++)
			{
				r.clear(si->second.states.size());
				r.right = up(si->second[bi0], o);

				mscount = strict_count(r.right);
				mcount = r.right.states.size() - count(r.right);

				invars.clear();
				for (sj = states.begin(); sj != states.end(); sj++)
					for (bi1 = 0; bi1 < global.find(sj->first)->second->width; bi1++)
						if (sj != si || bi0 != bi1)
							invars.insert(pair<string, space>(sj->first + to_string(bi1), sj->second[bi1]));

				first = true;
				found = true;
				while (invars.size() > 0 && found && count(r.left) > count(r.right))
				{
					cout << tab << "...................Iteration..................." << endl;
					setspace = r.left;

					found = false;
					for (sj = invars.begin(); sj != invars.end(); sj++)
					{
						if (first)
							tempspace = sj->second;
						else
							tempspace = r.left & sj->second;

						scount = strict_count(r.right & tempspace);
						ccount = count(tempspace) - count(r.right & tempspace);

						if (ccount < mcount && scount >= mscount && r.left.var.find(tempspace.var) == r.left.var.npos)
						{
							setspace = tempspace;
							invar = sj->first;
							mcount = ccount;
							mscount = scount;
						}

						cout << tab << "\t" << tempspace << "\t" << ccount << "/" << mcount << "\t" << scount << "/" << mscount << endl;

						if (first)
							tempspace = ~sj->second;
						else
							tempspace = r.left & (~sj->second);

						scount = strict_count(r.right & tempspace);
						ccount = count(tempspace) - count(r.right & tempspace);

						if (ccount < mcount && scount >= mscount && r.left.var.find(tempspace.var) == r.left.var.npos)
						{
							setspace = tempspace;
							invar = sj->first;
							mcount = ccount;
							mscount = scount;
						}

						cout << tab << "\t" << tempspace << "\t" << ccount << "/" << mcount << "\t" << scount << "/" << mscount << endl;
					}

					if (r.left.var.find(setspace.var) == r.left.var.npos)
					{
						r.left = setspace;
						invars.erase(invar);
						first = false;
						found = true;
					}
				}

				if (o == 0)
					f = r;
				else
					f.left = f.left | r.left;
			}

			if (delta_count(f.right) > 0)
				rules.push_back(f);


			f.right = down(si->second[bi0]);

			cout << tab << "-----------------------------------------------" << endl;
			cout << tab << f.right << "\t" << count(f.right) << "\t" << strict_count(f.right) << endl;
			for (o = 0; o < delta_count(f.right); o++)
			{
				r.clear(si->second.states.size());
				r.right = down(si->second[bi0], o);

				mscount = strict_count(r.right);
				mcount = r.right.states.size() - count(r.right);

				invars.clear();
				for (sj = states.begin(); sj != states.end(); sj++)
					for (bi1 = 0; bi1 < global.find(sj->first)->second->width; bi1++)
						if (sj != si || bi0 != bi1)
							invars.insert(pair<string, space>(sj->first + to_string(bi1), sj->second[bi1]));

				first = true;
				found = true;
				while (invars.size() > 0 && found && count(r.left) > count(r.right))
				{
					cout << tab << "...................Iteration..................." << endl;
					setspace = r.left;

					found = false;
					for (sj = invars.begin(); sj != invars.end(); sj++)
					{
						if (first)
							tempspace = sj->second;
						else
							tempspace = r.left & sj->second;

						scount = strict_count(r.right & tempspace);
						ccount = count(tempspace) - count(r.right & tempspace);

						if (ccount < mcount && scount >= mscount && r.left.var.find(tempspace.var) == r.left.var.npos)
						{
							setspace = tempspace;
							invar = sj->first;
							mcount = ccount;
							mscount = scount;
						}

						cout << tab << "\t" << tempspace << "\t" << ccount << "/" << mcount << "\t" << scount << "/" << mscount << endl;

						if (first)
							tempspace = ~sj->second;
						else
							tempspace = r.left & (~sj->second);

						scount = strict_count(r.right & tempspace);
						ccount = count(tempspace) - count(r.right & tempspace);

						if (ccount < mcount && scount >= mscount && r.left.var.find(tempspace.var) == r.left.var.npos)
						{
							setspace = tempspace;
							invar = sj->first;
							mcount = ccount;
							mscount = scount;
						}

						cout << tab << "\t" << tempspace << "\t" << ccount << "/" << mcount << "\t" << scount << "/" << mscount << endl;
					}

					if (r.left.var.find(setspace.var) == r.left.var.npos)
					{
						r.left = setspace;
						invars.erase(invar);
						first = false;
						found = true;
					}
				}

				if (o == 0)
					f = r;
				else
					f.left = f.left | r.left;
			}

			if (delta_count(f.right) > 0)
				rules.push_back(f);
		}
	}

	return rules;
}

//There can be four states: 1, 0, X, _ where underscore is empty set and X is full set.
//The return list will be even by design. Each pair is a different state variable. The first
//element of the pair is the index of state up, the second is state down.
//Ex: 0, 1, 5, 7 means sv0 goes high at 0, low at 1, and s2 goes high at 5, and low at 7.
list<int> where_state_var(space left, space right, string tab)
{
	list<state> left_list, right_list;
	list<state>::iterator i,j;
	list<int> state_locations;
	state result;
	string a, b, conflicts = "";
	left_list = left.states;
	right_list = right.states;

	//Loop through all of the production rule states (left) and the corresponding desired functionality (right)
	for (i = left_list.begin(),j = right_list.begin() ; i != left_list.end() && j != right_list.end(); i++, j++)
	{
		if(i->data == "0" && j->data == "0" )
			conflicts += ".";		//Doesn't fire, shouldn't fire. Good.
		else if(i->data == "0" && j->data == "1" ){
			cout << "ERROR: Production rule doesn't fire during a place where it should." << endl;
			conflicts += "E";	//Error fire! Our PRS aren't good enough.
		}
		else if(i->data == "1" && j->data == "0" )
			conflicts += "C";  // Illegal fire (fires when it shouldn't)
		else if(i->data == "1" && j->data == "1" )
			conflicts += "!";  // This fires, and it must keep firing after we after we add a state var
		else if(j->data == "X" )
			conflicts += "."; 	//Don't really care if it fires or not. Ambivalence.
		else if(i->data == "X" && j->data == "0")
			conflicts += "C";
		else{
			cout << "ERROR! State var generate is very confused right now. " << endl;
			conflicts += "E";	//Error fire! Not quite sure how you got here...
		}
	}
	//Format for conflicts string:
	//. is 'allowable',
	//E is error,
	//C is conflict if no state variable
	//! is necessary fire
	//S is resolved necessary fire given conflicts (surrounded by state variable)
	cout << conflicts << endl;
	if(conflicts.find_first_of("C") == conflicts.npos)
	{
		return state_locations;		//No conflicts! We are golden.
	}
	// For now, I am simply going to surround the conflicting state with a state variable. This is probably not optimal.
	size_t st = conflicts.find_first_of("!");
	while (st != conflicts.npos)
	{
		if ((conflicts.find_first_of("C") != conflicts.npos)&&(conflicts.find_first_of("C") < st)) //Insert state before required instr
			state_locations.push_back(st+1);
		else
			state_locations.push_back(st+2);


		//state_locations.push_back(st);

		conflicts = conflicts.substr(0,st) + "S" + conflicts.substr(st+1);
		//Get the next conflict
		//cout << tab << conflicts << endl;
		st = conflicts.find_first_of("!");
	}
	return state_locations;

}

//Legacy code (don't delete till I am done with it)
/*
	for (li = state_locations.begin(); li != state_locations.end(); li++){
		cout << tab << *li << endl;

		//Find the lowest variable name not in globals (so no conflicts)
		search_done = -1;
		while (global.find("sv"+to_string(highest_state_name)) != global.end()){
			cout << "FGASGFSDFG " << highest_state_name++ << endl;
		}
		cout << "=========" << to_string(highest_state_name) << "=====" << highest_state_name<<endl;
		cout << "adding variable sv" << to_string(highest_state_name) << endl;
		v = new variable("int<1>sv" + to_string(highest_state_name), tab);

		//Add to globals
		cout <<v->name<<endl;
		global.insert(pair<string, variable*>(v->name, v));
		if( global.find("sv"+to_string(highest_state_name)) == global.end() )
			cout << "WE HAVE SERIOUS PROBLEMS"<<endl;
		//Add to locals
		local.insert(pair<string, variable*>(v->name, v));

		//Make an instruction for state up
		state_inst_up = new instruction("int<1>"+v->name + ":=1", types, global, current_state, tab+"\t");
		inst_setter = instrs.begin();
		for (int tcounter = 0;tcounter<(*li+how_many_added); tcounter++){
			inst_setter++;
			cout << "ddebug"<<tcounter<<endl;
		}
		instrs.insert(inst_setter, state_inst_up);
		cout << "up instr added "<<state_inst_up->chp <<endl;
		how_many_added++;
		cout << "How many total added = "<< how_many_added<< endl;
		cout << tab << *(++li) << endl;

		//Make an instruction for state down
		state_inst_down = new instruction(v->name + ":=0", types, global, current_state, tab+"\t");
		inst_setter = instrs.begin();
		for (int tcounter = 0;tcounter<(*li+how_many_added); tcounter++){
			inst_setter++;
			cout << "debug"<<tcounter<<endl;
		}
		instrs.insert(inst_setter, state_inst_down);
		cout << "down instr added" <<endl;
		how_many_added++;
		cout << "How many added = "<< how_many_added<< endl;
	} */

/*
//Loop through all the instructions to create a new unified CHP string
for (inst_setter = instrs.begin(); inst_setter != instrs.end(); inst_setter++){
	corrected = corrected + (*inst_setter)->chp+ ";";
}
corrected = corrected.substr(0, corrected.end()-corrected.begin()+1); //Remove the tail semicolon.
cout <<"CHP updated with added instruction:" << endl;
cout << corrected << endl;
*/


