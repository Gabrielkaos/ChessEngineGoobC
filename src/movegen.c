#include "stdio.h"
#include "defs.h"
#include "movegen.h"
#include "bitboards.h"
#include "attacks.h"
#include "validate.h"

//Moves are written through a local cursor and the count is stored once at the
//end: list->count and the S_MOVE entries are both int, so bumping the count in
//memory after every move forced a store and a reload per move.
//
//Each generator is written once for a compile-time colour (us is a literal
//WHITE/BLACK at every call site, so the shifts, masks and squares fold) and
//emits moves in a fixed order that the move picker's tie-breaking depends on:
//  all  : pawns, knights, bishops, rooks, queens, castling, king
//  noisy: pawns, knights, bishops, rooks, queens, king
//  quiet: pawns, knights, bishops, rooks, queens, castling, king

INLINE S_MOVE *addMove(S_MOVE *cur, int move){
    ASSERT(moveValid(move));
    cur->move = move;
    return cur + 1;
}

//adds Q,R,B,N promotions (base is wP or bP so base+1..base+4 are N,B,R,Q)
INLINE S_MOVE *addPromotions(S_MOVE *cur, int from, int to, int cap, int base){
    ASSERT(SqOnBoard(from));
    ASSERT(SqOnBoard(to));
    ASSERT(PieceValidEmpty(cap));

    cur = addMove(cur, MOVE(from,to,cap,(base+4),0));
    cur = addMove(cur, MOVE(from,to,cap,(base+3),0));
    cur = addMove(cur, MOVE(from,to,cap,(base+2),0));
    cur = addMove(cur, MOVE(from,to,cap,(base+1),0));
    return cur;
}

//pawn moves landing on targets, which came from shifting the pawns by delta
//(to = from + delta); promotions are split off by landing rank
INLINE S_MOVE *addPawnCaptures(const S_BOARD *pos, S_MOVE *cur, U64 targets, int delta, U64 promoRank, int base){
    U64 t = targets & promoRank;
    while (t) {
        int to = poplsb(&t);
        cur = addPromotions(cur, to - delta, to, pos->pieces[to], base);
    }
    t = targets & ~promoRank;
    while (t) {
        int to = poplsb(&t);
        cur = addMove(cur, MOVE(to - delta, to, pos->pieces[to], EMPTY, 0));
    }
    return cur;
}

INLINE S_MOVE *addPieceCaptures(const S_BOARD *pos, S_MOVE *cur, int from, U64 targets){
    while (targets) {
        int to = poplsb(&targets);
        cur = addMove(cur, MOVE(from, to, pos->pieces[to], 0, 0));
    }
    return cur;
}

INLINE S_MOVE *addPieceQuiets(S_MOVE *cur, int from, U64 targets){
    while (targets) {
        int to = poplsb(&targets);
        cur = addMove(cur, MOVE(from, to, 0, 0, 0));
    }
    return cur;
}

INLINE U64 pieceAttacks(int pt, int sq, U64 occ){
    switch (pt) {
        case KNIGHT: return knight_attacks[sq];
        case BISHOP: return get_bishop_attacks(sq, occ);
        case ROOK:   return get_rook_attacks(sq, occ);
        case QUEEN:  return get_queen_attacks(sq, occ);
        default:     return king_attacks[sq];
    }
}

//moves of every piece of type pt: captures (into capturable) then quiets
//(into empty squares) per piece, either set may be masked out by the caller
INLINE S_MOVE *addPieceMoves(const S_BOARD *pos, S_MOVE *cur, int us, int pt, U64 occ, U64 capMask, U64 quietMask){
    U64 pieces = pos->byColorBB[us] & pos->byTypeBB[pt];
    while (pieces) {
        int from = poplsb(&pieces);
        U64 attacks = pieceAttacks(pt, from, occ);
        cur = addPieceCaptures(pos, cur, from, attacks & capMask);
        cur = addPieceQuiets(cur, from, attacks & quietMask);
    }
    return cur;
}

