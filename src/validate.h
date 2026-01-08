#ifndef VALIDATE_H
#define VALIDATE_H

#include "board.h"

//validate.c
extern int moveValid(const int move);
extern void Evaltest(S_BOARD *pos);
extern int SqOnBoard(const int sq);
extern int SideValid(const int side);
extern int FileRankValid(const int fr);
extern int PieceValidEmpty(const int pce);
extern int PieceValid(const int pce);

#endif