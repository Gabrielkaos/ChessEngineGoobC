#include "stdio.h"
#include "defs.h"
#include "movegen.h"
#include "bitboards.h"
#include "attacks.h"
#include "validate.h"

INLINE void AddMovee(const S_BOARD *pos, int move,S_MOVELIST *list){

    ASSERT(moveValid(move));

    list->moves[list->count].move =move;
    list->count++;
}

//adds Q,R,B,N promotions (base is wP or bP so base+1..base+4 are N,B,R,Q)
INLINE void AddPromotionMoves(const S_BOARD *pos,const int from,const int to,const int cap,const int base,S_MOVELIST *list){

    ASSERT(SqOnBoard(from));
    ASSERT(SqOnBoard(to));
    ASSERT(PieceValidEmpty(cap));

    AddMovee(pos,MOVE(from,to,cap,base+4,0),list);
    AddMovee(pos,MOVE(from,to,cap,base+3,0),list);
    AddMovee(pos,MOVE(from,to,cap,base+2,0),list);
    AddMovee(pos,MOVE(from,to,cap,base+1,0),list);
}

//bitboard based move generator

void GenerateAllMoves(const S_BOARD *pos,S_MOVELIST *list){

    ASSERT(checkBoard(pos));

    list->count=0;
    int side=pos->side;
    int source_square, target_square;
    U64 bitboard;

    const U64 occ = pos->byTypeBB[ALL_PIECES];
    const U64 my_occ = pos->byColorBB[side];
    const U64 capturable = pos->byColorBB[!side] & ~pos->byTypeBB[KING];
    int base = (side == WHITE) ? wP : bP;

    for (int pt = PAWN; pt <= KING; pt++)
    {
        bitboard = my_occ & pos->byTypeBB[pt];

        if (side == WHITE)
        {
            if (pt == PAWN)
            {
                //bulk generation: whole pawn sets are shifted at once and
                //promotions are split off by target rank, instead of walking
                //pawn-by-pawn with per-pawn rank lookups
                const U64 empty   = ~occ;
                const U64 pawnsBB = bitboard;
                const U64 rank8   = RankBBMask[RANK_8];
                U64 t;

                //single pushes, promotion pushes split off by landing rank
                const U64 pushes = (pawnsBB << 8) & empty;

                t = pushes & rank8;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddPromotionMoves(pos,target_square-8,target_square,EMPTY,base,list);
                }

                t = pushes & ~rank8;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddMovee(pos,MOVE(target_square-8,target_square,EMPTY,EMPTY,0),list);
                }

                //double pushes: single-push targets still on rank 3 push again
                t = ((pushes & RankBBMask[RANK_3]) << 8) & empty;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddMovee(pos,MOVE(target_square-16,target_square,EMPTY,EMPTY,MVFLAGPS),list);
                }

                //captures as bulk shifts (<<7 west, <<9 east), promotions split
                U64 caps = (pawnsBB << 7) & NOT_H_FILE & capturable;

                t = caps & rank8;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddPromotionMoves(pos,target_square-7,target_square,pos->pieces[target_square],base,list);
                }
                caps &= ~rank8;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    caps &= caps - 1;

                    AddMovee(pos,MOVE(target_square-7,target_square,pos->pieces[target_square],EMPTY,0),list);
                }

                caps = (pawnsBB << 9) & NOT_A_FILE & capturable;

                t = caps & rank8;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddPromotionMoves(pos,target_square-9,target_square,pos->pieces[target_square],base,list);
                }
                caps &= ~rank8;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    caps &= caps - 1;

                    AddMovee(pos,MOVE(target_square-9,target_square,pos->pieces[target_square],EMPTY,0),list);
                }

                //en passant: computed once for the whole pawn set - a white
                //pawn attacks the ep square iff it stands where a black pawn
                //on the ep square would attack
                if (pos->enPas != NO_SQ)
                {
                    t = pawn_attacks[BLACK][pos->enPas] & pawnsBB;

                    while (t)
                    {
                        source_square = LSBINDEX(t);
                        t &= t - 1;

                        AddMovee(pos,MOVE(source_square,pos->enPas,EMPTY,EMPTY,MVFLAGEP),list);
                    }
                }
            }

            if (pt == KING)
            {
                if (pos->castleRights & (WKCA|WQCA))
                {
                    //E1 safety is shared by both castling sides - check it once
                    const int kingSqSafe = !is_square_attacked_BB(E1, BLACK, pos);

                    if ((pos->castleRights & WKCA) && kingSqSafe &&
                        !(occ & ((1ULL << F1) | (1ULL << G1))) &&
                        !is_square_attacked_BB(F1, BLACK, pos))
                        AddMovee(pos,MOVE(E1,G1,0,0,MVFLAGCA),list);

                    if ((pos->castleRights & WQCA) && kingSqSafe &&
                        !(occ & ((1ULL << B1) | (1ULL << C1) | (1ULL << D1))) &&
                        !is_square_attacked_BB(D1, BLACK, pos))
                        AddMovee(pos,MOVE(E1,C1,0,0,MVFLAGCA),list);
                }
            }
        }

        else
        {
            if (pt == PAWN)
            {
                //bulk generation, black mirrors: shifts go down, promotions
                //land on rank 1, doubles run through rank 6
                const U64 empty   = ~occ;
                const U64 pawnsBB = bitboard;
                const U64 rank1   = RankBBMask[RANK_1];
                U64 t;

                const U64 pushes = (pawnsBB >> 8) & empty;

                t = pushes & rank1;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddPromotionMoves(pos,target_square+8,target_square,EMPTY,base,list);
                }

                t = pushes & ~rank1;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddMovee(pos,MOVE(target_square+8,target_square,EMPTY,EMPTY,0),list);
                }

                t = ((pushes & RankBBMask[RANK_6]) >> 8) & empty;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddMovee(pos,MOVE(target_square+16,target_square,EMPTY,EMPTY,MVFLAGPS),list);
                }

                //black capture shifts: >>9 west (NOT_H_FILE), >>7 east (NOT_A_FILE)
                U64 caps = (pawnsBB >> 9) & NOT_H_FILE & capturable;

                t = caps & rank1;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddPromotionMoves(pos,target_square+9,target_square,pos->pieces[target_square],base,list);
                }
                caps &= ~rank1;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    caps &= caps - 1;

                    AddMovee(pos,MOVE(target_square+9,target_square,pos->pieces[target_square],EMPTY,0),list);
                }

                caps = (pawnsBB >> 7) & NOT_A_FILE & capturable;

                t = caps & rank1;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddPromotionMoves(pos,target_square+7,target_square,pos->pieces[target_square],base,list);
                }
                caps &= ~rank1;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    caps &= caps - 1;

                    AddMovee(pos,MOVE(target_square+7,target_square,pos->pieces[target_square],EMPTY,0),list);
                }

                //en passant: a black pawn attacks the ep square iff it stands
                //where a white pawn on the ep square would attack
                if (pos->enPas != NO_SQ)
                {
                    t = pawn_attacks[WHITE][pos->enPas] & pawnsBB;

                    while (t)
                    {
                        source_square = LSBINDEX(t);
                        t &= t - 1;

                        AddMovee(pos,MOVE(source_square,pos->enPas,EMPTY,EMPTY,MVFLAGEP),list);
                    }
                }
            }

            if (pt == KING)
            {
                if (pos->castleRights & (BKCA|BQCA))
                {
                    const int kingSqSafe = !is_square_attacked_BB(E8, WHITE, pos);

                    if ((pos->castleRights & BKCA) && kingSqSafe &&
                        !(occ & ((1ULL << F8) | (1ULL << G8))) &&
                        !is_square_attacked_BB(F8, WHITE, pos))
                        AddMovee(pos,MOVE(E8,G8,0,0,MVFLAGCA),list);

                    if ((pos->castleRights & BQCA) && kingSqSafe &&
                        !(occ & ((1ULL << B8) | (1ULL << C8) | (1ULL << D8))) &&
                        !is_square_attacked_BB(D8, WHITE, pos))
                        AddMovee(pos,MOVE(E8,C8,0,0,MVFLAGCA),list);
                }
            }
        }

        //knights
        if (pt == KNIGHT)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                U64 pseudo = knight_attacks[source_square] & ~my_occ;

                U64 caps = pseudo & capturable;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    caps &= caps - 1;
                }

                U64 quiets = pseudo & ~occ;
                while (quiets)
                {
                    target_square = LSBINDEX(quiets);
                    AddMovee(pos,MOVE(source_square,target_square,0,0,0),list);
                    quiets &= quiets - 1;
                }

                bitboard &= bitboard - 1;
            }
        }

        //bishops
        if (pt == BISHOP)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                U64 pseudo = get_bishop_attacks(source_square, occ) & ~my_occ;

                U64 caps = pseudo & capturable;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    caps &= caps - 1;
                }

                U64 quiets = pseudo & ~occ;
                while (quiets)
                {
                    target_square = LSBINDEX(quiets);
                    AddMovee(pos,MOVE(source_square,target_square,0,0,0),list);
                    quiets &= quiets - 1;
                }

                bitboard &= bitboard - 1;
            }
        }

        //rooks
        if (pt == ROOK)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                U64 pseudo = get_rook_attacks(source_square, occ) & ~my_occ;

                U64 caps = pseudo & capturable;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    caps &= caps - 1;
                }

                U64 quiets = pseudo & ~occ;
                while (quiets)
                {
                    target_square = LSBINDEX(quiets);
                    AddMovee(pos,MOVE(source_square,target_square,0,0,0),list);
                    quiets &= quiets - 1;
                }

                bitboard &= bitboard - 1;
            }
        }

        //queens
        if (pt == QUEEN)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                U64 pseudo = get_queen_attacks(source_square, occ) & ~my_occ;

                U64 caps = pseudo & capturable;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    caps &= caps - 1;
                }

                U64 quiets = pseudo & ~occ;
                while (quiets)
                {
                    target_square = LSBINDEX(quiets);
                    AddMovee(pos,MOVE(source_square,target_square,0,0,0),list);
                    quiets &= quiets - 1;
                }

                bitboard &= bitboard - 1;
            }
        }

        //kings
        if (pt == KING)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                U64 pseudo = king_attacks[source_square] & ~my_occ;

                U64 caps = pseudo & capturable;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    caps &= caps - 1;
                }

                U64 quiets = pseudo & ~occ;
                while (quiets)
                {
                    target_square = LSBINDEX(quiets);
                    AddMovee(pos,MOVE(source_square,target_square,0,0,0),list);
                    quiets &= quiets - 1;
                }

                bitboard &= bitboard - 1;
            }
        }
    }
}
void GenerateAllNoisy(const S_BOARD *pos,S_MOVELIST *list){

    ASSERT(checkBoard(pos));

    list->count=0;
    int side=pos->side;

    int source_square, target_square;

    U64 bitboard, attacks;

    const U64 occ = pos->byTypeBB[ALL_PIECES];
    const U64 my_occ = pos->byColorBB[side];
    const U64 capturable = pos->byColorBB[!side] & ~pos->byTypeBB[KING];

    int base = (side == WHITE) ? wP : bP;

    for (int pt = PAWN; pt <= KING; pt++)
    {
        bitboard = my_occ & pos->byTypeBB[pt];

        if (side == WHITE)
        {
            if (pt == PAWN)
            {
                //noisy pawns in bulk: promotion pushes + all captures + EP
                const U64 empty   = ~occ;
                const U64 pawnsBB = bitboard;
                const U64 rank8   = RankBBMask[RANK_8];
                U64 t;

                //promotion pushes only - quiet pushes are not noisy
                t = (pawnsBB << 8) & empty & rank8;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddPromotionMoves(pos,target_square-8,target_square,EMPTY,base,list);
                }

                U64 caps = (pawnsBB << 7) & NOT_H_FILE & capturable;

                t = caps & rank8;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddPromotionMoves(pos,target_square-7,target_square,pos->pieces[target_square],base,list);
                }
                caps &= ~rank8;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    caps &= caps - 1;

                    AddMovee(pos,MOVE(target_square-7,target_square,pos->pieces[target_square],EMPTY,0),list);
                }

                caps = (pawnsBB << 9) & NOT_A_FILE & capturable;

                t = caps & rank8;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddPromotionMoves(pos,target_square-9,target_square,pos->pieces[target_square],base,list);
                }
                caps &= ~rank8;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    caps &= caps - 1;

                    AddMovee(pos,MOVE(target_square-9,target_square,pos->pieces[target_square],EMPTY,0),list);
                }

                if (pos->enPas != NO_SQ)
                {
                    t = pawn_attacks[BLACK][pos->enPas] & pawnsBB;

                    while (t)
                    {
                        source_square = LSBINDEX(t);
                        t &= t - 1;

                        AddMovee(pos,MOVE(source_square,pos->enPas,EMPTY,EMPTY,MVFLAGEP),list);
                    }
                }
            }
        }

        else
        {
            if (pt == PAWN)
            {
                //noisy pawns in bulk, black mirror (promotions land on rank 1)
                const U64 empty   = ~occ;
                const U64 pawnsBB = bitboard;
                const U64 rank1   = RankBBMask[RANK_1];
                U64 t;

                t = (pawnsBB >> 8) & empty & rank1;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddPromotionMoves(pos,target_square+8,target_square,EMPTY,base,list);
                }

                U64 caps = (pawnsBB >> 9) & NOT_H_FILE & capturable;

                t = caps & rank1;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddPromotionMoves(pos,target_square+9,target_square,pos->pieces[target_square],base,list);
                }
                caps &= ~rank1;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    caps &= caps - 1;

                    AddMovee(pos,MOVE(target_square+9,target_square,pos->pieces[target_square],EMPTY,0),list);
                }

                caps = (pawnsBB >> 7) & NOT_A_FILE & capturable;

                t = caps & rank1;
                while (t)
                {
                    target_square = LSBINDEX(t);
                    t &= t - 1;

                    AddPromotionMoves(pos,target_square+7,target_square,pos->pieces[target_square],base,list);
                }
                caps &= ~rank1;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    caps &= caps - 1;

                    AddMovee(pos,MOVE(target_square+7,target_square,pos->pieces[target_square],EMPTY,0),list);
                }

                if (pos->enPas != NO_SQ)
                {
                    t = pawn_attacks[WHITE][pos->enPas] & pawnsBB;

                    while (t)
                    {
                        source_square = LSBINDEX(t);
                        t &= t - 1;

                        AddMovee(pos,MOVE(source_square,pos->enPas,EMPTY,EMPTY,MVFLAGEP),list);
                    }
                }
            }
        }

        //noisy generation only ever wants captures, so mask attacks
        //with the enemy occupancy directly instead of generating the full
        //pseudo-legal set (captures + quiets) and branch-filtering per move
        if (pt == KNIGHT)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                attacks = knight_attacks[source_square] & capturable;

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    attacks &= attacks - 1;
                }

                bitboard &= bitboard - 1;
            }
        }

        if (pt == BISHOP)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                attacks = get_bishop_attacks(source_square, occ) & capturable;

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    attacks &= attacks - 1;
                }

                bitboard &= bitboard - 1;
            }
        }

        if (pt == ROOK)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);
                attacks = get_rook_attacks(source_square, occ) & capturable;

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    attacks &= attacks - 1;
                }

                bitboard &= bitboard - 1;
            }
        }

        if (pt == QUEEN)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);
                attacks = get_queen_attacks(source_square, occ) & capturable;

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    attacks &= attacks - 1;
                }

                bitboard &= bitboard - 1;
            }
        }

        if (pt == KING)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                attacks = king_attacks[source_square] & capturable;

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    attacks &= attacks - 1;
                }
                bitboard &= bitboard - 1;
            }
        }
    }
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
                return (pos->castleRights & WKCA)
                    && !(occ & ((1ULL << F1) | (1ULL << G1)))
                    && !is_square_attacked_BB(E1,BLACK,pos)
                    && !is_square_attacked_BB(F1,BLACK,pos);
            if(to == C1)
                return (pos->castleRights & WQCA)
                    && !(occ & ((1ULL << D1) | (1ULL << C1) | (1ULL << B1)))
                    && !is_square_attacked_BB(E1,BLACK,pos)
                    && !is_square_attacked_BB(D1,BLACK,pos);
            return FALSE;
        }else{
            if(from != E8) return FALSE;
            if(to == G8)
                return (pos->castleRights & BKCA)
                    && !(occ & ((1ULL << F8) | (1ULL << G8)))
                    && !is_square_attacked_BB(E8,WHITE,pos)
                    && !is_square_attacked_BB(F8,WHITE,pos);
            if(to == C8)
                return (pos->castleRights & BQCA)
                    && !(occ & ((1ULL << D8) | (1ULL << C8) | (1ULL << B8)))
                    && !is_square_attacked_BB(E8,WHITE,pos)
                    && !is_square_attacked_BB(D8,WHITE,pos);
            return FALSE;
        }
    }

    //-------- en passant --------
    if(move & MVFLAGEP){
        if(TYPE_OF(pce) != PAWN || prom != EMPTY) return FALSE;
        if(pos->enPas == NO_SQ || to != pos->enPas) return FALSE;
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

void GenerateAllQuiet(const S_BOARD *pos, S_MOVELIST *list){

    ASSERT(checkBoard(pos));

    int side = pos->side;
    int source_square, target_square;
    U64 bitboard, quiets;

    const U64 occ = pos->byTypeBB[ALL_PIECES];
    const U64 my_occ = pos->byColorBB[side];

    for (int pt = PAWN; pt <= KING; pt++)
    {
        bitboard = my_occ & pos->byTypeBB[pt];

        if (side == WHITE && pt == PAWN)
        {
            //quiet pawns in bulk: non-promoting pushes + doubles only
            //(promotion pushes are noisy and generated elsewhere)
            const U64 empty   = ~occ;
            const U64 pawnsBB = bitboard;
            U64 t;

            t = (pawnsBB << 8) & empty & ~RankBBMask[RANK_8];
            while (t)
            {
                target_square = LSBINDEX(t);
                t &= t - 1;

                AddMovee(pos,MOVE(target_square-8,target_square,EMPTY,EMPTY,0),list);
            }

            t = ((pawnsBB << 8) & empty & RankBBMask[RANK_3]) << 8 & empty;
            while (t)
            {
                target_square = LSBINDEX(t);
                t &= t - 1;

                AddMovee(pos,MOVE(target_square-16,target_square,EMPTY,EMPTY,MVFLAGPS),list);
            }
        }
        else if (side == BLACK && pt == PAWN)
        {
            const U64 empty   = ~occ;
            const U64 pawnsBB = bitboard;
            U64 t;

            t = (pawnsBB >> 8) & empty & ~RankBBMask[RANK_1];
            while (t)
            {
                target_square = LSBINDEX(t);
                t &= t - 1;

                AddMovee(pos,MOVE(target_square+8,target_square,EMPTY,EMPTY,0),list);
            }

            t = ((pawnsBB >> 8) & empty & RankBBMask[RANK_6]) >> 8 & empty;
            while (t)
            {
                target_square = LSBINDEX(t);
                t &= t - 1;

                AddMovee(pos,MOVE(target_square+16,target_square,EMPTY,EMPTY,MVFLAGPS),list);
            }
        }

        if (side == WHITE && pt == KING)
        {
            if (pos->castleRights & (WKCA|WQCA))
            {
                //E1 safety is shared by both castling sides - check it once
                const int kingSqSafe = !is_square_attacked_BB(E1, BLACK, pos);

                if ((pos->castleRights & WKCA) && kingSqSafe &&
                    !(occ & ((1ULL << F1) | (1ULL << G1))) &&
                    !is_square_attacked_BB(F1, BLACK, pos))
                    AddMovee(pos,MOVE(E1,G1,0,0,MVFLAGCA),list);

                if ((pos->castleRights & WQCA) && kingSqSafe &&
                    !(occ & ((1ULL << B1) | (1ULL << C1) | (1ULL << D1))) &&
                    !is_square_attacked_BB(D1, BLACK, pos))
                    AddMovee(pos,MOVE(E1,C1,0,0,MVFLAGCA),list);
            }
        }

        if (side == BLACK && pt == KING)
        {
            if (pos->castleRights & (BKCA|BQCA))
            {
                const int kingSqSafe = !is_square_attacked_BB(E8, WHITE, pos);

                if ((pos->castleRights & BKCA) && kingSqSafe &&
                    !(occ & ((1ULL << F8) | (1ULL << G8))) &&
                    !is_square_attacked_BB(F8, WHITE, pos))
                    AddMovee(pos,MOVE(E8,G8,0,0,MVFLAGCA),list);

                if ((pos->castleRights & BQCA) && kingSqSafe &&
                    !(occ & ((1ULL << B8) | (1ULL << C8) | (1ULL << D8))) &&
                    !is_square_attacked_BB(D8, WHITE, pos))
                    AddMovee(pos,MOVE(E8,C8,0,0,MVFLAGCA),list);
            }
        }

        if (pt == KNIGHT){ //knights
            while (bitboard){
                source_square = LSBINDEX(bitboard);
                quiets = knight_attacks[source_square] & ~occ;
                while (quiets){ target_square = LSBINDEX(quiets); AddMovee(pos,MOVE(source_square,target_square,0,0,0),list); quiets &= quiets - 1; }
                bitboard &= bitboard - 1;
            }
        }
        if (pt == BISHOP){ //bishops
            while (bitboard){
                source_square = LSBINDEX(bitboard);
                quiets = get_bishop_attacks(source_square, occ) & ~occ;
                while (quiets){ target_square = LSBINDEX(quiets); AddMovee(pos,MOVE(source_square,target_square,0,0,0),list); quiets &= quiets - 1; }
                bitboard &= bitboard - 1;
            }
        }
        if (pt == ROOK){ //rooks
            while (bitboard){
                source_square = LSBINDEX(bitboard);
                quiets = get_rook_attacks(source_square, occ) & ~occ;
                while (quiets){ target_square = LSBINDEX(quiets); AddMovee(pos,MOVE(source_square,target_square,0,0,0),list); quiets &= quiets - 1; }
                bitboard &= bitboard - 1;
            }
        }
        if (pt == QUEEN){ //queens
            while (bitboard){
                source_square = LSBINDEX(bitboard);
                quiets = get_queen_attacks(source_square, occ) & ~occ;
                while (quiets){ target_square = LSBINDEX(quiets); AddMovee(pos,MOVE(source_square,target_square,0,0,0),list); quiets &= quiets - 1; }
                bitboard &= bitboard - 1;
            }
        }
        if (pt == KING){ //kings
            while (bitboard){
                source_square = LSBINDEX(bitboard);
                quiets = king_attacks[source_square] & ~occ;
                while (quiets){ target_square = LSBINDEX(quiets); AddMovee(pos,MOVE(source_square,target_square,0,0,0),list); quiets &= quiets - 1; }
                bitboard &= bitboard - 1;
            }
        }
    }
}