INLINE S_MOVE *addCastling(const S_BOARD *pos, S_MOVE *cur, int us, U64 occ){
    const int them   = us ^ 1;
    const int kingCA = (us == WHITE) ? WKCA : BKCA;
    const int queenCA= (us == WHITE) ? WQCA : BQCA;
    const int rights = pos->st->castleRights;

    if (!(rights & (kingCA | queenCA))) return cur;

    const int e = RELATIVE_SQ(us, E1), f = RELATIVE_SQ(us, F1), g = RELATIVE_SQ(us, G1);
    const int d = RELATIVE_SQ(us, D1), c = RELATIVE_SQ(us, C1), b = RELATIVE_SQ(us, B1);

    //E1 safety is shared by both castling sides - check it once
    const int kingSqSafe = !is_square_attacked_BB(e, them, pos);

    if ((rights & kingCA) && kingSqSafe &&
        !(occ & ((1ULL << f) | (1ULL << g))) &&
        !is_square_attacked_BB(f, them, pos))
        cur = addMove(cur, MOVE(e, g, 0, 0, MVFLAGCA));

    if ((rights & queenCA) && kingSqSafe &&
        !(occ & ((1ULL << b) | (1ULL << c) | (1ULL << d))) &&
        !is_square_attacked_BB(d, them, pos))
        cur = addMove(cur, MOVE(e, c, 0, 0, MVFLAGCA));

    return cur;
}

enum { GEN_ALL, GEN_NOISY, GEN_QUIET };

//bulk pawn generation: whole pawn sets are shifted at once and promotions
//are split off by target rank. Noisy = promotion pushes, captures and ep;
//quiet = the other single pushes and the double pushes.
INLINE S_MOVE *addPawnMoves(const S_BOARD *pos, S_MOVE *cur, int us, int type, U64 occ, U64 capturable){
    const int them    = us ^ 1;
    const int base    = (us == WHITE) ? wP : bP;
    const int up      = (us == WHITE) ? 8 : -8;
    const U64 empty   = ~occ;
    const U64 pawns   = pos->byColorBB[us] & pos->byTypeBB[PAWN];
    const U64 promo   = RankBBMask[us == WHITE ? RANK_8 : RANK_1];
    const U64 dblRank = RankBBMask[us == WHITE ? RANK_3 : RANK_6];

    const U64 pushes  = (us == WHITE ? pawns << 8 : pawns >> 8) & empty;
    U64 t;

    if (type != GEN_QUIET) {
        t = pushes & promo;
        while (t) {
            int to = poplsb(&t);
            cur = addPromotions(cur, to - up, to, EMPTY, base);
        }
    }

    if (type != GEN_NOISY) {
        t = pushes & ~promo;
        while (t) {
            int to = poplsb(&t);
            cur = addMove(cur, MOVE(to - up, to, EMPTY, EMPTY, 0));
        }

        //double pushes: single-push targets still on the third rank push again
        t = (us == WHITE ? (pushes & dblRank) << 8 : (pushes & dblRank) >> 8) & empty;
        while (t) {
            int to = poplsb(&t);
            cur = addMove(cur, MOVE(to - 2 * up, to, EMPTY, EMPTY, MVFLAGPS));
        }
    }

    if (type != GEN_QUIET) {
        //towards the a-file first (white <<7, black >>9), then the h-file
        if (us == WHITE) {
            cur = addPawnCaptures(pos, cur, (pawns << 7) & NOT_H_FILE & capturable, 7, promo, base);
            cur = addPawnCaptures(pos, cur, (pawns << 9) & NOT_A_FILE & capturable, 9, promo, base);
        } else {
            cur = addPawnCaptures(pos, cur, (pawns >> 9) & NOT_H_FILE & capturable, -9, promo, base);
            cur = addPawnCaptures(pos, cur, (pawns >> 7) & NOT_A_FILE & capturable, -7, promo, base);
        }

        //en passant: one of our pawns attacks the ep square iff it stands
        //where an enemy pawn on the ep square would attack
        const int ep = pos->st->enPas;
        if (ep != NO_SQ) {
            t = pawn_attacks[them][ep] & pawns;
            while (t) {
                int from = poplsb(&t);
                cur = addMove(cur, MOVE(from, ep, EMPTY, EMPTY, MVFLAGEP));
            }
        }
    }

    return cur;
}

