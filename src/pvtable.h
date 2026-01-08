#ifndef PVTABLE_H
#define PVTABLE_H

#include "defs.h"
#include "board.h"

//pvtable.c

extern int hashfullTT(S_PVTABLE *table);
extern void updateAge(S_PVTABLE *table);
extern int valueFromTT(int value,int ply);
extern int valueToTT(int value,int ply);
extern void InitPvTable(S_PVTABLE *table,const int mb,int noisy);
extern int ProbePvTable(const S_BOARD *pos, S_PVTABLE *table);
extern void StoreHashEntry(S_BOARD *pos, S_PVTABLE *table,const int move, int score, const int flags, const int depth,const int eval);
extern void clearPvTable(S_PVTABLE *table);
extern int getPvLine(const int depth,S_BOARD *pos, S_PVTABLE *table);
extern int ProbeHashEntry(S_BOARD *pos, S_PVTABLE *table, int *move, int *score,int *ttDepth,int *ttBound,int *ttEval);
extern void TestHASH(char *fen);

#endif //PVTABLE_H