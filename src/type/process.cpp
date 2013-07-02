/*
 * process.cpp
 *
 *  Created on: Oct 29, 2012
 *      Author: Ned Bingham and Nicholas Kramer
 *
 *  DO NOT DISTRIBUTE
 */

#include "../common.h"
#include "../utility.h"
#include "../syntax.h"
#include "../data.h"

#include "process.h"
#include "record.h"
#include "channel.h"

process::process()
{
	name = "";
	_kind = "process";
	verbosity = 0;
	chp = "";
	is_inline = false;
}

process::process(string raw, map<string, keyword*> *types, int verbosity)
{
	_kind = "process";
	vars.types = types;
	this->verbosity = verbosity;
	this->chp = raw;
	is_inline = false;

	parse(raw);
}

process::~process()
{
	name = "";
	_kind = "process";
	verbosity = 0;
	chp = "";

	vars.clear();
}

process &process::operator=(process p)
{
	def = p.def;
	prs = p.prs;
	vars = p.vars;
	return *this;
}

void process::parse(string raw)
{
	if (raw.compare(0, 7, "inline ") == 0)
	{
		is_inline = true;
		raw = raw.substr(7);
		chp = raw;
	}

	size_t name_start = chp.find_first_of(" ")+1;
	size_t name_end = chp.find_first_of("(");
	size_t input_start = chp.find_first_of("(")+1;
	size_t input_end = chp.find_first_of(")");
	size_t sequential_start = chp.find_first_of("{")+1;
	size_t sequential_end = chp.length()-1;
	string io_sequential;
	string::iterator i, j;

	map<string, variable> temp;
	map<string, variable>::iterator vi, vj;
	map<string, keyword*>::iterator ti;

	name = chp.substr(name_start, name_end - name_start);
	io_sequential = chp.substr(input_start, input_end - input_start);

	if ((verbosity & VERB_BASE_HSE) && (verbosity & VERB_DEBUG))
	{
		cout << "Process:\t" << chp << endl;
		cout << "\tName:\t" << name << endl;
		cout << "\tArgs:\t" << io_sequential << endl;
	}

	for (i = io_sequential.begin(), j = io_sequential.begin(); i != io_sequential.end(); i++)
	{
		if (*(i+1) == ',' || i+1 == io_sequential.end())
		{
			expand_instantiation(NULL, io_sequential.substr(j-io_sequential.begin(), i+1 - j), &vars, &args, "\t", verbosity, false);
			j = i+2;
		}
	}

	string sequential = chp.substr(sequential_start, sequential_end - sequential_start);

	if (!is_inline)
	{
		expand_instantiation(NULL, "__sync call", &vars, &args, "\t", verbosity, false);
		sequential = "[call.r];call.a+;(" + sequential + ");[~call.r];call.a-";
	}

	def.init(sequential, &vars, "\t", verbosity);

	if ((verbosity & VERB_BASE_HSE) && (verbosity & VERB_DEBUG))
	{
		cout << "\tVariables:" << endl;
		vars.print("\t\t");
		cout << "\tHSE:" << endl;
		def.print_hse("\t\t");
		cout << endl << endl;
	}
}

void process::merge()
{
	def.merge();

	if (verbosity & VERB_MERGED_HSE)
	{
		def.print_hse("");
		cout << endl;
	}
}

// TODO Projection algorithm - when do we need to do projection? when shouldn't we do projection?
void process::project()
{
	if (verbosity & VERB_PROJECTED_HSE)
	{
		def.print_hse("");
		cout << endl;
	}
}

// TODO Process decomposition - How big should we make processes?
void process::decompose()
{
	if (verbosity & VERB_DECOMPOSED_HSE)
	{
		def.print_hse("");
		cout << endl;
	}
}

// TODO Handshaking Reshuffling
void process::reshuffle()
{
	if (verbosity & VERB_RESHUFFLED_HSE)
	{
		def.print_hse("");
		cout << endl;
	}
}

