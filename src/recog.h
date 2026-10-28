
#ifndef RECOG_H
#define RECOG_H

#include "defs.h"
#include "board.h"
#include "bitboards.h"
#include "init.h"
#include "attacks.h"

INLINE int is_repetition(const S_BOARD *pos) {
    return pos->st->repetition && pos->st->repetition < pos->ply;
}

// Marcel van Kervinck's Cuckoo upcoming repetition detection (Stockfish / Berserk):
// Checks in O(1) per reversible ply whether a single pseudo-legal reversible move
// from the current board repeats an earlier position.
INLINE int upcoming_repetition(const S_BOARD *pos) {
    int end = MIN(pos->st->fiftyMove, pos->st->pliesFromNull);
    if (end < 3) return FALSE;

    U64 originalKey = pos->st->posKey;
    const StateInfo *stp = pos->st->previous;
    if (!stp) return FALSE;
    U64 other = originalKey ^ stp->posKey ^ sideKey;

    for (int i = 3; i <= end; i += 2) {
        if (!stp->previous || !stp->previous->previous) break;
        stp = stp->previous;
        other ^= stp->posKey ^ stp->previous->posKey ^ sideKey;
        stp = stp->previous;

        if (other != 0ULL) continue;

        U64 moveKey = originalKey ^ stp->posKey;
        int j;
        if ((j = (int)(moveKey & 0x1fff), cuckoo[j] == moveKey) ||
            (j = (int)((moveKey >> 16) & 0x1fff), cuckoo[j] == moveKey)) {
            int move = cuckooMove[j];
            int s1 = FROMSQ(move);
            int s2 = TOSQ(move);

            if (!(BetweenBB[s1][s2] & pos->byTypeBB[ALL_PIECES])) {
                if (pos->ply > i || stp->repetition)
                    return TRUE;
            }
        }
    }
    return FALSE;
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

INLINE int drawKBPRP(const S_BOARD *pos) {
    if (pos->byTypeBB[QUEEN] | pos->byTypeBB[ROOK] | pos->byTypeBB[KNIGHT])
        return FALSE;

    U64 pawns = pos->byTypeBB[PAWN];
    if (!pawns) return FALSE;

    // Check if pawns are only on A file or only on H file
    if ((pawns & ~FileBBMask[FILE_A]) && (pawns & ~FileBBMask[FILE_H]))
        return FALSE;

    int strongColor = (pos->byColorBB[WHITE] & pawns) ? WHITE : BLACK;
    int weakColor = !strongColor;

    // Weak side must have only the King (no pawns or pieces)
    if (pos->byColorBB[weakColor] & ~pos->byTypeBB[KING])
        return FALSE;

    // Strong side must not have any non-king pieces other than a bishop
    U64 strongPieces = pos->byColorBB[strongColor] & ~pos->byTypeBB[PAWN] & ~pos->byTypeBB[KING];
    U64 bishops = pos->byTypeBB[BISHOP];

    if (strongPieces != bishops)
        return FALSE;

    int isAFile = (pawns & FileBBMask[FILE_A]) != 0;
    int promSq = isAFile ? (strongColor == WHITE ? A8 : A1) : (strongColor == WHITE ? H8 : H1);

    if (bishops) {
        if (!onlyOne(bishops)) return FALSE;
        int bishopSq = LSBINDEX(bishops);
        if (squaresOfMatchingColour(bishopSq) & (1ULL << promSq))
            return FALSE; // Right-colored bishop can win
    }

    // Defending king on promSq or adjacent (corner fortress reached)
    return (pos->byColorBB[weakColor] & (king_attacks[promSq] | (1ULL << promSq))) != 0;
}

INLINE int recog_draw(const S_BOARD *pos) {
    ASSERT(checkBoard(pos));
    if (drawFiftyMoveRule(pos) || is_repetition(pos)) return TRUE;
    if (__builtin_expect(!(pos->byTypeBB[QUEEN] | pos->byTypeBB[ROOK]), 0)) {
        return drawByMaterial(pos) || drawKBPRP(pos);
    }
    return FALSE;
}

#endif // RECOG_H
