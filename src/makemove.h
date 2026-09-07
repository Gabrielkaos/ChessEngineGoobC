#ifndef MAKEMOVE_H
#define MAKEMOVE_H

#include "board.h"


//makemove.c
extern int moveIsTactical(S_BOARD *pos,int move);
extern int MoveBestCaseValue(S_BOARD *pos);
extern int legal(const S_BOARD *pos, int move);
extern void makeMove(S_BOARD *pos, int move, StateInfo *newSt);
extern void takeMove(S_BOARD *pos);
extern void takeNullMove(S_BOARD *pos);
extern void makeNullMove(S_BOARD *pos, StateInfo *newSt);
extern int moveEstimatedValue(S_BOARD *pos, int move);
extern int MoveExists(S_BOARD *pos, const int move);


#endif