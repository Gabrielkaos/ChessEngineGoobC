#include "stdio.h"
#include "search.h"
#include "bitboards.h"
#include "evaluate.h"
#include "movegen.h"
#include "makemove.h"
#include "init.h"
#include "attacks.h"
#include "validate.h"
#include "nnue_loader.h"

#define HASH_PCE(pce,sq) (pos->st->posKey ^= (pieceKeys[(pce)][(sq)]))
#define HASH_SIDE (pos->st->posKey ^= (sideKey))
#define HASH_CA (pos->st->posKey ^= (castleKeys[(pos->st->castleRights)]))
#define HASH_EP (pos->st->posKey ^= (pieceKeys[EMPTY][pos->st->enPas]))
#define HASH_PK(pce,sq) (pos->st->pkHash^=(pieceKeys[(pce)][(sq)]))
#define HASH_NP(pce,sq,col) (pos->st->npHash[(col)]^=(pieceKeys[(pce)][(sq)]))
#define HASH_MINOR(pce,sq) (pos->st->minorHash^=(pieceKeys[(pce)][(sq)]))

const int castlePerm[64]={
    13,15,15,15,12,15,15,14,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
     7,15,15,15, 3,15,15,11
};

int moveIsTactical(S_BOARD *pos,int move){
    return (pos->pieces[TOSQ(move)] != EMPTY && (move & MVFLAGCA)==0) ||
            (move & MVFLAGEP || PROMOTED(move) != 0);
}

int MoveBestCaseValue(S_BOARD *pos){
    ASSERT(checkBoard(pos));

    U64 enemy = pos->byColorBB[!pos->side];
    int value = SEEPieceValues[wP];

    if (pos->byTypeBB[QUEEN] & enemy){
        value = SEEPieceValues[wQ];
    } else if (pos->byTypeBB[ROOK] & enemy){
        value = SEEPieceValues[wR];
    } else if ((pos->byTypeBB[BISHOP] | pos->byTypeBB[KNIGHT]) & enemy){
        value = SEEPieceValues[wB];
    }

    U64 pawns = pos->byTypeBB[PAWN] & pos->byColorBB[pos->side];
    if(pawns & (pos->side==WHITE ? RankBBMask[RANK_7]:RankBBMask[RANK_2])){
        value += SEEPieceValues[wQ] - SEEPieceValues[wP];
    }

    return value;
}

int moveEstimatedValue(S_BOARD *pos, int move) {
    ASSERT(moveValid(move));

    int value = SEEPieceValues[pos->pieces[TOSQ(move)]];

    if (PROMOTED(move))
        value += SEEPieceValues[PROMOTED(move)] - SEEPieceValues[wP];
    else if (move & MVFLAGEP)
        value = SEEPieceValues[wP];
    else if (move & MVFLAGCA)
        value = 0;

    return value;
}

INLINE void putPiece(S_BOARD *pos, int pce, int sq) {
    ASSERT(SqOnBoard(sq));
    ASSERT(PieceValidEmpty(pce));
    int col = COLOR_OF(pce);
    int pt = TYPE_OF(pce);
    U64 mask = 1ULL << sq;
    pos->pieces[sq] = pce;
    pos->byColorBB[col] |= mask;
    pos->byTypeBB[pt] |= mask;
    pos->byTypeBB[ALL_PIECES] |= mask;
}

INLINE void removePiece(S_BOARD *pos, int sq) {
    ASSERT(SqOnBoard(sq));
    int pce = pos->pieces[sq];
    ASSERT(PieceValidEmpty(pce));
    int col = COLOR_OF(pce);
    int pt = TYPE_OF(pce);
    U64 mask = 1ULL << sq;
    pos->pieces[sq] = EMPTY;
    pos->byColorBB[col] ^= mask;
    pos->byTypeBB[pt] ^= mask;
    pos->byTypeBB[ALL_PIECES] ^= mask;
}

