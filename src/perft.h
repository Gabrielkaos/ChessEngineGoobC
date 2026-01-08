#ifndef PERFT_H
#define PERFT_H

#include "defs.h"
#include "board.h"

//perft.c
extern void PerftTest(int depth,S_BOARD *pos);
extern void BenchTest(int depth,S_BOARD *pos);
extern U64 countNps(U64 nodes, int time);


#endif