#ifndef MAKEMOVE_H
#define MAKEMOVE_H

#include "board.h"


#include "bitboards.h"
#include "attacks.h"

// Fast inlined tactical test
INLINE int moveIsTactical(const S_BOARD *pos, int move) {
    return (pos->pieces[TOSQ(move)] != EMPTY && !(move & MVFLAGCA)) ||
           (move & MVFLAGEP) || (PROMOTED(move) != 0);
}

// Fast inlined estimated capture value for SEE threshold test
INLINE int moveEstimatedValue(const S_BOARD *pos, int move) {
    int value = SEEPieceValues[pos->pieces[TOSQ(move)]];
    if (PROMOTED(move))
        value += SEEPieceValues[PROMOTED(move)] - SEEPieceValues[wP];
    else if (move & MVFLAGEP)
        value = SEEPieceValues[wP];
    else if (move & MVFLAGCA)
        value = 0;
    return value;
}

// Fast inlined upper-bound capture value for ProbCut / Delta pruning
INLINE int MoveBestCaseValue(const S_BOARD *pos) {
    U64 enemy = pos->byColorBB[!pos->side];
    int value = SEEPieceValues[wP];

    if (pos->byTypeBB[QUEEN] & enemy) {
        value = SEEPieceValues[wQ];
    } else if (pos->byTypeBB[ROOK] & enemy) {
        value = SEEPieceValues[wR];
    } else if ((pos->byTypeBB[BISHOP] | pos->byTypeBB[KNIGHT]) & enemy) {
        value = SEEPieceValues[wB];
    }

    U64 pawns = pos->byTypeBB[PAWN] & pos->byColorBB[pos->side];
    if (pawns & (pos->side == WHITE ? RankBBMask[RANK_7] : RankBBMask[RANK_2])) {
        value += SEEPieceValues[wQ] - SEEPieceValues[wP];
    }

    return value;
}

// Fast inlined legality validator with non-pinned fast path
INLINE int legal(const S_BOARD *pos, int move) {
    int us = pos->side;
    int from = FROMSQ(move);

    // Fast path: ordinary moves (not EP, not castling) when not in check
    if (__builtin_expect(!(move & (MVFLAGEP | MVFLAGCA)), 1)) {
        if (!pos->st->checkersBB) {
            if (!(pos->st->blockersForKing[us] & (1ULL << from))) {
                U64 kbb = pos->byColorBB[us] & pos->byTypeBB[KING];
                if (from != LSBINDEX(kbb)) return TRUE;
            }
        }
    }

    int them = us ^ 1;
    int to = TOSQ(move);
    U64 kbb = pos->byColorBB[us] & pos->byTypeBB[KING];
    int ksq = LSBINDEX(kbb);

    // En passant capture
    if (move & MVFLAGEP) {
        int capsq = (us == WHITE) ? (to - 8) : (to + 8);
        if (pos->st->checkersBB) {
            if (pos->st->checkersBB != (1ULL << capsq)) return FALSE;
        }
        U64 occ = (pos->byTypeBB[ALL_PIECES] ^ (1ULL << from) ^ (1ULL << capsq)) | (1ULL << to);
        U64 enemySliders = pos->byColorBB[them];
        if (get_rook_attacks(ksq, occ) & enemySliders & (pos->byTypeBB[ROOK] | pos->byTypeBB[QUEEN]))
            return FALSE;
        if (get_bishop_attacks(ksq, occ) & enemySliders & (pos->byTypeBB[BISHOP] | pos->byTypeBB[QUEEN]))
            return FALSE;
        return TRUE;
    }

    // Castling moves
    if (move & MVFLAGCA) {
        if (pos->st->checkersBB) return FALSE;
        if (us == WHITE) {
            if (to == G1) {
                if (is_square_attacked_BB(F1, BLACK, pos) || is_square_attacked_BB(G1, BLACK, pos))
                    return FALSE;
            } else if (to == C1) {
                if (is_square_attacked_BB(D1, BLACK, pos) || is_square_attacked_BB(C1, BLACK, pos))
                    return FALSE;
            }
        } else {
            if (to == G8) {
                if (is_square_attacked_BB(F8, WHITE, pos) || is_square_attacked_BB(G8, WHITE, pos))
                    return FALSE;
            } else if (to == C8) {
                if (is_square_attacked_BB(D8, WHITE, pos) || is_square_attacked_BB(C8, WHITE, pos))
                    return FALSE;
            }
        }
        return TRUE;
    }

    // King moves (only our king stands on ksq)
    if (from == ksq) {
        U64 occ = pos->byTypeBB[ALL_PIECES] ^ (1ULL << from);
        return !is_square_attacked_occ(to, them, pos, occ);
    }

    // In check: non-king moves
    if (pos->st->checkersBB) {
        if (pos->st->checkersBB & (pos->st->checkersBB - 1))
            return FALSE;

        int checkerSq = LSBINDEX(pos->st->checkersBB);
        U64 targetMask = (1ULL << checkerSq) | BetweenBB[ksq][checkerSq];
        if (!((1ULL << to) & targetMask))
            return FALSE;

        if (pos->st->blockersForKing[us] & (1ULL << from)) {
            return (LineBB[from][to] & (1ULL << ksq)) != 0;
        }
        return TRUE;
    }

    // Not in check: non-king moves
    return !(pos->st->blockersForKing[us] & (1ULL << from)) || ((LineBB[from][to] & (1ULL << ksq)) != 0);
}

// makemove.c exports
extern void makeMove(S_BOARD *pos, int move, StateInfo *newSt);
extern void takeMove(S_BOARD *pos);
extern void takeNullMove(S_BOARD *pos);
extern void makeNullMove(S_BOARD *pos, StateInfo *newSt);
extern int  MoveExists(S_BOARD *pos, const int move);


#endif