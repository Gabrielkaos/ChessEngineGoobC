
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
extern const int KingPawnFileDistance[FILE_NONE][1<<FILE_NONE];

INLINE int boardHasNonPawnMaterial(const S_BOARD *pos, int turn) {
    ASSERT(SideValid(turn));
    return (pos->byColorBB[turn] & ~(pos->byTypeBB[KING] | pos->byTypeBB[PAWN])) != 0;
}

INLINE int openFileCount(U64 pawns) {
    pawns |= pawns >> 8; pawns |= pawns >> 16; pawns |= pawns >> 32;
    return COUNTBIT(~pawns & 0xFF);
}

INLINE int kingPawnFileDistance(U64 pawns, int ksq) {
    ASSERT(SqOnBoard(ksq));
    pawns |= pawns >> 8; pawns |= pawns >> 16; pawns |= pawns >> 32;
    return KingPawnFileDistance[FILE_OF(ksq)][pawns & 0xFF];
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

INLINE int getmsb(U64 bb) {
    return __builtin_clzll(bb) ^ 63;
}

INLINE int mirrorFile(int file) {
    return file < 4 ? file : 7 - file;
}

INLINE int relativeRankOf(int colour, int sq) {
    return (sq >> 3) ^ (colour * 7);
}

INLINE int relativeSquare32(int colour, int sq) {
    return 4 * relativeRankOf(colour, sq) + mirrorFile(FILE_OF(sq));
}

INLINE int backmost(int colour, U64 bb) {
    return colour == WHITE ? LSBINDEX(bb) : getmsb(bb);
}

INLINE U64 pawnAdvance(U64 pawns, U64 occupied, int colour) {
    return ~occupied & (colour == WHITE ? (pawns << 8) : (pawns >> 8));
}

INLINE U64 squaresOfMatchingColour(int sq) {
    return testBit(lightsquaresBB, sq) ? lightsquaresBB : darksquaresBB;
}

INLINE U64 pawnPassedMark(int color, int sq) {
    return color == WHITE ? WhitePassedMark[sq] : BlackPassedMask[sq];
}

INLINE U64 getOutpostSquareMasks(int color, int sq) {
    return OutpostSquareMasks[color][sq];
}

INLINE U64 getOutpostRanksMasks(int color) {
    return OutpostRanksMasks[color];
}

INLINE U64 forwardRanksMasks(int color, int rank) {
    return ForwardRanksMasks[color][rank];
}

INLINE U64 KingAreaMasks(int color, int sq) {
    return KingAreasMask[color][sq];
}

#define CLRBIT(bb,sq) (bb &= ClearMask[(sq)])

#endif // BIT_H

