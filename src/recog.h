
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

extern int DistanceBetween[64][64];

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
    int weakKingSq = LSBINDEX(pos->byColorBB[weakColor]);
    return DistanceBetween[weakKingSq][promSq] <= 1;
}

INLINE int recog_draw(const S_BOARD *pos) {
    ASSERT(checkBoard(pos));
    return drawFiftyMoveRule(pos) ||
           is_repetition(pos) ||
           drawByMaterial(pos) ||
           drawKBPRP(pos);
}

extern int drawRepetition(const S_BOARD *pos);
extern int drawByRepetitionEthereals(const S_BOARD *pos);

#endif // RECOG_H