// TODO There is a problem with the interaction of scribe variables with bubbleless reshuffling because scribe variables insert bubbles
void process::generate_states()
{
	cout << "Process" << endl;
	net.values = &values;
	net.vars = &vars;

#ifdef METHOD_PETRIFY_SIMPLE
	map<string, variable>::iterator i;
	for (i = vars.global.begin(); i != vars.global.end(); i++)
	{
		i->second.state0 = net.new_place(map<int, int>(), NULL);
		net.M0.push_back(i->second.state0);
		i->second.state1 = net.new_place(map<int, int>(), NULL);
	}
#endif

	vector<int> start;
	start.push_back(net.insert_place(start, vector<int>(), map<int, int>(), NULL));
	net.M0.push_back(start[0]);
	net.connect(def.generate_states(&net, start,  map<int, int>(), vector<int>()), start);

#ifdef METHOD_PETRIFY_SIMPLE
	for (i = vars.global.begin(); i != vars.global.end(); i++)
		if (!i->second.driven && i->second.arg)
		{
			net.connect(net.insert_transitions(i->second.state0, values.build(expression(i->first, &vars).expr), map<int, int>(), NULL), i->second.state1);
			net.connect(net.insert_transitions(i->second.state1, values.build(expression("~" + i->first, &vars).expr), map<int, int>(), NULL), i->second.state0);
		}
#endif

	net.trim();
	net.tails();
}

void process::insert_state_vars()
{
	map<int, list<vector<int> > > c = net.conflicts();
	map<int, list<vector<int> > >::iterator i;
	list<vector<int> >::iterator lj;
	map<int, int>::iterator m, n;
	int j, k, l;
	list<path>::iterator li;

	cout << "Conflicts: " << name << endl;

	for (i = c.begin(); i != c.end(); i++)
	{
		cout << i->first << ": ";
		for (lj = i->second.begin(); lj != i->second.end(); lj++)
		{
			cout << "{";
			for (j = 0; j < (int)lj->size(); j++)
				cout << (*lj)[j] << " ";
			cout << "} ";
		}
		cout << endl;
	}

	vector<int> implicants;
	vector<int> arcs;

	for (j = 0; j < (int)net.T.size(); j++)
		if (net.T[j].active)
			implicants.push_back(net.trans_id(j));

	cout << "Implicants: ";
	for (j = 0; j < (int)implicants.size(); j++)
	{
		cout << net.index(implicants[j]) << "{(";
		arcs = net.input_arcs(implicants[j]);
		for (k = 0; k < (int)arcs.size(); k++)
		{
			if (k != 0)
				cout << ", ";
			cout << arcs[k];
		}
		cout << ") -> (";
		arcs = net.output_arcs(implicants[j]);
		for (k = 0; k < (int)arcs.size(); k++)
		{
			if (k != 0)
				cout << ", ";
			cout << arcs[k];
		}
		cout << ")} ";
	}
	cout << endl;

	path up;
	path_space up_paths;
	path down;
	path_space down_paths;
	vector<int> uptrans, downtrans;
	vector<pair<vector<int>, vector<int> > > ip;
	int um, dm;
	int ium, idm;
	for (i = c.begin(); i != c.end(); i++)
	{
		for (lj = i->second.begin(); lj != i->second.end(); lj++)
		{
			up_paths = net.get_paths(i->first, *lj, path(net.S.size()));
			up = net.restrict_path(up_paths.total, implicants);
			down_paths = net.get_paths(*lj, i->first, path(net.S.size()));
			down = net.restrict_path(down_paths.total, implicants);

			cout << "Up:" << endl;
			uptrans.clear();
			while ((ium = up.max()) != -1)
			{
				cout << up << endl;
				uptrans.push_back(implicants[ium]);
				arcs = net.input_arcs(implicants[ium]);
				for (j = 0; j < (int)arcs.size(); j++)
					up_paths = up_paths.avoidance(arcs[j]);
				up = net.restrict_path(up_paths.total, implicants);
			}

			cout << "Down:" << endl;
			downtrans.clear();
			while ((idm = down.max()) != -1)
			{
				cout << down << endl;
				downtrans.push_back(implicants[idm]);
				arcs = net.input_arcs(implicants[idm]);
				for (j = 0; j < (int)arcs.size(); j++)
					down_paths = down_paths.avoidance(arcs[j]);
				down = net.restrict_path(down_paths.total, implicants);
			}
			cout << endl;

			unique(&uptrans, &downtrans);

			if (uptrans.size() == 0 || downtrans.size() == 0)
			{
				cout << "Error: No solution for the conflict set: " << i->first << " -> {";
				for (j = 0; j < (int)lj->size(); j++)
					cout << (*lj)[j] << " ";
				cout << "}." << endl;
			}
			else if (downtrans < uptrans)
				ip.push_back(pair<vector<int>, vector<int> >(downtrans, uptrans));
			else
				ip.push_back(pair<vector<int>, vector<int> >(uptrans, downtrans));
		}
	}

	unique(&ip);

	string vname;
	int vid;
	for (j = 0; j < (int)ip.size(); j++)
	{
		string vname = vars.unique_name("_sv");
		vid = vars.insert(variable(vname, "node", 1, false));

		um = values.mk(vid, 0, 1);
		dm = values.mk(vid, 1, 0);

		for (k = 0; k < (int)ip[j].first.size(); k++)
		{
			net.T[net.index(ip[j].first[k])].index = values.apply_and(net.T[net.index(ip[j].first[k])].index, um);
			for (l = 0; l < (int)net.S.size(); l++)
			{
				for (m = net.T[net.index(ip[j].first[k])].branch.begin(); m != net.T[net.index(ip[j].first[k])].branch.end(); m++)
				{
					n = net.S[l].branch.find(m->first);
					if (n != net.S[l].branch.end() && n->second != m->second)
						net.S[l].mutables.push_back(vid);
				}
				unique(&net.S[l].mutables);
			}
		}
		for (k = 0; k < (int)ip[j].second.size(); k++)
		{
			net.T[net.index(ip[j].second[k])].index = values.apply_and(net.T[net.index(ip[j].second[k])].index, dm);
			for (l = 0; l < (int)net.S.size(); l++)
			{
				for (m = net.T[net.index(ip[j].second[k])].branch.begin(); m != net.T[net.index(ip[j].second[k])].branch.end(); m++)
				{
					n = net.S[l].branch.find(m->first);
					if (n != net.S[l].branch.end() && n->second != m->second)
						net.S[l].mutables.push_back(vid);
				}
				unique(&net.S[l].mutables);
			}
		}
	}

	for (k = 0; k < (int)net.S.size(); k++)
		net.S[k].index = 1;
	for (k = 0; k < (int)net.S.size(); k++)
		net.update(k);
}