INLINE void movePiece(S_BOARD *pos, int from, int to) {
    ASSERT(SqOnBoard(from));
    ASSERT(SqOnBoard(to));
    int pce = pos->pieces[from];
    ASSERT(PieceValidEmpty(pce));
    int col = COLOR_OF(pce);
    int pt = TYPE_OF(pce);
    U64 mask = (1ULL << from) | (1ULL << to);
    pos->pieces[from] = EMPTY;
    pos->pieces[to] = pce;
    pos->byColorBB[col] ^= mask;
    pos->byTypeBB[pt] ^= mask;
    pos->byTypeBB[ALL_PIECES] ^= mask;
}

void update_slider_blockers(S_BOARD *pos, int c) {
    U64 kbb = pos->byColorBB[c] & pos->byTypeBB[KING];
    if (!kbb) {
        pos->st->blockersForKing[c] = 0ULL;
        pos->st->pinners[c ^ 1] = 0ULL;
        return;
    }
    int ksq = LSBINDEX(kbb);
    pos->st->blockersForKing[c] = 0ULL;
    pos->st->pinners[c ^ 1] = 0ULL;

    int them = c ^ 1;
    U64 themPieces = pos->byColorBB[them];
    U64 snipers = ((get_rook_attacks(ksq, 0ULL) & (pos->byTypeBB[ROOK] | pos->byTypeBB[QUEEN])) |
                   (get_bishop_attacks(ksq, 0ULL) & (pos->byTypeBB[BISHOP] | pos->byTypeBB[QUEEN])))
                  & themPieces;

    while (snipers) {
        int sniperSq = LSBINDEX(snipers);
        snipers &= snipers - 1;
        U64 b = BetweenBB[ksq][sniperSq] & pos->byTypeBB[ALL_PIECES];
        if (b && !(b & (b - 1))) {
            pos->st->blockersForKing[c] |= b;
            if (b & pos->byColorBB[c]) {
                pos->st->pinners[them] |= (1ULL << sniperSq);
            }
        }
    }
}

void set_check_info(S_BOARD *pos) {
    pos->st->checkersBB = attackersToKingSq(pos, pos->side);
    update_slider_blockers(pos, WHITE);
    update_slider_blockers(pos, BLACK);
}

