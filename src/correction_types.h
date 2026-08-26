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

#endif