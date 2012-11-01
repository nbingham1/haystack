/*
 * process.h
 *
 *  Created on: Oct 28, 2012
 *      Author: Ned Bingham
 */

#include "keyword.h"
#include "block.h"
#include "common.h"

#ifndef process_h
#define process_h

/* This structure represents a process. Processes act in parallel
 * with each other and can only communicate with other processes using
 * channels. We need to keep track of the block that defines this process and
 * the input and output signals. The final element in this structure is
 * a list of production rules that are the result of the compilation.
 */
struct process : keyword
{
	process();
	process(string chp, map<string, keyword*> typ);
	~process();

	block					def;	// the chp that defined this process
	list<string>			prs;	// the final set of generated production rules
	map<string, variable*>	io;		// the input and output signals of this process

	process &operator=(process p);

	void parse(string chp, map<string, keyword*> typ);
};

#endif