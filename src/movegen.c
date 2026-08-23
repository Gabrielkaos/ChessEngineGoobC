#include "stdio.h"
#include "defs.h"
#include "movegen.h"
#include "attacks.h"
#include "validate.h"

INLINE void AddMovee(const S_BOARD *pos, int move,S_MOVELIST *list){

    ASSERT(moveValid(move));

    list->moves[list->count].move =move;
    list->count++;
}

INLINE void AddWhitePawnCaptureMove(const S_BOARD *pos,const int from,const int to,const int cap,S_MOVELIST *list){

    ASSERT(SqOnBoard(from));
    ASSERT(SqOnBoard(to));
    ASSERT(PieceValidEmpty(cap));

    if(ranksBoard[from]==RANK_7){
        AddMovee(pos,MOVE(from,to,cap,wQ,0),list);
        AddMovee(pos,MOVE(from,to,cap,wR,0),list);
        AddMovee(pos,MOVE(from,to,cap,wB,0),list);
        AddMovee(pos,MOVE(from,to,cap,wN,0),list);
    }
    else{
        AddMovee(pos,MOVE(from,to,cap,EMPTY,0),list);
    }
}
INLINE void AddWhitePawnMove(const S_BOARD *pos,const int from,const int to,S_MOVELIST *list){

    ASSERT(SqOnBoard(from));
    ASSERT(SqOnBoard(to));

    if(ranksBoard[from]==RANK_7){
        AddMovee(pos,MOVE(from,to,EMPTY,wQ,0),list);
        AddMovee(pos,MOVE(from,to,EMPTY,wR,0),list);
        AddMovee(pos,MOVE(from,to,EMPTY,wB,0),list);
        AddMovee(pos,MOVE(from,to,EMPTY,wN,0),list);
    }
    else{
        AddMovee(pos,MOVE(from,to,EMPTY,EMPTY,0),list);
    }
}

INLINE void AddBlackPawnCaptureMove(const S_BOARD *pos,const int from,const int to,const int cap,S_MOVELIST *list){

    ASSERT(SqOnBoard(from));
    ASSERT(SqOnBoard(to));
    ASSERT(PieceValidEmpty(cap));

    if(ranksBoard[from]==RANK_2){
        AddMovee(pos,MOVE(from,to,cap,bQ,0),list);
        AddMovee(pos,MOVE(from,to,cap,bR,0),list);
        AddMovee(pos,MOVE(from,to,cap,bB,0),list);
        AddMovee(pos,MOVE(from,to,cap,bN,0),list);
    }
    else{
        AddMovee(pos,MOVE(from,to,cap,EMPTY,0),list);
    }
}
INLINE void AddBlackPawnMove(const S_BOARD *pos,const int from,const int to,S_MOVELIST *list){

    ASSERT(SqOnBoard(from));
    ASSERT(SqOnBoard(to));

    if(ranksBoard[from]==RANK_2){
        AddMovee(pos,MOVE(from,to,EMPTY,bQ,0),list);
        AddMovee(pos,MOVE(from,to,EMPTY,bR,0),list);
        AddMovee(pos,MOVE(from,to,EMPTY,bB,0),list);
        AddMovee(pos,MOVE(from,to,EMPTY,bN,0),list);
    }
    else{
        AddMovee(pos,MOVE(from,to,EMPTY,EMPTY,0),list);
    }
}

//bitboard based move generator

