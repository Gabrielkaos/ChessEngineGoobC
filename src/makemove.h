#ifndef MAKEMOVE_H
#define MAKEMOVE_H

#include "board.h"


//makemove.c
extern int getCapturedPiece(int move);
extern int moveIsTactical(S_BOARD *pos,int move);
extern int MoveBestCaseValue(S_BOARD *pos);
extern int makeMove(S_BOARD *pos,int move);
extern void takeMove(S_BOARD *pos);
extern void takeNullMove(S_BOARD *pos);
extern void makeNullMove(S_BOARD *pos);
extern int moveEstimatedValue(S_BOARD *pos, int move);


#endif