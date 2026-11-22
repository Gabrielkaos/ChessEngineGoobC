// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2022-2026 Gabriel Montes

#include "stdio.h"
#include "search.h"
#include "bitboards.h"
#include "movegen.h"
#include "makemove.h"
#include "init.h"
#include "attacks.h"
#include "validate.h"
#include "nnue_loader.h"
#include "some_maths.h"

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
    if (pt == KING) pos->kingSq[col] = (uint8_t)sq;
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
    if (pt == KING) pos->kingSq[col] = (uint8_t)to;
}

void update_slider_blockers(const S_BOARD *pos, int c) {
    int ksq = pos->kingSq[c];
    if (ksq >= NO_SQ) {
        pos->st->blockersForKing = 0ULL;
        return;
    }

    int them = c ^ 1;
    U64 themPieces = pos->byColorBB[them];
    U64 snipers = ((rook_pseudo_attacks[ksq] & (pos->byTypeBB[ROOK] | pos->byTypeBB[QUEEN])) |
                   (bishop_pseudo_attacks[ksq] & (pos->byTypeBB[BISHOP] | pos->byTypeBB[QUEEN])))
                  & themPieces;

    if (!snipers) {
        pos->st->blockersForKing = 0ULL;
        return;
    }

    //accumulate in local: pos->st may alias the board, so in-place |= would
    //reload and store on every sniper
    U64 occ = pos->byTypeBB[ALL_PIECES];
    U64 blockers = 0ULL;

    while (snipers) {
        int sniperSq = LSBINDEX(snipers);
        snipers &= snipers - 1;
        U64 b = BetweenBB[ksq][sniperSq] & occ;
        if (b && !(b & (b - 1))) {
            blockers |= b;
        }
    }

    pos->st->blockersForKing = blockers;
}

