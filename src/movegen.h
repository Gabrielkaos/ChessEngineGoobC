#ifndef MOVEGEN_H
#define MOVEGEN_H


#include "board.h"

//movegen.c
//extern void GenerateAllQuiets(const S_BOARD *pos,S_MOVELIST *list);
extern void GenerateAllMoves(const S_BOARD *pos,S_MOVELIST *list);
extern void GenerateAllNoisy(const S_BOARD *pos,S_MOVELIST *list);
extern int MoveExists(S_BOARD *pos,const int move);
//extern void InitMvvLva();


#endif