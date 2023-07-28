#include "stdio.h"
#include "defs.h"
#include "see.h"

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

    for (int piece = wP; piece <= bK; piece++)
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

                    attacks = pawn_attacks[side][source_square] & pos->occupancy[BLACK];

                    while (attacks)
                    {
                        target_square = LSBINDEX(attacks);

                        AddWhitePawnCaptureMove(pos,source_square,target_square,pos->pieces[target_square],list);

                        POPBIT(attacks, target_square);
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

                    POPBIT(bitboard, source_square);
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

                    attacks = pawn_attacks[side][source_square] & pos->occupancy[WHITE];

                    while (attacks)
                    {
                        target_square = LSBINDEX(attacks);

                        AddBlackPawnCaptureMove(pos,source_square,target_square,pos->pieces[target_square],list);

                        POPBIT(attacks, target_square);
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

                    POPBIT(bitboard, source_square);
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
        if ((side == WHITE) ? piece == wN : piece == bN)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                attacks = knight_attacks[source_square] & ((side == WHITE) ? ~pos->occupancy[WHITE] : ~pos->occupancy[BLACK]);

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);

                    if (!GETBIT(((side == WHITE) ? pos->occupancy[BLACK] : pos->occupancy[WHITE]), target_square))
                        AddMovee(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                        AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

                    POPBIT(attacks, target_square);
                }


                POPBIT(bitboard, source_square);
            }
        }

        //bishops
        if ((side == WHITE) ? piece == wB : piece == bB)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                attacks = get_bishop_attacks(source_square, pos->occupancy[BOTH]) & ((side == WHITE) ? ~pos->occupancy[WHITE] : ~pos->occupancy[BLACK]);

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);

                    if (!GETBIT(((side == WHITE) ? pos->occupancy[BLACK] : pos->occupancy[WHITE]), target_square))
                        AddMovee(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                        AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

                    POPBIT(attacks, target_square);
                }


                POPBIT(bitboard, source_square);
            }
        }

        //rooks
        if ((side == WHITE) ? piece == wR : piece == bR)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                attacks = get_rook_attacks(source_square, pos->occupancy[BOTH]) & ((side == WHITE) ? ~pos->occupancy[WHITE] : ~pos->occupancy[BLACK]);

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);

                    if (!GETBIT(((side == WHITE) ? pos->occupancy[BLACK] : pos->occupancy[WHITE]), target_square))
                        AddMovee(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                        AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

                    POPBIT(attacks, target_square);
                }


                // pop ls1b of the current piece bitboard copy
                POPBIT(bitboard, source_square);
            }
        }

        //queens
        if ((side == WHITE) ? piece == wQ : piece == bQ)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                attacks = get_queen_attacks(source_square, pos->occupancy[BOTH]) & ((side == WHITE) ? ~pos->occupancy[WHITE] : ~pos->occupancy[BLACK]);

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);

                    if (!GETBIT(((side == WHITE) ? pos->occupancy[BLACK] : pos->occupancy[WHITE]), target_square))
                        AddMovee(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                         AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

                    POPBIT(attacks, target_square);
                }


                // pop ls1b of the current piece bitboard copy
                POPBIT(bitboard, source_square);
            }
        }

        //kings
        if ((side == WHITE) ? piece == wK : piece == bK)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                attacks = king_attacks[source_square] & ((side == WHITE) ? ~pos->occupancy[WHITE] : ~pos->occupancy[BLACK]);

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);

                    if (!GETBIT(((side == WHITE) ? pos->occupancy[BLACK] : pos->occupancy[WHITE]), target_square))
                        AddMovee(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                         AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

                    POPBIT(attacks, target_square);
                }

                POPBIT(bitboard, source_square);
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

    for (int piece = wP; piece <= bK; piece++)
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
                    attacks = pawn_attacks[side][source_square] & pos->occupancy[BLACK];

                    if ((!GETBIT(pos->occupancy[BOTH], target_square)))
                    {
                        if(ranksBoard[source_square]==RANK_7)AddWhitePawnMove(pos,source_square,target_square,list);
                    }

                    while (attacks)
                    {
                        target_square = LSBINDEX(attacks);

                        AddWhitePawnCaptureMove(pos,source_square,target_square,pos->pieces[target_square],list);

                        POPBIT(attacks, target_square);
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

                    POPBIT(bitboard, source_square);
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

                    attacks = pawn_attacks[side][source_square] & pos->occupancy[WHITE];

                    if ((!GETBIT(pos->occupancy[BOTH], target_square)))
                    {
                        if(ranksBoard[source_square]==RANK_2)AddBlackPawnMove(pos,source_square,target_square,list);
                    }

                    while (attacks)
                    {
                        target_square = LSBINDEX(attacks);
                        AddBlackPawnCaptureMove(pos,source_square,target_square,pos->pieces[target_square],list);

                        POPBIT(attacks, target_square);
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

                    POPBIT(bitboard, source_square);
                }
            }
        }

        if ((side == WHITE) ? piece == wN : piece == bN)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                attacks = knight_attacks[source_square] & ((side == WHITE) ? ~pos->occupancy[WHITE] : ~pos->occupancy[BLACK]);

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);

                    if (GETBIT(((side == WHITE) ? pos->occupancy[BLACK] : pos->occupancy[WHITE]), target_square))
                        AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

                    POPBIT(attacks, target_square);
                }

                POPBIT(bitboard, source_square);
            }
        }

        if ((side == WHITE) ? piece == wB : piece == bB)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                attacks = get_bishop_attacks(source_square, pos->occupancy[BOTH]) & ((side == WHITE) ? ~pos->occupancy[WHITE] : ~pos->occupancy[BLACK]);

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);

                    if (GETBIT(((side == WHITE) ? pos->occupancy[BLACK] : pos->occupancy[WHITE]), target_square))
                        AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

                    POPBIT(attacks, target_square);
                }

                POPBIT(bitboard, source_square);
            }
        }

        if ((side == WHITE) ? piece == wR : piece == bR)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);
                attacks = get_rook_attacks(source_square, pos->occupancy[BOTH]) & ((side == WHITE) ? ~pos->occupancy[WHITE] : ~pos->occupancy[BLACK]);

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);

                    if (GETBIT(((side == WHITE) ? pos->occupancy[BLACK] : pos->occupancy[WHITE]), target_square))
                        AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

                    POPBIT(attacks, target_square);
                }

                POPBIT(bitboard, source_square);
            }
        }

        if ((side == WHITE) ? piece == wQ : piece == bQ)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);
                attacks = get_queen_attacks(source_square, pos->occupancy[BOTH]) & ((side == WHITE) ? ~pos->occupancy[WHITE] : ~pos->occupancy[BLACK]);

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);

                    if (GETBIT(((side == WHITE) ? pos->occupancy[BLACK] : pos->occupancy[WHITE]), target_square))
                         AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

                    POPBIT(attacks, target_square);
                }

                POPBIT(bitboard, source_square);
            }
        }

        if ((side == WHITE) ? piece == wK : piece == bK)
        {
            while (bitboard)
            {
                source_square = LSBINDEX(bitboard);

                attacks = king_attacks[source_square] & ((side == WHITE) ? ~pos->occupancy[WHITE] : ~pos->occupancy[BLACK]);

                while (attacks)
                {
                    target_square = LSBINDEX(attacks);

                    if (GETBIT(((side == WHITE) ? pos->occupancy[BLACK] : pos->occupancy[WHITE]), target_square))
                         AddMovee(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

                    POPBIT(attacks, target_square);
                }
                POPBIT(bitboard, source_square);
            }
        }
    }
}


