#ifndef IO_H
#define IO_H


#include "board.h"

//io.c
extern char * PrSq(const int sq);
extern void PrintMoveList(const S_MOVELIST *list,S_BOARD *pos);
extern char * PrMove(const int move);
extern void printBitBoard(U64 bitboard);
extern int ParseMove(char *ptrChar, S_BOARD *pos);
extern void printFen(const S_BOARD *pos,char *fen);


#endif