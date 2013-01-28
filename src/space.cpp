/*
 * space.cpp
 *
 *  Created on: Oct 29, 2012
 *      Author: Ned Bingham and Nicholas Kramer
 *
 *  DO NOT DISTRIBUTE
 */

#include "space.h"
#include "state.h"
#include "common.h"

int state_space::size()
{
	return states.size();
}

int trace_space::size()
{
	return traces.size();
}
