
#ifndef EVAL_H
#define EVAL_H

#include "stdint.h"
#include "defs.h"

enum {
    SCALE_DRAW             =   0,
    SCALE_OCB_BISHOPS_ONLY =  64,
    SCALE_OCB_ONE_KNIGHT   = 106,
    SCALE_OCB_ONE_ROOK     =  96,
    SCALE_LONE_QUEEN       =  88,
    SCALE_NORMAL           = 128,
    SCALE_LARGE_PAWN_ADV   = 144,
};

#define MakeScore(mg, eg) ((int)((unsigned int)(eg) << 16) + (mg))
#define S(mg, eg) (MakeScore((mg), (eg)))
#define ScoreMG(s) ((int16_t)((uint16_t)((unsigned)((s)))))
#define ScoreEG(s) ((int16_t)((uint16_t)((unsigned)((s) + 0x8000) >> 16)))

//VARIABLES
#define PHASE_OPENING 0
#define PHASE_MIDDLE 43
#define PHASE_ENDING 171
#define PHASE_PAWN_ENDING 256
extern const int tempo;
extern int DistanceBetween[64][64];
extern int PSQTMATTABLE[13][64];

//FUNCTIONS
extern void initPQSTMAT();
extern void initDistancesForEval();
//extern int RewardForOppKingDistanceFromCenter(int oppKing,int friendKing);
extern int EvalPosition(S_BOARD *pos);

#endif // EVAL_H
