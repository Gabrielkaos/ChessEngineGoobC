
#ifndef TT_EVAL_H
#define TT_EVAL_H

#include "board.h"
#include "evaluate.h"
#include "thread.h"

//tt_eval.c
extern void clearEvalTable(EVAL_TABLE *eTable);
extern void InitEvalTable(EVAL_TABLE *table,const int mb,int noisy);
extern void StoreTTEval(S_BOARD *pos,int Eval);
extern int ProbeTTEval(const S_BOARD *pos);

// Persistent per-thread eval hash tables (allocated once, reused across searches)
extern EVAL_TABLE threadEvalTable[MAXTHREADS];
extern int currentEvalHashMB;
extern void EnsureThreadTables(int numThreads);
extern void ClearThreadTables(int numThreads);
extern void ReallocThreadTables(int newEvalMB);
extern void FreeAllThreadTables(void);

#endif // TT_EVAL_H
