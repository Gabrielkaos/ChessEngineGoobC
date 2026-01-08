

#ifndef EVAL_H
#define EVAL_H

#include "stdint.h"
#include "defs.h"
#include "board.h"


//Evaluation info structure
typedef struct{
    int kingSq[2]; //stores king squares
    int pkSafety[2]; //stores safety score of pawn king
    U64 attackedByBishops[2]; //bitboards attacked by bishops
    U64 attackedByKnights[2]; //bitboards attacked by knights
    U64 occupiedMinusBishops[2]; //occupancy except bishop
    U64 occupiedMinusRooks[2]; //occupancy except rook
    U64 mobilityAreas[2]; //mobility
    int attWeight[2]; //for attackers
    int attCnt[2]; //how many attackers
    int kingAttacksCount[2]; //how many attacks on king
    U64 passers[2]; //passer pawn
    int pawnEval[2]; //pawn eval
    U64 attacks_array_minors[2]; //attacks of minors bitboard
    U64 attacks_array_rooks[2]; //attacks of rooks bitboards
    U64 attacks_array_queens[2]; //attacks of queens bitboards
    U64 attacks_array_pawns[2]; //pawn attacks
    U64 rammedPawns[2]; //rammed pawns
    U64 attackedBy2[2]; //stores bitboards attacked by two piece
    U64 attacked[2]; //squares attacked
    U64 kingAreas[2]; //areas surrounding king
    U64 pawnAttackedBy2[2]; //squares attacked by 2 pawns
} EVAL_INFO;

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
