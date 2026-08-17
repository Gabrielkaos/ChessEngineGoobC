
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
extern void clearPawnKingTable(PAWNKING_TABLE *eTable);
extern void InitPawnKingTable(PAWNKING_TABLE *table,const int mb,int noisy);
extern void StorePawnKingEval(S_BOARD *pos, EVAL_INFO *eval_info);
extern int ProbePawnKingEval(S_BOARD *pos, EVAL_INFO *eval_info);

// Persistent per-thread pawn/eval hash tables (allocated once, reused across searches)
extern PAWNKING_TABLE threadPawnTable[MAXTHREADS];
extern EVAL_TABLE threadEvalTable[MAXTHREADS];
extern int currentPawnHashMB;
extern int currentEvalHashMB;
extern void EnsureThreadTables(int numThreads);
extern void ClearThreadTables(int numThreads);
extern void ReallocThreadTables(int newPawnMB, int newEvalMB);
extern void FreeAllThreadTables(void);

#endif // TT_EVAL_H
