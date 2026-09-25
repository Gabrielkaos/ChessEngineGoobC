#ifndef BIT_H
#define BIT_H

#include "stdbool.h"
#include "defs.h"
#include "board.h"

extern const U64 NOT_AB_FILE;
extern const U64 NOT_HG_FILE;
extern const U64 NOT_A_FILE;
extern const U64 NOT_H_FILE;
extern const U64 darksquaresBB;
extern const U64 lightsquaresBB;
extern const U64 FileBBMask[FILE_NONE];
extern const U64 RankBBMask[RANK_NONE];

INLINE int boardHasNonPawnMaterial(const S_BOARD *pos, int turn) {
    ASSERT(SideValid(turn));
    return (pos->byColorBB[turn] & ~(pos->byTypeBB[KING] | pos->byTypeBB[PAWN])) != 0;
}

INLINE int poplsb(U64 *bb) {
    int lsb = LSBINDEX(*bb);
    *bb &= *bb - 1;
    return lsb;
}

INLINE bool several(U64 bb) {
    return (bb & (bb - 1)) != 0;
}

INLINE bool onlyOne(U64 bb) {
    return bb && !several(bb);
}

INLINE bool testBit(U64 bb, int i) {
    return (bb & (1ULL << i)) != 0;
}

INLINE U64 squaresOfMatchingColour(int sq) {
    return testBit(lightsquaresBB, sq) ? lightsquaresBB : darksquaresBB;
}

#endif // BIT_H