void GenerateAllMoves(const S_BOARD *pos,S_MOVELIST *list){

    ASSERT(checkBoard(pos));

    list->count=0;
    int side=pos->side;
    int source_square, target_square;
    U64 bitboard, attacks;

    //kings are never capturable - capturing one would clear the enemy king
    //bitboard and blind makeMove's legality check
    const U64 capturable = pos->occupancy[!side] & ~(pos->bitboards[wK]|pos->bitboards[bK]);

    //fix #2: only the 6 piece types belonging to the side to move are ever
    //relevant here (wP..wK = 1..6, bP..bK = 7..12 in the enum), so walk only
    //those instead of looping over all 12 and branching on side every time
    int base = (side == WHITE) ? wP : bP;

    for (int piece = base; piece <= base + 5; piece++)
    {
        bitboard = pos->bitboards[piece];

        if (side == WHITE)
        {
            if (piece == wP)
            {
                while (bitboard)
                {
                    source_square = LSBINDEX(bitboard);

                    target_square = source_square + 8;

                    if ((!GETBIT(pos->occupancy[BOTH], target_square)))
                    {
                        AddWhitePawnMove(pos,source_square,target_square,list);

                        if ((source_square >= A2 && source_square <= H2) && !GETBIT(pos->occupancy[BOTH], (target_square + 8)))
                            AddMovee(pos,MOVE(source_square,(target_square+8),0,0,MVFLAGPS),list);
                    }

                    attacks = pawn_attacks[side][source_square] & capturable;

                    while (attacks)
                    {
                        target_square = LSBINDEX(attacks);

                        AddWhitePawnCaptureMove(pos,source_square,target_square,pos->pieces[target_square],list);

                        //fix #1: target_square is always a set bit of attacks here
                        //(it just came from LSBINDEX(attacks)), so clear the lowest
                        //set bit directly instead of re-testing then clearing
                        attacks &= attacks - 1;
                    }

                    if (pos->enPas != NO_SQ)
                    {
                        U64 enpassant_attacks = pawn_attacks[side][source_square] & (1ULL << pos->enPas);

                        if (enpassant_attacks)
                        {
                            int target_enpassant = LSBINDEX(enpassant_attacks);
                            AddMovee(pos,MOVE(source_square,target_enpassant,0,0,MVFLAGEP),list);
                        }
                    }

                    bitboard &= bitboard - 1;
                }
            }

            if (piece == wK)
            {
                if (pos->castleRights & WKCA)
                {
                    if (!GETBIT(pos->occupancy[BOTH], F1) && !GETBIT(pos->occupancy[BOTH], G1))
                    {
                        if (!is_square_attacked_BB(E1, BLACK,pos) && !is_square_attacked_BB(F1, BLACK,pos))
                            AddMovee(pos,MOVE(E1,G1,0,0,MVFLAGCA),list);
                    }
                }

                if (pos->castleRights & WQCA)
                {
                    if (!GETBIT(pos->occupancy[BOTH], D1) && !GETBIT(pos->occupancy[BOTH], C1) && !GETBIT(pos->occupancy[BOTH], B1))
                    {
                        if (!is_square_attacked_BB(E1, BLACK,pos) && !is_square_attacked_BB(D1, BLACK,pos))
                            AddMovee(pos,MOVE(E1,C1,0,0,MVFLAGCA),list);
                    }
                }
            }
        }

        else
        {
            if (piece == bP)
            {
                while (bitboard)
                {
                    source_square = LSBINDEX(bitboard);

                    target_square = source_square - 8;

                    if ((!GETBIT(pos->occupancy[BOTH], target_square)))
                    {
                        AddBlackPawnMove(pos,source_square,target_square,list);

                        if ((source_square >= A7 && source_square <= H7) && !GETBIT(pos->occupancy[BOTH], (target_square - 8)))
                            AddMovee(pos,MOVE(source_square,(target_square-8),0,0,MVFLAGPS),list);
                    }

                    attacks = pawn_attacks[side][source_square] & capturable;

                    while (attacks)
                    {
                        target_square = LSBINDEX(attacks);

                        AddBlackPawnCaptureMove(pos,source_square,target_square,pos->pieces[target_square],list);

                        attacks &= attacks - 1;
                    }

                    if (pos->enPas != NO_SQ)
                    {
                        U64 enpassant_attacks = pawn_attacks[side][source_square] & (1ULL << pos->enPas);

                        if (enpassant_attacks)
                        {
                            int target_enpassant = LSBINDEX(enpassant_attacks);
                            AddMovee(pos,MOVE(source_square,target_enpassant,0,0,MVFLAGEP),list);
                        }
                    }

                    bitboard &= bitboard - 1;
                }
            }

            if (piece == bK)
            {
                if (pos->castleRights & BKCA)
                {
                    if (!GETBIT(pos->occupancy[BOTH], F8) && !GETBIT(pos->occupancy[BOTH], G8))
                    {
                        if (!is_square_attacked_BB(E8, WHITE,pos) && !is_square_attacked_BB(F8, WHITE,pos))
                            AddMovee(pos,MOVE(E8,G8,0,0,MVFLAGCA),list);
                    }
                }

                if (pos->castleRights & BQCA)
                {
                    if (!GETBIT(pos->occupancy[BOTH], D8) && !GETBIT(pos->occupancy[BOTH], C8) && !GETBIT(pos->occupancy[BOTH], B8))
                    {
                        if (!is_square_attacked_BB(E8, WHITE,pos) && !is_square_attacked_BB(D8, WHITE,pos))
                            AddMovee(pos,MOVE(E8,C8,0,0,MVFLAGCA),list);
                    }
                }
            }
        }

        //knights
        if (piece == base + 1)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                //fix #4: attacks already excludes our own pieces, so every
                //target square is either empty or an enemy piece - split into
                //two branch-free loops instead of testing GETBIT per move
                U64 pseudo = knight_attacks[source_square] & ~pos->occupancy[side];

                U64 caps = pseudo & capturable;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    caps &= caps - 1;
                }

                U64 quiets = pseudo & ~pos->occupancy[BOTH];
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
        if (piece == base + 2)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                U64 pseudo = get_bishop_attacks(source_square, pos->occupancy[BOTH]) & ~pos->occupancy[side];

                U64 caps = pseudo & capturable;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    caps &= caps - 1;
                }

                U64 quiets = pseudo & ~pos->occupancy[BOTH];
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
        if (piece == base + 3)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                U64 pseudo = get_rook_attacks(source_square, pos->occupancy[BOTH]) & ~pos->occupancy[side];

                U64 caps = pseudo & capturable;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    caps &= caps - 1;
                }

                U64 quiets = pseudo & ~pos->occupancy[BOTH];
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
        if (piece == base + 4)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                U64 pseudo = get_queen_attacks(source_square, pos->occupancy[BOTH]) & ~pos->occupancy[side];

                U64 caps = pseudo & capturable;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    caps &= caps - 1;
                }

                U64 quiets = pseudo & ~pos->occupancy[BOTH];
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
        if (piece == base + 5)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                U64 pseudo = king_attacks[source_square] & ~pos->occupancy[side];

                U64 caps = pseudo & capturable;
                while (caps)
                {
                    target_square = LSBINDEX(caps);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    caps &= caps - 1;
                }

                U64 quiets = pseudo & ~pos->occupancy[BOTH];
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

    //kings are never capturable - capturing one would clear the enemy king
    //bitboard and blind makeMove's legality check
    const U64 capturable = pos->occupancy[!side] & ~(pos->bitboards[wK]|pos->bitboards[bK]);

    int base = (side == WHITE) ? wP : bP;

    for (int piece = base; piece <= base + 5; piece++)
    {
        bitboard = pos->bitboards[piece];

        if (side == WHITE)
        {
            if (piece == wP)
            {
                while (bitboard)
                {
                    source_square = LSBINDEX(bitboard);
                    target_square = source_square + 8;
                    attacks = pawn_attacks[side][source_square] & capturable;

                    if ((!GETBIT(pos->occupancy[BOTH], target_square)))
                    {
                        if(ranksBoard[source_square]==RANK_7)AddWhitePawnMove(pos,source_square,target_square,list);
                    }

                    while (attacks)
                    {
                        target_square = LSBINDEX(attacks);

                        AddWhitePawnCaptureMove(pos,source_square,target_square,pos->pieces[target_square],list);

                        attacks &= attacks - 1;
                    }

                    if (pos->enPas != NO_SQ)
                    {
                        U64 enpassant_attacks = pawn_attacks[side][source_square] & (1ULL << pos->enPas);

                        if (enpassant_attacks)
                        {
                            int target_enpassant = LSBINDEX(enpassant_attacks);
                            AddMovee(pos,MOVE(source_square,target_enpassant,0,0,MVFLAGEP),list);
                        }
                    }

                    bitboard &= bitboard - 1;
                }
            }
        }

        else
        {
            if (piece == bP)
            {
                while (bitboard)
                {
                    source_square = LSBINDEX(bitboard);
                    target_square = source_square - 8;

                    attacks = pawn_attacks[side][source_square] & capturable;

                    if ((!GETBIT(pos->occupancy[BOTH], target_square)))
                    {
                        if(ranksBoard[source_square]==RANK_2)AddBlackPawnMove(pos,source_square,target_square,list);
                    }

                    while (attacks)
                    {
                        target_square = LSBINDEX(attacks);
                        AddBlackPawnCaptureMove(pos,source_square,target_square,pos->pieces[target_square],list);

                        attacks &= attacks - 1;
                    }

                    if (pos->enPas != NO_SQ)
                    {
                        U64 enpassant_attacks = pawn_attacks[side][source_square] & (1ULL << pos->enPas);

                        if (enpassant_attacks)
                        {
                            int target_enpassant = LSBINDEX(enpassant_attacks);
                            AddMovee(pos,MOVE(source_square,target_enpassant,0,0,MVFLAGEP),list);
                        }
                    }

                    bitboard &= bitboard - 1;
                }
            }
        }

        //fix #4: noisy generation only ever wants captures, so mask attacks
        //with the enemy occupancy directly instead of generating the full
        //pseudo-legal set (captures + quiets) and branch-filtering per move
        if (piece == base + 1) //knights
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

        if (piece == base + 2) //bishops
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                attacks = get_bishop_attacks(source_square, pos->occupancy[BOTH]) & capturable;

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    attacks &= attacks - 1;
                }

                bitboard &= bitboard - 1;
            }
        }

        if (piece == base + 3) //rooks
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);
                attacks = get_rook_attacks(source_square, pos->occupancy[BOTH]) & capturable;

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    attacks &= attacks - 1;
                }

                bitboard &= bitboard - 1;
            }
        }

        if (piece == base + 4) //queens
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);
                attacks = get_queen_attacks(source_square, pos->occupancy[BOTH]) & capturable;

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);
                    AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);
                    attacks &= attacks - 1;
                }

                bitboard &= bitboard - 1;
            }
        }

        if (piece == base + 5) //kings
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

    if(pce == EMPTY || pieceCol[pce] != side) return FALSE;

    int cap  = CAPTURED(move);
    int prom = PROMOTED(move);
    int targetPce = pos->pieces[to];

    //kings are never capturable - guards TT/PV junk moves from corrupting state
    if(targetPce==wK || targetPce==bK) return FALSE;

    //-------- castling --------
    if(move & MVFLAGCA){
        if(!pieceKing[pce] || cap != EMPTY || prom != EMPTY) return FALSE;

        if(side == WHITE){
            if(from != E1) return FALSE;
            if(to == G1)
                return (pos->castleRights & WKCA)
                    && !GETBIT(pos->occupancy[BOTH],F1)
                    && !GETBIT(pos->occupancy[BOTH],G1)
                    && !is_square_attacked_BB(E1,BLACK,pos)
                    && !is_square_attacked_BB(F1,BLACK,pos);
            if(to == C1)
                return (pos->castleRights & WQCA)
                    && !GETBIT(pos->occupancy[BOTH],D1)
                    && !GETBIT(pos->occupancy[BOTH],C1)
                    && !GETBIT(pos->occupancy[BOTH],B1)
                    && !is_square_attacked_BB(E1,BLACK,pos)
                    && !is_square_attacked_BB(D1,BLACK,pos);
            return FALSE;
        }else{
            if(from != E8) return FALSE;
            if(to == G8)
                return (pos->castleRights & BKCA)
                    && !GETBIT(pos->occupancy[BOTH],F8)
                    && !GETBIT(pos->occupancy[BOTH],G8)
                    && !is_square_attacked_BB(E8,WHITE,pos)
                    && !is_square_attacked_BB(F8,WHITE,pos);
            if(to == C8)
                return (pos->castleRights & BQCA)
                    && !GETBIT(pos->occupancy[BOTH],D8)
                    && !GETBIT(pos->occupancy[BOTH],C8)
                    && !GETBIT(pos->occupancy[BOTH],B8)
                    && !is_square_attacked_BB(E8,WHITE,pos)
                    && !is_square_attacked_BB(D8,WHITE,pos);
            return FALSE;
        }
    }

    //-------- en passant --------
    if(move & MVFLAGEP){
        if(!piecePawn[pce] || prom != EMPTY) return FALSE;
        if(pos->enPas == NO_SQ || to != pos->enPas) return FALSE;
        if(targetPce != EMPTY) return FALSE;
        return !!GETBIT(pawn_attacks[side][from], to);
    }

    //-------- pawn pushes / captures --------
    if(piecePawn[pce]){
        int promRank    = (side == WHITE) ? RANK_7 : RANK_2;
        int mustPromote = (ranksBoard[from] == promRank);

        if(mustPromote){
            if(prom == EMPTY || pieceCol[prom] != side ||
               pieceType[prom] == p_pawn || pieceType[prom] == p_king)
                return FALSE;
        }else if(prom != EMPTY) return FALSE;

        int push = (side == WHITE) ? from + 8 : from - 8;

        if(move & MVFLAGPS){
            int startRank = (side == WHITE) ? RANK_2 : RANK_7;
            int dbl       = (side == WHITE) ? from + 16 : from - 16;
            if(cap != EMPTY || ranksBoard[from] != startRank || to != dbl) return FALSE;
            return !GETBIT(pos->occupancy[BOTH], push) && !GETBIT(pos->occupancy[BOTH], to);
        }

        if(to == push){
            if(cap != EMPTY) return FALSE;
            return !GETBIT(pos->occupancy[BOTH], to);
        }

        //capture
        if(!GETBIT(pawn_attacks[side][from], to)) return FALSE;
        if(targetPce == EMPTY || pieceCol[targetPce] == side) return FALSE;
        return cap == targetPce;
    }

    //-------- knight / bishop / rook / queen / king --------
    if(prom != EMPTY) return FALSE;

    U64 attackSet;
    if(pieceKnight[pce])          attackSet = knight_attacks[from];
    else if(pieceKing[pce])       attackSet = king_attacks[from];
    else if(pce==wB || pce==bB)   attackSet = get_bishop_attacks(from, pos->occupancy[BOTH]);
    else if(pce==wR || pce==bR)   attackSet = get_rook_attacks(from, pos->occupancy[BOTH]);
    else if(pce==wQ || pce==bQ)   attackSet = get_queen_attacks(from, pos->occupancy[BOTH]);
    else return FALSE;

    if(!GETBIT(attackSet, to)) return FALSE;

    if(targetPce == EMPTY) return cap == EMPTY;
    if(pieceCol[targetPce] == side) return FALSE;
    return cap == targetPce;
}

