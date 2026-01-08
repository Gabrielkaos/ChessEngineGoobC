#ifndef INIT_H
#define INIT_H


#include "defs.h"

extern U64 pieceKeys[13][BOARD_NUMS_SQ];
extern U64 sideKey;
extern U64 castleKeys[16];

//init.c
extern void AllInit();

#endif