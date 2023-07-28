
#ifndef BIT_H
#define BIT_H

#include "stdbool.h"
#include "defs.h"

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
extern U64 pawnPassedMark(int color,int sq);
extern U64 getOutpostSquareMasks(int color,int sq);
extern U64 getOutpostRanksMasks(int color);
extern int relativeRankOf(int colour, int sq);
extern int mirrorFile(int file);
extern int kingPawnFileDistance(U64 pawns, int ksq);
extern int getmsb(U64 bb);
extern int relativeSquare32(int colour, int sq);
extern U64 KingAreaMasks(int color,int sq);
extern bool several(U64 bb);
extern U64 pawnAdvance(U64 pawns, U64 occupied, int colour);
extern int openFileCount(U64 pawns);
extern bool testBit(U64 bb, int i);
extern U64 squaresOfMatchingColour(int sq);
extern bool onlyOne(U64 bb);
extern int poplsb(U64 *bb);
extern U64 forwardRanksMasks(int color,int rank);
extern int backmost(int colour, U64 bb);

#define CLRBIT(bb,sq) (bb &= ClearMask[(sq)])

#endif // BIT_H
