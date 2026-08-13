#ifndef CORRECTION_TYPES_H
#define CORRECTION_TYPES_H

#include <stdint.h>

#define CORR_HIST_SIZE   16384
#define CORR_HIST_MAX     16384
#define CORR_HIST_SCALE     256

typedef int16_t PawnCorrectionTable[2][CORR_HIST_SIZE];

#endif