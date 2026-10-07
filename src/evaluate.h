#ifndef EVAL_H
#define EVAL_H

#include "stdint.h"
#include "defs.h"
#include "board.h"

extern int DistanceBetween[64][64];
extern void initDistancesForEval(void);

#include "nnue_loader.h"

static inline int EvalPosition(S_BOARD *pos) {
    // Null-move recognizer: estimate child eval after a null move as -parent_eval + 2*tempo (tempo = 20)
    if (pos->ply > 0 && pos->search && pos->search->moveStack[pos->ply - 1] == NULLMOVE) {
        return -pos->search->eval_stack[pos->ply - 1] + 40;
    }

    return nnue_eval(pos);
}

#endif // EVAL_H
