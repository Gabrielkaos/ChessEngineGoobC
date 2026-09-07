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

#include "bitboards.h"

INLINE int is_square_attacked_occ(const int square, const int side, const S_BOARD *state, U64 occ) {
    ASSERT(SqOnBoard(square));
    ASSERT(SideValid(side));

    const U64 colorBB = state->byColorBB[side];

    if (pawn_attacks[side ^ 1][square] & state->byTypeBB[PAWN] & colorBB) return 1;

    if (knight_attacks[square] & state->byTypeBB[KNIGHT] & colorBB) return 1;

    const U64 diag = (state->byTypeBB[BISHOP] | state->byTypeBB[QUEEN]) & colorBB;
    if (diag && (get_bishop_attacks(square, occ) & diag)) return 1;

    const U64 orth = (state->byTypeBB[ROOK] | state->byTypeBB[QUEEN]) & colorBB;
    if (orth && (get_rook_attacks(square, occ) & orth)) return 1;

    if (king_attacks[square] & state->byTypeBB[KING] & colorBB) return 1;

    return 0;
}

INLINE int is_square_attacked_BB(const int square, const int side, const S_BOARD *state) {
    ASSERT(SqOnBoard(square));
    ASSERT(SideValid(side));
    ASSERT(checkBoard(state));

    return is_square_attacked_occ(square, side, state, state->byTypeBB[ALL_PIECES]);
}

INLINE U64 pawnLeftAttacks(U64 pawns, U64 targets, int colour) {
    ASSERT(SideValid(colour));
    return targets & (colour == WHITE ? (pawns << 7) & ~FileBBMask[FILE_H]
                                      : (pawns >> 7) & ~FileBBMask[FILE_A]);
}

INLINE U64 pawnRightAttacks(U64 pawns, U64 targets, int colour) {
    ASSERT(SideValid(colour));
    return targets & (colour == WHITE ? (pawns << 9) & ~FileBBMask[FILE_A]
                                      : (pawns >> 9) & ~FileBBMask[FILE_H]);
}

INLINE U64 pawnAttackSpan(U64 pawns, U64 targets, int colour) {
    ASSERT(SideValid(colour));
    return pawnLeftAttacks(pawns, targets, colour)
        | pawnRightAttacks(pawns, targets, colour);
}

INLINE U64 pawnAttackDouble(U64 pawns, U64 targets, int colour) {
    ASSERT(SideValid(colour));
    return pawnLeftAttacks(pawns, targets, colour)
        & pawnRightAttacks(pawns, targets, colour);
}

INLINE U64 pawnAttacks(int color, int sq) {
    ASSERT(SideValid(color));
    ASSERT(SqOnBoard(sq));
    return color == WHITE ? pawn_attacks[WHITE][sq] : pawn_attacks[BLACK][sq];
}

INLINE U64 attackersToKingSq(const S_BOARD *pos, int side) {
    ASSERT(SideValid(side));

    U64 kbb = pos->byColorBB[side] & pos->byTypeBB[KING];
    ASSERT(kbb);
    if (!kbb) return 0ULL;

    int ksq = LSBINDEX(kbb);
    U64 occ = pos->byTypeBB[ALL_PIECES];
    int them = side ^ 1;
    U64 enemyBB = pos->byColorBB[them];

    return (pawn_attacks[side][ksq] & enemyBB & pos->byTypeBB[PAWN])
         | (knight_attacks[ksq]      & enemyBB & pos->byTypeBB[KNIGHT])
         | (get_bishop_attacks(ksq, occ) & enemyBB & (pos->byTypeBB[BISHOP] | pos->byTypeBB[QUEEN]))
         | (get_rook_attacks(ksq, occ)   & enemyBB & (pos->byTypeBB[ROOK] | pos->byTypeBB[QUEEN]))
         | (king_attacks[ksq]         & enemyBB & pos->byTypeBB[KING]);
}

extern U64 discoveredAttacks(S_BOARD *pos, int sq, int US);
extern U64 allAttackersToSquare(const S_BOARD *pos, U64 occupied, int sq);
extern U64 allAttackedSquares(const S_BOARD *pos, int side);
extern void InitAttacks();

#endif