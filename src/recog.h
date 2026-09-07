
#ifndef RECOG_H
#define RECOG_H

#include "defs.h"
#include "board.h"
#include "bitboards.h"

INLINE int is_repetition(const S_BOARD *pos) {
    return pos->st->repetition && pos->st->repetition < pos->ply;
}

INLINE int drawFiftyMoveRule(const S_BOARD *pos) {
    return pos->useFiftyMoveRule ? pos->st->fiftyMove > 99 : FALSE;
}

INLINE int drawByMaterial(const S_BOARD *pos) {
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

INLINE int recog_draw(const S_BOARD *pos) {
    ASSERT(checkBoard(pos));
    return drawFiftyMoveRule(pos) ||
           is_repetition(pos) ||
           drawByMaterial(pos);
}

extern int drawRepetition(const S_BOARD *pos);
extern int drawByRepetitionEthereals(const S_BOARD *pos);

#endif // RECOG_H
