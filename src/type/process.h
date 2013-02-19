/*
 * process.h
 *
 *  Created on: Oct 28, 2012
 *      Author: Ned Bingham and Nicholas Kramer
 *
 *  DO NOT DISTRIBUTE
 */

#ifndef process_h
#define process_h

#include "../common.h"
#include "../data.h"
#include "../syntax.h"
#include "keyword.h"

/* This structure represents a process. Processes act in parallel
 * with each other and can only communicate with other processes using
 * channels. We need to keep track of the block that defines this process and
 * the input and output signals. The final element in this structure is
 * a list of production rules that are the result of the compilation.
 */
struct process : keyword
{
	process();
	process(string raw, map<string, keyword*> types, int verbosity);
	~process();

	string					chp;	// the raw process definition
	parallel				def;	// the chp that defined this process
	list<string>			prs;	// the final set of generated production rules
	vspace					vars;
	list<string>			input;

	process &operator=(process p);

	void parse(string raw, map<string, keyword*> types, int verbosity);
};

#endif