INLINE S_MOVE *generate(const S_BOARD *pos, S_MOVE *cur, int us, int type){
    const U64 occ        = pos->byTypeBB[ALL_PIECES];
    const U64 capturable = pos->byColorBB[us ^ 1] & ~pos->byTypeBB[KING];
    const U64 capMask    = (type == GEN_QUIET) ? 0ULL : capturable;
    const U64 quietMask  = (type == GEN_NOISY) ? 0ULL : ~occ;

    cur = addPawnMoves(pos, cur, us, type, occ, capturable);

    cur = addPieceMoves(pos, cur, us, KNIGHT, occ, capMask, quietMask);
    cur = addPieceMoves(pos, cur, us, BISHOP, occ, capMask, quietMask);
    cur = addPieceMoves(pos, cur, us, ROOK,   occ, capMask, quietMask);
    cur = addPieceMoves(pos, cur, us, QUEEN,  occ, capMask, quietMask);

    if (type != GEN_NOISY)
        cur = addCastling(pos, cur, us, occ);

    return addPieceMoves(pos, cur, us, KING, occ, capMask, quietMask);
}

void GenerateAllMoves(const S_BOARD *pos,S_MOVELIST *list){
    ASSERT(checkBoard(pos));

    S_MOVE *end = (pos->side == WHITE) ? generate(pos, list->moves, WHITE, GEN_ALL)
                                       : generate(pos, list->moves, BLACK, GEN_ALL);
    list->count = (int)(end - list->moves);
}

void GenerateAllNoisy(const S_BOARD *pos,S_MOVELIST *list){
    ASSERT(checkBoard(pos));

    S_MOVE *end = (pos->side == WHITE) ? generate(pos, list->moves, WHITE, GEN_NOISY)
                                       : generate(pos, list->moves, BLACK, GEN_NOISY);
    list->count = (int)(end - list->moves);
}

//appends to the list (the move picker keeps the noisy moves in front)
void GenerateAllQuiet(const S_BOARD *pos, S_MOVELIST *list){
    ASSERT(checkBoard(pos));

    S_MOVE *start = list->moves + list->count;
    S_MOVE *end = (pos->side == WHITE) ? generate(pos, start, WHITE, GEN_QUIET)
                                       : generate(pos, start, BLACK, GEN_QUIET);
    list->count = (int)(end - list->moves);
}

