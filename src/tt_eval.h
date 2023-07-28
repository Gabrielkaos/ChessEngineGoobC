#ifndef TT_EVAL_H
#define TT_EVAL_H

//tt_eval.c
extern void clearEvalTable(EVAL_TABLE *eTable);
extern void InitEvalTable(EVAL_TABLE *table,const int mb,int noisy);
extern void StoreTTEval(S_BOARD *pos,int Eval);
extern int ProbeTTEval(const S_BOARD *pos);
extern void clearPawnKingTable(PAWNKING_TABLE *eTable);
extern void InitPawnKingTable(PAWNKING_TABLE *table,const int mb,int noisy);
extern void StorePawnKingEval(S_BOARD *pos);
extern int ProbePawnKingEval(S_BOARD *pos);

#endif // TT_EVAL_H
