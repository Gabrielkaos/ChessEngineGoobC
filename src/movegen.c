#include "stdio.h"
#include "defs.h"

#define MOVE(f,t,cap,prom,fl) ((f) | (t<<7) |(cap<<14) | (prom <<20) | (fl))

const int victimScore[13]={0,100,200,300,400,500,600,100,200,300,400,500,600};
static int mvvLvaScore[13][13];

void InitMvvLva(){
    int attacker;
    int victim;

    for(attacker=wP;attacker<=bK;++attacker){
        for(victim=wP;victim<=bK;++victim){
            mvvLvaScore[victim][attacker]=victimScore[victim]+6 -(victimScore[attacker]/100);
        }
    }
}

int MoveExists(S_BOARD *pos,const int move){

    S_MOVELIST list[1];
    GenerateAllMoves(pos,list);

    int moveNum=0;
    for(moveNum=0;moveNum<list->count;++moveNum){

        if(!makeMove(pos,list->moves[moveNum].move)){
            continue;
        }
        takeMove(pos);
        if(list->moves[moveNum].move==move){
            return TRUE;
        }
    }

    return FALSE;
}

static inline void AddQuietMove(const S_BOARD *pos, int move,S_MOVELIST *list){

    list->moves[list->count].move =move;

    if(pos->searchKillers[0][pos->ply]==move){
        list->moves[list->count].score =900000;
    }else if(pos->searchKillers[1][pos->ply]==move){
        list->moves[list->count].score =800000;
    }else{
        list->moves[list->count].score =pos->searchHistory[pos->pieces[FROMSQ(move)]][TOSQ(move)];
    }
    list->count++;
}
static inline void AddCaptureMove(const S_BOARD *pos, int move,S_MOVELIST *list){
    list->moves[list->count].move =move;
    list->moves[list->count].score =mvvLvaScore[CAPTURED(move)][pos->pieces[FROMSQ(move)]]+1000000;
    list->count++;
}
static inline void AddEnPasMove(const S_BOARD *pos, int move,S_MOVELIST *list){
    list->moves[list->count].move =move;
    list->moves[list->count].score =105+1000000;
    list->count++;
}

static inline void AddWhitePawnCaptureMove(const S_BOARD *pos,const int from,const int to,const int cap,S_MOVELIST *list){

    if(ranksBoard[from]==RANK_7){
        AddCaptureMove(pos,MOVE(from,to,cap,wQ,0),list);
        AddCaptureMove(pos,MOVE(from,to,cap,wR,0),list);
        AddCaptureMove(pos,MOVE(from,to,cap,wB,0),list);
        AddCaptureMove(pos,MOVE(from,to,cap,wN,0),list);
    }
    else{
        AddCaptureMove(pos,MOVE(from,to,cap,EMPTY,0),list);
    }
}
static inline void AddWhitePawnMove(const S_BOARD *pos,const int from,const int to,S_MOVELIST *list){

    if(ranksBoard[from]==RANK_7){
        AddQuietMove(pos,MOVE(from,to,EMPTY,wQ,0),list);
        AddQuietMove(pos,MOVE(from,to,EMPTY,wR,0),list);
        AddQuietMove(pos,MOVE(from,to,EMPTY,wB,0),list);
        AddQuietMove(pos,MOVE(from,to,EMPTY,wN,0),list);
    }
    else{
        AddQuietMove(pos,MOVE(from,to,EMPTY,EMPTY,0),list);
    }
}

static inline void AddBlackPawnCaptureMove(const S_BOARD *pos,const int from,const int to,const int cap,S_MOVELIST *list){

    if(ranksBoard[from]==RANK_2){
        AddCaptureMove(pos,MOVE(from,to,cap,bQ,0),list);
        AddCaptureMove(pos,MOVE(from,to,cap,bR,0),list);
        AddCaptureMove(pos,MOVE(from,to,cap,bB,0),list);
        AddCaptureMove(pos,MOVE(from,to,cap,bN,0),list);
    }
    else{
        AddCaptureMove(pos,MOVE(from,to,cap,EMPTY,0),list);
    }
}
static inline void AddBlackPawnMove(const S_BOARD *pos,const int from,const int to,S_MOVELIST *list){

    if(ranksBoard[from]==RANK_2){
        AddQuietMove(pos,MOVE(from,to,EMPTY,bQ,0),list);
        AddQuietMove(pos,MOVE(from,to,EMPTY,bR,0),list);
        AddQuietMove(pos,MOVE(from,to,EMPTY,bB,0),list);
        AddQuietMove(pos,MOVE(from,to,EMPTY,bN,0),list);
    }
    else{
        AddQuietMove(pos,MOVE(from,to,EMPTY,EMPTY,0),list);
    }
}

//bitboard based move generator
void GenerateAllMoves(const S_BOARD *pos,S_MOVELIST *list){

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
                            AddQuietMove(pos,MOVE(source_square,(target_square+8),0,0,MVFLAGPS),list);
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
                            AddEnPasMove(pos,MOVE(source_square,target_enpassant,0,0,MVFLAGEP),list);
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
                            AddQuietMove(pos,MOVE(E1,G1,0,0,MVFLAGCA),list);
                    }
                }

                if (pos->castleRights & WQCA)
                {
                    if (!GETBIT(pos->occupancy[BOTH], D1) && !GETBIT(pos->occupancy[BOTH], C1) && !GETBIT(pos->occupancy[BOTH], B1))
                    {
                        if (!is_square_attacked_BB(E1, BLACK,pos) && !is_square_attacked_BB(D1, BLACK,pos))
                            AddQuietMove(pos,MOVE(E1,C1,0,0,MVFLAGCA),list);
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
                            AddQuietMove(pos,MOVE(source_square,(target_square-8),0,0,MVFLAGPS),list);
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
                            AddEnPasMove(pos,MOVE(source_square,target_enpassant,0,0,MVFLAGEP),list);
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
                            AddQuietMove(pos,MOVE(E8,G8,0,0,MVFLAGCA),list);
                    }
                }

                if (pos->castleRights & BQCA)
                {
                    if (!GETBIT(pos->occupancy[BOTH], D8) && !GETBIT(pos->occupancy[BOTH], C8) && !GETBIT(pos->occupancy[BOTH], B8))
                    {
                        if (!is_square_attacked_BB(E8, WHITE,pos) && !is_square_attacked_BB(D8, WHITE,pos))
                            AddQuietMove(pos,MOVE(E8,C8,0,0,MVFLAGCA),list);
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
                        AddQuietMove(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                        AddCaptureMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

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
                        AddQuietMove(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                        AddCaptureMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

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
                        AddQuietMove(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                        AddCaptureMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

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
                        AddQuietMove(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                         AddCaptureMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

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
                        AddQuietMove(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                         AddCaptureMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

                    POPBIT(attacks, target_square);
                }

                POPBIT(bitboard, source_square);
            }
        }
    }
}
void GenerateAllCaptures(const S_BOARD *pos,S_MOVELIST *list){
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
                            AddEnPasMove(pos,MOVE(source_square,target_enpassant,0,0,MVFLAGEP),list);
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
                            AddEnPasMove(pos,MOVE(source_square,target_enpassant,0,0,MVFLAGEP),list);
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
                        AddCaptureMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

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
                        AddCaptureMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

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
                        AddCaptureMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

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
                         AddCaptureMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

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
                         AddCaptureMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

                    POPBIT(attacks, target_square);
                }
                POPBIT(bitboard, source_square);
            }
        }
    }
}



