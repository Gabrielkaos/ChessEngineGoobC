#ifndef CORRECTION_TYPES_H
#define CORRECTION_TYPES_H

#include <stdint.h>

#define CORR_HIST_SIZE   16384
#define CORR_HIST_MAX     16384
#define CORR_HIST_SCALE     256

// pawn structure key -> correction, per side to move
typedef int16_t PawnCorrectionTable[2][CORR_HIST_SIZE];

// Stockfish-style non-pawn correction: one table per material color
// (white pieces / black pieces), each indexed by that color's non-pawn
// material key, applied to the side to move
typedef int16_t NonPawnCorrectionTable[2][2][CORR_HIST_SIZE];

// all knights + bishops (both colors) -> correction, per side to move
typedef int16_t MinorCorrectionTable[2][CORR_HIST_SIZE];

// continuation correction history tuning, scaled to this engine's
// CORR_HIST_SCALE normalization from Stockfish's weights:
//   read weight 8761/15341 of the pawn term   -> CONT_CORR_WEIGHT
//   no-move fallback 64049/131072 cp          -> CONT_CORR_NOMOVE
//   initial fill 5 * CORR_HIST_MAX/1024       -> CONT_CORR_INIT_FILL
#define CONT_CORR_WEIGHT     73
#define CONT_CORR_NOMOVE  16000
#define CONT_CORR_INIT_FILL  80
// per-table update weights from update_correction_history():
// ss-2 table gets bonus*130/128, ss-4 table gets bonus*70/128
#define CONT_CORR_UP_SS2_W  130
#define CONT_CORR_UP_SS4_W   70

#endif