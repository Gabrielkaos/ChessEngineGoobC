#include "stdio.h"
#include "stdlib.h"
#include "defs.h"
#include "board.h"
#include "bitboards.h"
#include "evaluate.h"
#include "some_maths.h"
#include "tt_eval.h"
#include "nnue_loader.h"

int DistanceBetween[64][64];

void initDistancesForEval(void) {
    for (int sq1 = 0; sq1 < 64; sq1++) {
        for (int sq2 = 0; sq2 < 64; sq2++) {
            DistanceBetween[sq1][sq2] = MAX(abs(filesBoard[sq1] - filesBoard[sq2]),
                                            abs(ranksBoard[sq1] - ranksBoard[sq2]));
        }
    }
}

int EvalPosition(S_BOARD *pos) {
    // Null-move recognizer: estimate child eval after a null move as -parent_eval + 2*tempo (tempo = 20)
    if (pos->ply > 0 && pos->search->moveStack[pos->ply - 1] == NULLMOVE) {
        return -pos->search->eval_stack[pos->ply - 1] + 40;
    }

    int hashedEval = ProbeTTEval(pos);
    if (hashedEval != VALUE_NONE) {
        return (pos->side == WHITE ? hashedEval : -hashedEval);
    }

    int nn_score = nnue_eval(pos);
    int white_relative = (pos->side == WHITE) ? nn_score : -nn_score;
    StoreTTEval(pos, white_relative);
    return nn_score;
}
