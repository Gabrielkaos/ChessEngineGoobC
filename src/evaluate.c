#include "stdio.h"
#include "stdlib.h"
#include "defs.h"
#include "board.h"
#include "bitboards.h"
#include "evaluate.h"
#include "some_maths.h"
#include "nnue_loader.h"

int DistanceBetween[64][64];

void initDistancesForEval(void) {
    for (int sq1 = 0; sq1 < 64; sq1++) {
        for (int sq2 = 0; sq2 < 64; sq2++) {
            DistanceBetween[sq1][sq2] = MAX(abs(FILE_OF(sq1) - FILE_OF(sq2)),
                                            abs(RANK_OF(sq1) - RANK_OF(sq2)));
        }
    }
}