int moveIsPseudoLegal(const S_BOARD *pos, int move){

    if(move == NOMOVE || move == NULLMOVE) return FALSE;

    int from = FROMSQ(move);
    int to   = TOSQ(move);

    //FROMSQ/TOSQ mask 7 bits (0-127) but the board is only 0-63 -
    //reject out-of-range squares before they're used to index pos->pieces
    if(from > H8 || to > H8) return FALSE;

    int side = pos->side;
    int pce  = pos->pieces[from];

    if(pce == EMPTY || COLOR_OF(pce) != side) return FALSE;

    int cap  = CAPTURED(move);
    int prom = PROMOTED(move);
    int targetPce = pos->pieces[to];

    //kings are never capturable - guards TT/PV junk moves from corrupting state
    if(targetPce != EMPTY && TYPE_OF(targetPce) == KING) return FALSE;

    const U64 occ = pos->byTypeBB[ALL_PIECES];

    //-------- castling --------
    if(move & MVFLAGCA){
        if(TYPE_OF(pce) != KING || cap != EMPTY || prom != EMPTY) return FALSE;

        if(side == WHITE){
            if(from != E1) return FALSE;
            if(to == G1)
                return (pos->st->castleRights & WKCA)
                    && !(occ & ((1ULL << F1) | (1ULL << G1)))
                    && !is_square_attacked_BB(E1,BLACK,pos)
                    && !is_square_attacked_BB(F1,BLACK,pos);
            if(to == C1)
                return (pos->st->castleRights & WQCA)
                    && !(occ & ((1ULL << D1) | (1ULL << C1) | (1ULL << B1)))
                    && !is_square_attacked_BB(E1,BLACK,pos)
                    && !is_square_attacked_BB(D1,BLACK,pos);
            return FALSE;
        }else{
            if(from != E8) return FALSE;
            if(to == G8)
                return (pos->st->castleRights & BKCA)
                    && !(occ & ((1ULL << F8) | (1ULL << G8)))
                    && !is_square_attacked_BB(E8,WHITE,pos)
                    && !is_square_attacked_BB(F8,WHITE,pos);
            if(to == C8)
                return (pos->st->castleRights & BQCA)
                    && !(occ & ((1ULL << D8) | (1ULL << C8) | (1ULL << B8)))
                    && !is_square_attacked_BB(E8,WHITE,pos)
                    && !is_square_attacked_BB(D8,WHITE,pos);
            return FALSE;
        }
    }

    //-------- en passant --------
    if(move & MVFLAGEP){
        if(TYPE_OF(pce) != PAWN || prom != EMPTY) return FALSE;
        if(pos->st->enPas == NO_SQ || to != pos->st->enPas) return FALSE;
        if(targetPce != EMPTY) return FALSE;
        return (pawn_attacks[side][from] & (1ULL << to)) != 0;
    }

    //-------- pawn pushes / captures --------
    if(TYPE_OF(pce) == PAWN){
        int promRank    = (side == WHITE) ? RANK_7 : RANK_2;
        int mustPromote = (RANK_OF(from) == promRank);

        if(mustPromote){
            if(prom == EMPTY || COLOR_OF(prom) != side ||
               TYPE_OF(prom) == PAWN || TYPE_OF(prom) == KING)
                return FALSE;
        }else if(prom != EMPTY) return FALSE;

        int push = (side == WHITE) ? from + 8 : from - 8;

        if(move & MVFLAGPS){
            int startRank = (side == WHITE) ? RANK_2 : RANK_7;
            int dbl       = (side == WHITE) ? from + 16 : from - 16;
            if(cap != EMPTY || RANK_OF(from) != startRank || to != dbl) return FALSE;
            return !(occ & ((1ULL << push) | (1ULL << to)));
        }

        if(to == push){
            if(cap != EMPTY) return FALSE;
            return !(occ & (1ULL << to));
        }

        //capture
        if(!(pawn_attacks[side][from] & (1ULL << to))) return FALSE;
        if(targetPce == EMPTY || COLOR_OF(targetPce) == side) return FALSE;
        return cap == targetPce;
    }

    //-------- knight / bishop / rook / queen / king --------
    if(prom != EMPTY) return FALSE;

    U64 attackSet;
    int pt = TYPE_OF(pce);
    if(pt == KNIGHT)              attackSet = knight_attacks[from];
    else if(pt == KING)           attackSet = king_attacks[from];
    else if(pt == BISHOP)         attackSet = get_bishop_attacks(from, occ);
    else if(pt == ROOK)           attackSet = get_rook_attacks(from, occ);
    else if(pt == QUEEN)          attackSet = get_queen_attacks(from, occ);
    else return FALSE;

    if(!(attackSet & (1ULL << to))) return FALSE;

    if(targetPce == EMPTY) return cap == EMPTY;
    if(COLOR_OF(targetPce) == side) return FALSE;
    return cap == targetPce;
}
