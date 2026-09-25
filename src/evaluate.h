#ifndef EVAL_H
#define EVAL_H

#include "stdint.h"
#include "defs.h"
#include "board.h"

extern int DistanceBetween[64][64];
extern void initDistancesForEval(void);

extern int EvalPosition(S_BOARD *pos);

#endif // EVAL_H
