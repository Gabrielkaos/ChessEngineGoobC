#ifndef ATTACKS_H
#define ATTACKS_H

#include "defs.h"
#include "board.h"

#if defined(USE_PEXT) && defined(__BMI2__)
#include <immintrin.h>
#define PEXT_ATTACKS 1
#endif

#define ROOK_TABLE_SIZE 102400
#define BISHOP_TABLE_SIZE 5248

extern U64 rook_attacks_table[ROOK_TABLE_SIZE];
extern U64 bishop_attacks_table[BISHOP_TABLE_SIZE];
extern int rook_offset[BOARD_NUMS_SQ];
extern int bishop_offset[BOARD_NUMS_SQ];
extern U64 bishop_masks[BOARD_NUMS_SQ];
extern U64 rook_masks[BOARD_NUMS_SQ];
extern const U64 rook_magic_numbers[BOARD_NUMS_SQ];
extern const U64 bishop_magic_numbers[BOARD_NUMS_SQ];
extern const int bishop_relevant_bits[BOARD_NUMS_SQ];
extern const int rook_relevant_bits[BOARD_NUMS_SQ];

INLINE U64 get_bishop_attacks(int square, U64 occupancy){
    ASSERT(square >= 0 && square < BOARD_NUMS_SQ);
#ifdef PEXT_ATTACKS
    return bishop_attacks_table[bishop_offset[square] + _pext_u64(occupancy, bishop_masks[square])];
#else
    occupancy &= bishop_masks[square];
    occupancy *= bishop_magic_numbers[square];
    occupancy >>= 64 - bishop_relevant_bits[square];
    return bishop_attacks_table[bishop_offset[square] + occupancy];
#endif
}

INLINE U64 get_rook_attacks(int square, U64 occupancy){
    ASSERT(square >= 0 && square < BOARD_NUMS_SQ);
#ifdef PEXT_ATTACKS
    return rook_attacks_table[rook_offset[square] + _pext_u64(occupancy, rook_masks[square])];
#else
    occupancy &= rook_masks[square];
    occupancy *= rook_magic_numbers[square];
    occupancy >>= 64 - rook_relevant_bits[square];
    return rook_attacks_table[rook_offset[square] + occupancy];
#endif
}

INLINE U64 get_queen_attacks(int square, U64 occupancy){
    return get_bishop_attacks(square, occupancy) | get_rook_attacks(square, occupancy);
}

extern U64 LineBB[BOARD_NUMS_SQ][BOARD_NUMS_SQ];
extern U64 BetweenBB[BOARD_NUMS_SQ][BOARD_NUMS_SQ];

//attacks.c
extern U64 pawnRightAttacks(U64 pawns, U64 targets, int colour);
extern U64 pawnLeftAttacks(U64 pawns, U64 targets, int colour);
extern U64 pawnAttackSpan(U64 pawns, U64 targets, int colour);
extern U64 pawnAttackDouble(U64 pawns, U64 targets, int colour);
extern U64 discoveredAttacks(S_BOARD *pos, int sq, int US);
extern U64 attackersToKingSq(const S_BOARD *pos,int side);
extern U64 pawnAttacks(int color,int sq);
extern U64 allAttackersToSquare(const S_BOARD *pos, U64 occupied, int sq);
extern U64 allAttackedSquares(const S_BOARD *pos, int side);
extern void InitAttacks();
extern int is_square_attacked_BB(const int square, const int side,const S_BOARD *pos);
extern int is_square_attacked_occ(const int square, const int side, const S_BOARD *state, U64 occ);

#endif