void process::generate_prs()
{
	for (int vi = 0; vi < vars.size(); vi++)
		if (vars.get_name(vi).find_first_of("|&~") == string::npos)
			prs.push_back(rule(vi, &net, &vars, verbosity));

	if (verbosity & VERB_BASE_PRS)
	{
		cout << "Production Rules: " << name << endl;
		print_prs();
	}
}

/* TODO: Factoring - production rules should be relatively short.
 * Look for common expressions between production rules and factor them
 * out into their own variable.
 */
void process::factor_prs()
{

	//Not as trivial as seemed on initial exploration.
	//Must find cost function for inserting state variable.
	//Must find benefit for inserting factor
	//Balance size of factored chunk vs how many rules factored from
	//Prioritize longer rules to reduce capacitive driving (i.e. not every 'factor removed' is equal)
	//MUST factor if over cap in series/parallel

	if (verbosity & VERB_FACTORED_PRS)
		print_prs();
}

void process::print_hse(ostream *fout)
{
	def.print_hse("", fout);
}

void process::print_dot(ostream *fout)
{
	int i, j;
	string label;
	(*fout) << "digraph " << name << endl;
	(*fout) << "{" << endl;

	for (i = 0; i < (int)net.S.size(); i++)
		if (!net.dead(i))
		{
			if (net.S[i].index >= 0)
				label = values.expr(net.S[i].index, vars.get_names());//values.trace(net.S[i].index, vars.get_names());
			else
				label = "";

			for (j = 0; j < (int)label.size(); j++)
				if (label[j] == '|')
					label = label.substr(0, j+1) + "\\n" + label.substr(j+1);

			label = to_string(i) + " " + label;

			(*fout) << "\tS" << i << " [label=\"" << label << "\"];" << endl;
		}

	for (i = 0; i < (int)net.T.size(); i++)
	{
		label = values.expr(net.T[i].index, vars.get_names());
		if (label != "")
			(*fout) << "\tT" << i << " [shape=box] [label=\"" << label << "\"];" << endl;
		else
			(*fout) << "\tT" << i << " [shape=box];" << endl;
	}

	for (i = 0; i < (int)net.Wp.size(); i++)
		for (j = 0; j < (int)net.Wp[i].size(); j++)
			if (net.Wp[i][j] > 0)
				(*fout) << "\tT" << j << " -> " << "S" << i << ";" <<  endl;

	for (i = 0; i < (int)net.Wn.size(); i++)
		for (j = 0; j < (int)net.Wn[i].size(); j++)
			if (net.Wn[i][j] > 0)
				(*fout) << "\tS" << i << " -> " << "T" << j << ";" <<  endl;

	(*fout) << "}" << endl;
}