// Tests whether a pseudo-legal move is legal
int legal(const S_BOARD *pos, int move) {
    int us = pos->side;
    int them = us ^ 1;
    int from = FROMSQ(move);
    int to = TOSQ(move);
    int pce = pos->pieces[from];
    int pt = TYPE_OF(pce);
    U64 kbb = pos->byColorBB[us] & pos->byTypeBB[KING];
    ASSERT(kbb);
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

    // King moves
    if (pt == KING) {
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

int MoveExists(S_BOARD *pos, const int move){
    S_MOVELIST list[1];
    GenerateAllMoves(pos, list);

    for(int moveNum = 0; moveNum < list->count; ++moveNum){
        if(list->moves[moveNum].move == move){
            return legal(pos, move);
        }
    }
    return FALSE;
}

void makeMove(S_BOARD *pos, int move, StateInfo *newSt){
    ASSERT(moveValid(move));

    *newSt = *(pos->st);
    newSt->previous = pos->st;
    pos->st = newSt;

    int from = FROMSQ(move);
    int to = TOSQ(move);
    int side = pos->side;
    int pce = pos->pieces[from];
    int pt = TYPE_OF(pce);
    int captured = CAPTURED(move);

    pos->search->moveStack[pos->ply] = move;
    pos->search->pieceStack[pos->ply] = PTYPE_OF(pce);

    pos->st->capturedPiece = captured;

    DirtyPiece *dp = &newSt->dirtyPiece;
    dp->remove_count = 0;
    dp->add_count = 0;
    dp->king_moved[WHITE] = 0;
    dp->king_moved[BLACK] = 0;
    if (pt == KING) {
        dp->king_moved[side] = 1;
    }

    pos->st->fiftyMove++;
    if (captured != EMPTY || pt == PAWN) {
        pos->st->fiftyMove = 0;
    }

    if (pos->st->enPas != NO_SQ) {
        HASH_EP;
        pos->st->enPas = NO_SQ;
    }

    HASH_CA;
    pos->st->castleRights &= castlePerm[from];
    pos->st->castleRights &= castlePerm[to];
    HASH_CA;

    if (move & MVFLAGEP) {
        int capsq = (side == WHITE) ? (to - 8) : (to + 8);
        int capPce = pos->pieces[capsq];
        pos->st->capturedPiece = capPce;

        dp->piece_remove[0] = pce;
        dp->from[0] = from;
        dp->piece_remove[1] = capPce;
        dp->from[1] = capsq;
        dp->remove_count = 2;

        dp->piece_add[0] = pce;
        dp->to[0] = to;
        dp->add_count = 1;

        HASH_PCE(capPce, capsq);
        HASH_PK(capPce, capsq);
        pos->st->psqtmat -= PSQTMATTABLE[capPce][capsq];
        removePiece(pos, capsq);

        HASH_PCE(pce, from);
        HASH_PCE(pce, to);
        HASH_PK(pce, from);
        HASH_PK(pce, to);
        pos->st->psqtmat += PSQTMATTABLE[pce][to] - PSQTMATTABLE[pce][from];
        movePiece(pos, from, to);
    }
    else if (move & MVFLAGCA) {
        dp->piece_remove[0] = pce;
        dp->from[0] = from;
        dp->piece_add[0] = pce;
        dp->to[0] = to;

        HASH_PCE(pce, from);
        HASH_PCE(pce, to);
        HASH_PK(pce, from);
        HASH_PK(pce, to);
        HASH_NP(pce, from, side);
        HASH_NP(pce, to, side);
        pos->st->psqtmat += PSQTMATTABLE[pce][to] - PSQTMATTABLE[pce][from];
        movePiece(pos, from, to);

        int rfrom = 0, rto = 0;
        switch (to) {
            case C1: rfrom = A1; rto = D1; break;
            case G1: rfrom = H1; rto = F1; break;
            case C8: rfrom = A8; rto = D8; break;
            case G8: rfrom = H8; rto = F8; break;
        }
        int rookPce = pos->pieces[rfrom];

        dp->piece_remove[1] = rookPce;
        dp->from[1] = rfrom;
        dp->piece_add[1] = rookPce;
        dp->to[1] = rto;
        dp->remove_count = 2;
        dp->add_count = 2;

        HASH_PCE(rookPce, rfrom);
        HASH_PCE(rookPce, rto);
        HASH_NP(rookPce, rfrom, side);
        HASH_NP(rookPce, rto, side);
        pos->st->psqtmat += PSQTMATTABLE[rookPce][rto] - PSQTMATTABLE[rookPce][rfrom];
        movePiece(pos, rfrom, rto);
    }
    else {
        int promotedPiece = PROMOTED(move);
        int placedPiece = (promotedPiece != EMPTY) ? promotedPiece : pce;

        dp->piece_remove[0] = pce;
        dp->from[0] = from;
        dp->piece_add[0] = placedPiece;
        dp->to[0] = to;
        dp->add_count = 1;

        if (captured != EMPTY) {
            dp->piece_remove[1] = captured;
            dp->from[1] = to;
            dp->remove_count = 2;

            int capPt = TYPE_OF(captured);
            int them = side ^ 1;
            HASH_PCE(captured, to);
            if (capPt == PAWN || capPt == KING) {
                HASH_PK(captured, to);
            } else {
                HASH_NP(captured, to, them);
                if (capPt == KNIGHT || capPt == BISHOP)
                    HASH_MINOR(captured, to);
            }
            pos->st->psqtmat -= PSQTMATTABLE[captured][to];
            removePiece(pos, to);
        } else {
            dp->remove_count = 1;
        }

        if (promotedPiece != EMPTY) {
            HASH_PCE(pce, from);
            HASH_PK(pce, from);
            pos->st->psqtmat -= PSQTMATTABLE[pce][from];
            removePiece(pos, from);

            int promPt = TYPE_OF(promotedPiece);
            HASH_PCE(promotedPiece, to);
            HASH_NP(promotedPiece, to, side);
            if (promPt == KNIGHT || promPt == BISHOP)
                HASH_MINOR(promotedPiece, to);
            pos->st->psqtmat += PSQTMATTABLE[promotedPiece][to];
            putPiece(pos, promotedPiece, to);
        } else {
            HASH_PCE(pce, from);
            HASH_PCE(pce, to);
            if (pt == PAWN || pt == KING) {
                HASH_PK(pce, from);
                HASH_PK(pce, to);
            }
            if (pt != PAWN) {
                HASH_NP(pce, from, side);
                HASH_NP(pce, to, side);
                if (pt == KNIGHT || pt == BISHOP) {
                    HASH_MINOR(pce, from);
                    HASH_MINOR(pce, to);
                }
            }
            pos->st->psqtmat += PSQTMATTABLE[pce][to] - PSQTMATTABLE[pce][from];
            movePiece(pos, from, to);

            if (pt == PAWN && (move & MVFLAGPS)) {
                pos->st->enPas = (side == WHITE) ? (from + 8) : (from - 8);
                HASH_EP;
            }
        }
    }

    pos->st->pliesFromNull++;
    pos->hisPly++;
    pos->ply++;
    pos->side ^= 1;
    HASH_SIDE;

    int childPly = pos->ply;
    if (pos->search && childPly < MAXDEPTH) {
        pos->search->dirtyPieces[childPly] = newSt->dirtyPiece;
        pos->search->nnue_accumulators[childPly].computed[WHITE] = 0;
        pos->search->nnue_accumulators[childPly].computed[BLACK] = 0;
    }

    pos->st->checkersBB = attackersToKingSq(pos, pos->side);
    update_slider_blockers(pos, pos->side);
}

void takeMove(S_BOARD *pos) {
    pos->hisPly--;
    pos->ply--;

    int move = pos->search->moveStack[pos->ply];
    int from = FROMSQ(move);
    int to = TOSQ(move);

    pos->side ^= 1;
    int side = pos->side;

    if (move & MVFLAGEP) {
        int capsq = (side == WHITE) ? (to - 8) : (to + 8);
        int capPce = (side == WHITE) ? bP : wP;
        movePiece(pos, to, from);
        putPiece(pos, capPce, capsq);
    }
    else if (move & MVFLAGCA) {
        movePiece(pos, to, from);
        switch (to) {
            case C1: movePiece(pos, D1, A1); break;
            case C8: movePiece(pos, D8, A8); break;
            case G1: movePiece(pos, F1, H1); break;
            case G8: movePiece(pos, F8, H8); break;
        }
    }
    else {
        int promotedPiece = PROMOTED(move);
        if (promotedPiece != EMPTY) {
            removePiece(pos, to);
            putPiece(pos, MAKE_PIECE(side, PAWN), from);
        } else {
            movePiece(pos, to, from);
        }

        int captured = pos->st->capturedPiece;
        if (captured != EMPTY) {
            putPiece(pos, captured, to);
        }
    }

    pos->st = pos->st->previous;
}

void makeNullMove(S_BOARD *pos, StateInfo *newSt) {
    *newSt = *(pos->st);
    newSt->previous = pos->st;
    pos->st = newSt;

    DirtyPiece *dp = &newSt->dirtyPiece;
    dp->remove_count = 0;
    dp->add_count = 0;
    dp->king_moved[WHITE] = 0;
    dp->king_moved[BLACK] = 0;

    pos->search->moveStack[pos->ply] = NULLMOVE;
    pos->ply++;

    if (pos->st->enPas != NO_SQ) {
        HASH_EP;
        pos->st->enPas = NO_SQ;
    }

    pos->st->pliesFromNull = 0;
    pos->side ^= 1;
    pos->hisPly++;
    HASH_SIDE;

    pos->st->checkersBB = 0ULL;
    update_slider_blockers(pos, pos->side);

    int childPly = pos->ply;
    if (pos->search && childPly < MAXDEPTH) {
        pos->search->dirtyPieces[childPly] = newSt->dirtyPiece;
        pos->search->nnue_accumulators[childPly].computed[WHITE] = 0;
        pos->search->nnue_accumulators[childPly].computed[BLACK] = 0;
    }
}

void takeNullMove(S_BOARD *pos) {
    pos->hisPly--;
    pos->ply--;
    pos->side ^= 1;
    pos->st = pos->st->previous;
}