void set_check_info(S_BOARD *pos) {
    pos->st->checkersBB = attackersToKingSq(pos, pos->side);
    pos->st->checkersComputed = 1;
    update_slider_blockers(pos, pos->side);
    pos->st->pinsComputed = 1;
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

INLINE void makeMoveImpl(S_BOARD *pos, int move, StateInfo *newSt, int computeCheckers){
    ASSERT(moveValid(move));

    memcpy(newSt, pos->st, offsetof(StateInfo, repetition));
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

    pos->st->capturedPiece = (uint8_t)captured;

    int childPly = pos->ply + 1;
    DirtyPiece dummyDp;
    DirtyPiece *dp = (pos->search && childPly < MAXDEPTH)
                   ? &pos->search->dirtyPieces[childPly]
                   : &dummyDp;
    dp->remove_count = 0;
    dp->add_count = 0;
    dp->refresh[WHITE] = 0;
    dp->refresh[BLACK] = 0;
    if (pt == KING) {
        if (nnue_king_needs_refresh(side, from, to)) {
            dp->refresh[side] = 1;
        }
    }

    pos->st->fiftyMove++;
    if (captured != EMPTY || pt == PAWN) {
        pos->st->fiftyMove = 0;
    }

    if (pos->st->enPas != NO_SQ) {
        HASH_EP;
        pos->st->enPas = NO_SQ;
    }

    if (pos->st->castleRights) {
        HASH_CA;
        pos->st->castleRights &= (uint8_t)(castlePerm[from] & castlePerm[to]);
        HASH_CA;
    }

    if (move & MVFLAGEP) {
        int capsq = (side == WHITE) ? (to - 8) : (to + 8);
        int capPce = pos->pieces[capsq];
        pos->st->capturedPiece = (uint8_t)capPce;

        dp->piece_remove[0] = (uint8_t)pce;
        dp->from[0] = (uint8_t)from;
        dp->piece_remove[1] = (uint8_t)capPce;
        dp->from[1] = (uint8_t)capsq;
        dp->remove_count = 2;

        dp->piece_add[0] = (uint8_t)pce;
        dp->to[0] = (uint8_t)to;
        dp->add_count = 1;

        HASH_PCE(capPce, capsq);
        HASH_PK(capPce, capsq);
        removePiece(pos, capsq);

        HASH_PCE(pce, from);
        HASH_PCE(pce, to);
        HASH_PK(pce, from);
        HASH_PK(pce, to);
        movePiece(pos, from, to);
    }
    else if (move & MVFLAGCA) {
        dp->piece_remove[0] = (uint8_t)pce;
        dp->from[0] = (uint8_t)from;
        dp->piece_add[0] = (uint8_t)pce;
        dp->to[0] = (uint8_t)to;

        HASH_PCE(pce, from);
        HASH_PCE(pce, to);
        HASH_PK(pce, from);
        HASH_PK(pce, to);
        HASH_NP(pce, from, side);
        HASH_NP(pce, to, side);
        movePiece(pos, from, to);

        int rfrom = 0, rto = 0;
        switch (to) {
            case C1: rfrom = A1; rto = D1; break;
            case G1: rfrom = H1; rto = F1; break;
            case C8: rfrom = A8; rto = D8; break;
            case G8: rfrom = H8; rto = F8; break;
        }
        int rookPce = pos->pieces[rfrom];

        dp->piece_remove[1] = (uint8_t)rookPce;
        dp->from[1] = (uint8_t)rfrom;
        dp->piece_add[1] = (uint8_t)rookPce;
        dp->to[1] = (uint8_t)rto;
        dp->remove_count = 2;
        dp->add_count = 2;

        HASH_PCE(rookPce, rfrom);
        HASH_PCE(rookPce, rto);
        HASH_NP(rookPce, rfrom, side);
        HASH_NP(rookPce, rto, side);
        movePiece(pos, rfrom, rto);
    }
    else {
        int promotedPiece = PROMOTED(move);
        int placedPiece = (promotedPiece != EMPTY) ? promotedPiece : pce;

        dp->piece_remove[0] = (uint8_t)pce;
        dp->from[0] = (uint8_t)from;
        dp->piece_add[0] = (uint8_t)placedPiece;
        dp->to[0] = (uint8_t)to;
        dp->add_count = 1;

        if (captured != EMPTY) {
            dp->piece_remove[1] = (uint8_t)captured;
            dp->from[1] = (uint8_t)to;
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
            removePiece(pos, to);
        } else {
            dp->remove_count = 1;
        }

        if (promotedPiece != EMPTY) {
            HASH_PCE(pce, from);
            HASH_PK(pce, from);
            removePiece(pos, from);

            int promPt = TYPE_OF(promotedPiece);
            HASH_PCE(promotedPiece, to);
            HASH_NP(promotedPiece, to, side);
            if (promPt == KNIGHT || promPt == BISHOP)
                HASH_MINOR(promotedPiece, to);
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
            movePiece(pos, from, to);

            if (pt == PAWN && (move & MVFLAGPS)) {
                pos->st->enPas = (uint8_t)((side == WHITE) ? (from + 8) : (from - 8));
                HASH_EP;
            }
        }
    }

    pos->st->pliesFromNull++;
    pos->hisPly++;
    pos->ply = childPly;
    pos->side ^= 1;
    HASH_SIDE;

    newSt->repetition = 0;
    int end = MIN(newSt->fiftyMove, newSt->pliesFromNull);
    if (end >= 4) {
        StateInfo *stp = newSt->previous ? newSt->previous->previous : NULL;
        for (int i = 4; i <= end; i += 2) {
            if (!stp || !stp->previous || !stp->previous->previous) break;
            stp = stp->previous->previous;
            if (stp->posKey == newSt->posKey) {
                newSt->repetition = (int16_t)(stp->repetition ? -i : i);
                break;
            }
        }
    }

    if (pos->search && childPly < MAXDEPTH) {
        pos->search->accIdx[childPly] = (uint8_t)childPly;
        pos->search->nnue_accumulators[childPly].computed[WHITE] = 0;
        pos->search->nnue_accumulators[childPly].computed[BLACK] = 0;
    }

    pos->st->pinsComputed = 0;
    if (computeCheckers) {
        pos->st->checkersBB = attackersToKingSq(pos, pos->side);
        pos->st->checkersComputed = 1;
    } else {
        pos->st->checkersComputed = 0;
    }
}

void makeMove(S_BOARD *pos, int move, StateInfo *newSt){
    makeMoveImpl(pos, move, newSt, 1);
}

void makeMoveQSearch(S_BOARD *pos, int move, StateInfo *newSt){
    makeMoveImpl(pos, move, newSt, 0);
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
    memcpy(newSt, pos->st, offsetof(StateInfo, repetition));
    newSt->previous = pos->st;
    pos->st = newSt;

    int parentPly = pos->ply;
    int childPly = parentPly + 1;

    pos->search->moveStack[parentPly] = NULLMOVE;
    pos->ply = childPly;

    if (pos->st->enPas != NO_SQ) {
        HASH_EP;
        pos->st->enPas = NO_SQ;
    }

    pos->st->pliesFromNull = 0;
    pos->side ^= 1;
    pos->hisPly++;
    HASH_SIDE;

    newSt->repetition = 0;
    newSt->capturedPiece = EMPTY;
    newSt->checkersBB = 0ULL;
    newSt->checkersComputed = 1;
    newSt->pinsComputed = 0;

    if (pos->search && childPly < MAXDEPTH) {
        DirtyPiece *dp = &pos->search->dirtyPieces[childPly];
        dp->remove_count = 0;
        dp->add_count = 0;
        dp->refresh[WHITE] = 0;
        dp->refresh[BLACK] = 0;
        // Null move leaves all pieces untouched: alias parent's accumulator slot (zero copy)
        pos->search->accIdx[childPly] = pos->search->accIdx[parentPly];
    }
}

void takeNullMove(S_BOARD *pos) {
    pos->hisPly--;
    pos->ply--;
    pos->side ^= 1;
    pos->st = pos->st->previous;
}