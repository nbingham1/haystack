/*
 * record.h
 *
 *  Created on: Oct 23, 2012
 *      Author: Ned Bingham and Nicholas Kramer
 *
 *  DO NOT DISTRIBUTE
 */

#include "common.h"
#include "keyword.h"
#include "variable.h"
#include "instruction.h"

#ifndef record_h
#define record_h

/* This structure represents a structure or record. A record
 * contains a bunch of member variables that help you index
 * segments of bits within the multibit signal.
 */
struct record : keyword
{
	record();
	record(string raw, map<string, keyword*> types, string tab, int verbosity);
	~record();

	string					chp;
	map<string, variable>	vars;	// the list of member variables that make up this record
	map<string, variable>	labels;

	record &operator=(record r);
	void parse(string raw, map<string, keyword*> types, string tab, int verbosity);
};

instruction *expand_instantiation(string chp, map<string, keyword*> types, map<string, variable> *global, map<string, variable> *label, list<string> *input, string tab, int verbosity, bool allow_process);
ostream &operator<<(ostream &os, record s);

#endif