void GenerateAllQuiet(const S_BOARD *pos, S_MOVELIST *list){

    ASSERT(checkBoard(pos));

    int side = pos->side;
    int source_square, target_square;
    U64 bitboard, quiets;

    int base = (side == WHITE) ? wP : bP;

    for (int piece = base; piece <= base + 5; piece++)
    {
        bitboard = pos->bitboards[piece];

        if (side == WHITE && piece == wP)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);
                target_square = source_square + 8;

                //skip promotion-rank pushes entirely - those are noisy
                if (ranksBoard[source_square] != RANK_7 &&
                    !GETBIT(pos->occupancy[BOTH], target_square))
                {
                    AddMovee(pos,MOVE(source_square,target_square,EMPTY,EMPTY,0),list);

                    if (source_square >= A2 && source_square <= H2 &&
                        !GETBIT(pos->occupancy[BOTH], (target_square + 8)))
                        AddMovee(pos,MOVE(source_square,(target_square+8),0,0,MVFLAGPS),list);
                }
                bitboard &= bitboard - 1;
            }
        }
        else if (side == BLACK && piece == bP)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);
                target_square = source_square - 8;

                if (ranksBoard[source_square] != RANK_2 &&
                    !GETBIT(pos->occupancy[BOTH], target_square))
                {
                    AddMovee(pos,MOVE(source_square,target_square,EMPTY,EMPTY,0),list);

                    if (source_square >= A7 && source_square <= H7 &&
                        !GETBIT(pos->occupancy[BOTH], (target_square - 8)))
                        AddMovee(pos,MOVE(source_square,(target_square-8),0,0,MVFLAGPS),list);
                }
                bitboard &= bitboard - 1;
            }
        }

        if (side == WHITE && piece == wK)
        {
            if (pos->castleRights & WKCA)
                if (!GETBIT(pos->occupancy[BOTH], F1) && !GETBIT(pos->occupancy[BOTH], G1))
                    if (!is_square_attacked_BB(E1, BLACK,pos) && !is_square_attacked_BB(F1, BLACK,pos))
                        AddMovee(pos,MOVE(E1,G1,0,0,MVFLAGCA),list);

            if (pos->castleRights & WQCA)
                if (!GETBIT(pos->occupancy[BOTH], D1) && !GETBIT(pos->occupancy[BOTH], C1) && !GETBIT(pos->occupancy[BOTH], B1))
                    if (!is_square_attacked_BB(E1, BLACK,pos) && !is_square_attacked_BB(D1, BLACK,pos))
                        AddMovee(pos,MOVE(E1,C1,0,0,MVFLAGCA),list);
        }

        if (side == BLACK && piece == bK)
        {
            if (pos->castleRights & BKCA)
                if (!GETBIT(pos->occupancy[BOTH], F8) && !GETBIT(pos->occupancy[BOTH], G8))
                    if (!is_square_attacked_BB(E8, WHITE,pos) && !is_square_attacked_BB(F8, WHITE,pos))
                        AddMovee(pos,MOVE(E8,G8,0,0,MVFLAGCA),list);

            if (pos->castleRights & BQCA)
                if (!GETBIT(pos->occupancy[BOTH], D8) && !GETBIT(pos->occupancy[BOTH], C8) && !GETBIT(pos->occupancy[BOTH], B8))
                    if (!is_square_attacked_BB(E8, WHITE,pos) && !is_square_attacked_BB(D8, WHITE,pos))
                        AddMovee(pos,MOVE(E8,C8,0,0,MVFLAGCA),list);
        }

        if (piece == base + 1){ //knights
            while (bitboard){
                source_square = LSBINDEX(bitboard);
                quiets = knight_attacks[source_square] & ~pos->occupancy[BOTH];
                while (quiets){ target_square = LSBINDEX(quiets); AddMovee(pos,MOVE(source_square,target_square,0,0,0),list); quiets &= quiets - 1; }
                bitboard &= bitboard - 1;
            }
        }
        if (piece == base + 2){ //bishops
            while (bitboard){
                source_square = LSBINDEX(bitboard);
                quiets = get_bishop_attacks(source_square, pos->occupancy[BOTH]) & ~pos->occupancy[BOTH];
                while (quiets){ target_square = LSBINDEX(quiets); AddMovee(pos,MOVE(source_square,target_square,0,0,0),list); quiets &= quiets - 1; }
                bitboard &= bitboard - 1;
            }
        }
        if (piece == base + 3){ //rooks
            while (bitboard){
                source_square = LSBINDEX(bitboard);
                quiets = get_rook_attacks(source_square, pos->occupancy[BOTH]) & ~pos->occupancy[BOTH];
                while (quiets){ target_square = LSBINDEX(quiets); AddMovee(pos,MOVE(source_square,target_square,0,0,0),list); quiets &= quiets - 1; }
                bitboard &= bitboard - 1;
            }
        }
        if (piece == base + 4){ //queens
            while (bitboard){
                source_square = LSBINDEX(bitboard);
                quiets = get_queen_attacks(source_square, pos->occupancy[BOTH]) & ~pos->occupancy[BOTH];
                while (quiets){ target_square = LSBINDEX(quiets); AddMovee(pos,MOVE(source_square,target_square,0,0,0),list); quiets &= quiets - 1; }
                bitboard &= bitboard - 1;
            }
        }
        if (piece == base + 5){ //kings
            while (bitboard){
                source_square = LSBINDEX(bitboard);
                quiets = king_attacks[source_square] & ~pos->occupancy[BOTH];
                while (quiets){ target_square = LSBINDEX(quiets); AddMovee(pos,MOVE(source_square,target_square,0,0,0),list); quiets &= quiets - 1; }
                bitboard &= bitboard - 1;
            }
        }
    }
}