/*
 * instruction.h
 *
 *  Created on: Oct 23, 2012
 *      Author: Ned Bingham
 */

#include "common.h"

#ifndef instruction_h
#define instruction_h

/* This structure describes an instruction in the chp program, namely what lies between
 * two semicolons in a block of. This has not been expanded to ;S1||S2; type of composition.
 */

struct instruction
{
	instruction()
	{
		var_affected = "";
		val_at_end = "";
	}
	instruction(string chp)
	{
		parse(chp);
	}
	~instruction()
	{
		var_affected = "";
		val_at_end = "";
	}

	string		var_affected;	// the name of the variable this instruction operates on.
	string		val_at_end;		// the value that the variable is set to. i0, i1, iX, o0, o1, oX, in, on

	instruction &operator=(instruction v)
	{
		var_affected = v.var_affected;
		val_at_end = v.val_at_end;
		return *this;
	}

	void parse(string chp)
	{


		cout << "\t\tInstruction:\t "+chp << endl;

		string::iterator i;
		int name_end;
		int assign_start;
		/*for (i = chp.begin(); i != chp.end(); i++)
		{

		}*/
		name_end = chp.find_first_of(" .=-!?;:|,*+()[]{}&<>@#");
		var_affected = chp.substr(0,name_end);
		cout << "\t\t\tVariable affected -> " << var_affected << endl;

		if(chp.find_first_of(":")!=chp.npos){
			assign_start = chp.find_first_of(":");
			val_at_end = chp.substr(assign_start+2);
			cout << "\t\t\tValue at end -> " << val_at_end << endl;
		}





	}
};

#endif
