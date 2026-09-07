
#include "defs.h"
#include "recog.h"
#include "stdio.h"
#include "bitboards.h"

int drawFiftyMoveRule(const S_BOARD *pos){
    return pos->useFiftyMoveRule ? pos->st->fiftyMove > 99 : FALSE;
}

int drawRepetition(const S_BOARD *pos){
    int end = pos->st->fiftyMove;
    if (end > pos->hisPly) end = pos->hisPly;
    const StateInfo *stp = pos->st->previous;
    for (int i = 1; i <= end && stp; ++i, stp = stp->previous) {
        if (pos->st->posKey == stp->posKey) {
            return TRUE;
        }
    }

    return FALSE;
}

int drawByRepetitionEthereals(const S_BOARD *pos){
    int reps = 0;
    int end = pos->st->fiftyMove;
    if (end > pos->hisPly) end = pos->hisPly;
    if (end < 2 || !pos->st->previous || !pos->st->previous->previous) return 0;

    const StateInfo *stp = pos->st->previous->previous;
    for (int i = 2; i <= end; i += 2) {
        if (stp->posKey == pos->st->posKey && (i <= pos->ply || ++reps == 2))
            return 1;

        if (!stp->previous || !stp->previous->previous) break;
        stp = stp->previous->previous;
    }

    return 0;
}

int drawByMaterial(const S_BOARD *pos){
    U64 pawns   = pos->byTypeBB[PAWN];
    U64 rooks   = pos->byTypeBB[ROOK];
    U64 queens  = pos->byTypeBB[QUEEN];
    U64 bishops = pos->byTypeBB[BISHOP];
    U64 knights = pos->byTypeBB[KNIGHT];

    return !(pawns | rooks | queens)
        && (!several(pos->byColorBB[WHITE]) || !several(pos->byColorBB[BLACK]))
        && (!several(knights | bishops)
            || (!bishops && COUNTBIT(knights) <= 2));
}

int recog_draw(const S_BOARD *pos){

    ASSERT(checkBoard(pos));

    return  drawFiftyMoveRule(pos)   ||
            drawByRepetitionEthereals(pos)      ||
            drawByMaterial(pos);
}
