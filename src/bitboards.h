
#ifndef BIT_H
#define BIT_H

#include "stdbool.h"
#include "defs.h"
#include "board.h"

extern const U64 NOT_AB_FILE;
extern const U64 NOT_HG_FILE;
extern const U64 NOT_A_FILE;
extern const U64 NOT_H_FILE;
extern const U64 kingsideBB;
extern const U64 queensideBB;
extern const U64 darksquaresBB;
extern const U64 lightsquaresBB;
extern const U64 LONG_DIAGONALS;
extern const U64 CENTER_SQUARES;
extern const U64 CENTER_BIG;
extern const U64 KingAreasMask[BOTH][BOARD_NUMS_SQ];
extern const U64 PawnConnectedMasks[BOTH][BOARD_NUMS_SQ];
extern const U64 OutpostRanksMasks[BOTH];
extern const U64 ForwardRanksMasks[BOTH][RANK_NONE];
extern const U64 ForwardFileMasks[BOTH][BOARD_NUMS_SQ];
extern const U64 OutpostSquareMasks[BOTH][BOARD_NUMS_SQ];
extern const U64 ClearMask[BOARD_NUMS_SQ];
extern const U64 FileBBMask[FILE_NONE];
extern const U64 RankBBMask[RANK_NONE];
extern const U64 BlackPassedMask[BOARD_NUMS_SQ];
extern const U64 WhitePassedMark[BOARD_NUMS_SQ];
extern const U64 IsolatedMask[BOARD_NUMS_SQ];
extern const int PromoteSquare[BOTH][FILE_NONE];

extern int boardHasNonPawnMaterial(S_BOARD *pos, int turn);
extern int kingPawnFileDistance(U64 pawns, int ksq);
extern int openFileCount(U64 pawns);

static inline int poplsb(U64 *bb) {
    int lsb = LSBINDEX(*bb);
    *bb &= *bb - 1;
    return lsb;
}

static inline bool several(U64 bb) {
    return (bb & (bb - 1)) != 0;
}

static inline bool onlyOne(U64 bb) {
    return bb && !several(bb);
}

static inline bool testBit(U64 bb, int i) {
    return (bb & (1ULL << i)) != 0;
}

static inline int getmsb(U64 bb) {
    return __builtin_clzll(bb) ^ 63;
}

static inline int mirrorFile(int file) {
    return file < 4 ? file : 7 - file;
}

static inline int relativeRankOf(int colour, int sq) {
    return (sq >> 3) ^ (colour * 7);
}

static inline int relativeSquare32(int colour, int sq) {
    return 4 * relativeRankOf(colour, sq) + mirrorFile(FILE_OF(sq));
}

static inline int backmost(int colour, U64 bb) {
    return colour == WHITE ? LSBINDEX(bb) : getmsb(bb);
}

static inline U64 pawnAdvance(U64 pawns, U64 occupied, int colour) {
    return ~occupied & (colour == WHITE ? (pawns << 8) : (pawns >> 8));
}

static inline U64 squaresOfMatchingColour(int sq) {
    return testBit(lightsquaresBB, sq) ? lightsquaresBB : darksquaresBB;
}

static inline U64 pawnPassedMark(int color, int sq) {
    return color == WHITE ? WhitePassedMark[sq] : BlackPassedMask[sq];
}

static inline U64 getOutpostSquareMasks(int color, int sq) {
    return OutpostSquareMasks[color][sq];
}

static inline U64 getOutpostRanksMasks(int color) {
    return OutpostRanksMasks[color];
}

static inline U64 forwardRanksMasks(int color, int rank) {
    return ForwardRanksMasks[color][rank];
}

static inline U64 KingAreaMasks(int color, int sq) {
    return KingAreasMask[color][sq];
}

#define CLRBIT(bb,sq) (bb &= ClearMask[(sq)])

#endif // BIT_H