void process::print_petrify()
{
	int i, j;
	vector<string> labels;
	map<string, int> labelmap;
	map<string, int>::iterator li;
	string label;
	FILE *file;
	map<string, variable>::iterator vi;
	bool first;

	file = fopen((name + ".g").c_str(), "wb");

	fprintf(file, ".model %s\n", name.c_str());

	first = true;
	for (vi = vars.global.begin(); vi != vars.global.end(); vi++)
		if (vi->second.arg && !vi->second.driven)
		{
			if (first)
			{
				fprintf(file, ".inputs");
				first = false;
			}
			fprintf(file, " %s", vi->second.name.c_str());
		}
	if (!first)
		fprintf(file, "\n");

	first = true;
	for (vi = vars.global.begin(); vi != vars.global.end(); vi++)
		if (vi->second.arg && vi->second.driven)
		{
			if (first)
			{
				fprintf(file, ".outputs");
				first = false;
			}
			fprintf(file, " %s", vi->second.name.c_str());
		}
	if (!first)
		fprintf(file, "\n");

	first = true;
	for (vi = vars.global.begin(); vi != vars.global.end(); vi++)
		if (!vi->second.arg)
		{
			if (first)
			{
				fprintf(file, ".internal");
				first = false;
			}
			fprintf(file, " %s", vi->second.name.c_str());
		}
	if (!first)
		fprintf(file, "\n");

	first = true;
	for (i = 0; i < (int)net.T.size(); i++)
	{
		label = values.expr(net.T[i].index, vars.get_names());
		if (label == "0" || label == "1")
		{
			if (first)
			{
				fprintf(file, ".dummy");
				first = false;
			}
			label = string("T") + to_string(i);
			fprintf(file, " %s", label.c_str());
		}
		li = labelmap.find(label);
		if (li == labelmap.end())
			labelmap.insert(pair<string, int>(label, 1));
		else
		{
			label += string("/") + to_string(li->second);
			li->second++;
		}

		labels.push_back(label);
	}
	if (!first)
		fprintf(file, "\n");

	string from;
	vector<string> to;

	fprintf(file, ".graph\n");
	for (i = 0; i < (int)net.Wp.size(); i++)
		for (j = 0; j < (int)net.Wp[i].size(); j++)
			if (net.Wp[i][j] > 0)
				fprintf(file, "%s S%d\n", labels[j].c_str(), i);

	for (i = 0; i < (int)net.Wn.size(); i++)
		for (j = 0; j < (int)net.Wn[i].size(); j++)
			if (net.Wn[i][j] > 0)
				fprintf(file, "S%d %s\n", i, labels[j].c_str());

	first = true;
	fprintf(file, ".marking {");
	for (i = 0; i < (int)net.M0.size(); i++)
	{
		if (!net.dead(net.M0[i]))
		{
			if (first)
				first = false;
			else
				fprintf(file, " ");
			fprintf(file, "S%d", net.M0[i]);
		}
	}
	fprintf(file, "}\n");
	fprintf(file, ".end\n");

	fclose(file);
}

void process::print_prs(ostream *fout)
{
	for (size_t i = 0; i < prs.size(); i++)
		(*fout) << prs[i];
	(*fout) << endl;
}
