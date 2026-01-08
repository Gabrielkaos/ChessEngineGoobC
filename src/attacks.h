#ifndef ATTACKS_H
#define ATTACKS_H

#include "defs.h"
#include "board.h"

//attacks.c
extern U64 pawnRightAttacks(U64 pawns, U64 targets, int colour);
extern U64 pawnLeftAttacks(U64 pawns, U64 targets, int colour);
extern U64 pawnAttackSpan(U64 pawns, U64 targets, int colour);
extern U64 pawnAttackDouble(U64 pawns, U64 targets, int colour);
extern U64 discoveredAttacks(S_BOARD *pos, int sq, int US);
extern U64 attackersToKingSq(const S_BOARD *pos,int side);
extern U64 pawnAttacks(int color,int sq);
extern U64 allAttackersToSquare(const S_BOARD *pos, U64 occupied, int sq);
extern U64 get_rook_attacks(int square,U64 occupancy);
extern U64 get_bishop_attacks(int square,U64 occupancy);
extern U64 get_queen_attacks(int square,U64 occupancy);
extern void InitAttacks();
extern int is_square_attacked_BB(const int square, const int side,const S_BOARD *pos);

#endif