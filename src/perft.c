#include "defs.h"
#include "stdio.h"

#define MOVE(f,t,cap,prom,fl) ((f) | (t<<7) |(cap<<14) | (prom <<20) | (fl))

const int castlePerms[64]={
    13,15,15,15,12,15,15,14,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    7,15,15,15, 3,15,15,11

};

static inline void ClearPieces(const int sq,S_BOARD *pos){
    int pce=pos->pieces[sq];
    int col=pieceCol[pce];
    pos->pieces[sq]=EMPTY;

    CLRBIT(pos->occupancy[col],(sq));
    CLRBIT(pos->occupancy[BOTH],(sq));
    CLRBIT(pos->bitboards[pce],(sq));
}
static inline void AddPieces(const int sq,S_BOARD *pos,const int pce){
    int col=pieceCol[pce];
    pos->pieces[sq]=pce;

    SETBIT(pos->occupancy[col],(sq));
    SETBIT(pos->occupancy[BOTH],(sq));
    SETBIT(pos->bitboards[pce],(sq));
}
static inline void MovePieces(const int from,const int to,S_BOARD *pos){
    int pce=pos->pieces[from];
    int col=pieceCol[pce];

    pos->pieces[from]=EMPTY;
    pos->pieces[to]=pce;

    CLRBIT(pos->occupancy[col],(from));
    SETBIT(pos->occupancy[col],(to));
    CLRBIT(pos->occupancy[BOTH],(from));
    SETBIT(pos->occupancy[BOTH],(to));

    CLRBIT(pos->bitboards[pce],(from));
    SETBIT(pos->bitboards[pce],(to));
}
void takeMoves(S_BOARD *pos){

    pos->hisPly--;

    int move=pos->history[pos->hisPly].move;
    int from =FROMSQ(move);
    int to=TOSQ(move);

    pos->castleRights=pos->history[pos->hisPly].castleRights;
    pos->enPas=pos->history[pos->hisPly].enPas;

    pos->side ^= 1;

    if(move & MVFLAGEP){
        if(pos->side==WHITE){
            AddPieces(to-8,pos,bP);
        }else if(pos->side==BLACK){
            AddPieces(to+8,pos,wP);
        }
    }else if(move & MVFLAGCA){
        switch(to){
            case C1: MovePieces(D1,A1,pos);break;
            case C8: MovePieces(D8,A8,pos);break;
            case G1: MovePieces(F1,H1,pos);break;
            case G8: MovePieces(F8,H8,pos);break;

        }
    }

    MovePieces(to,from,pos);

    if(pieceKing[pos->pieces[from]]){
        pos->kingSq[pos->side]=from;
    }

    int cap=CAPTURED(move);
    if(cap != EMPTY){
        AddPieces(to,pos,cap);
    }

    int ptom=PROMOTED(move);
    if(ptom != EMPTY){
        ClearPieces(from,pos);
        AddPieces(from,pos,(pieceCol[ptom]==WHITE ? wP:bP));
    }
    //update occupancy for both
    //pos->occupancy[BOTH] = (pos->occupancy[WHITE] | pos->occupancy[BLACK]);
}

static inline int makeMoves(S_BOARD *pos,int move){

    int from=FROMSQ(move);
    int to=TOSQ(move);
    int side=pos->side;

    pos->history[pos->hisPly].posKey=pos->posKey;

    if(move & MVFLAGEP){
        if(side==WHITE){
            ClearPieces(to-8,pos);
        }else{
            ClearPieces(to+8,pos);
        }
    }else if(move & MVFLAGCA){
        switch(to){
            case C1:
                MovePieces(A1,D1,pos);
                break;
            case G1:
                MovePieces(H1,F1,pos);
                break;
            case C8:
                MovePieces(A8,D8,pos);
                break;
            case G8:
                MovePieces(H8,F8,pos);
                break;

        }
    }

    pos->history[pos->hisPly].move=move;
    pos->history[pos->hisPly].enPas=pos->enPas;
    pos->history[pos->hisPly].castleRights=pos->castleRights;
    pos->castleRights &= castlePerms[from];
    pos->castleRights &= castlePerms[to];
    pos->enPas=NO_SQ;

    if(CAPTURED(move) != EMPTY){
        ClearPieces(to,pos);
    }

    pos->hisPly++;

    if(piecePawn[pos->pieces[from]]){
        if(move & MVFLAGPS){
            if(side==WHITE){
                pos->enPas=from+8;
            }else{
                pos->enPas=from-8;
            }
        }
    }

    MovePieces(from,to,pos);

    int promoted=PROMOTED(move);
    if(promoted != EMPTY){
        ClearPieces(to,pos);
        AddPieces(to,pos,promoted);
    }

    if(pieceKing[pos->pieces[to]]){
        pos->kingSq[side]=to;
    }

    pos->side ^=1;

    //update occupancy for both
    //pos->occupancy[BOTH] = (pos->occupancy[WHITE] | pos->occupancy[BLACK]);

    if(is_square_attacked_BB((pos->kingSq[side]),pos->side,pos)){
        takeMoves(pos);
        return FALSE;
    }

    return TRUE;
}

static inline void AddMove(const S_BOARD *pos, int move,S_MOVELIST *list){
    list->moves[list->count].move =move;
    list->count++;
}
static inline void AddWhitePawnCaptureMoves(const S_BOARD *pos,const int from,const int to,const int cap,S_MOVELIST *list){

    if(ranksBoard[from]==RANK_7){
        AddMove(pos,MOVE(from,to,cap,wQ,0),list);
        AddMove(pos,MOVE(from,to,cap,wR,0),list);
        AddMove(pos,MOVE(from,to,cap,wB,0),list);
        AddMove(pos,MOVE(from,to,cap,wN,0),list);
    }
    else{
        AddMove(pos,MOVE(from,to,cap,EMPTY,0),list);
    }
}
static inline void AddWhitePawnMoves(const S_BOARD *pos,const int from,const int to,S_MOVELIST *list){

    if(ranksBoard[from]==RANK_7){
        AddMove(pos,MOVE(from,to,EMPTY,wQ,0),list);
        AddMove(pos,MOVE(from,to,EMPTY,wR,0),list);
        AddMove(pos,MOVE(from,to,EMPTY,wB,0),list);
        AddMove(pos,MOVE(from,to,EMPTY,wN,0),list);
    }
    else{
        AddMove(pos,MOVE(from,to,EMPTY,EMPTY,0),list);
    }
}
static inline void AddBlackPawnCaptureMoves(const S_BOARD *pos,const int from,const int to,const int cap,S_MOVELIST *list){

    if(ranksBoard[from]==RANK_2){
        AddMove(pos,MOVE(from,to,cap,bQ,0),list);
        AddMove(pos,MOVE(from,to,cap,bR,0),list);
        AddMove(pos,MOVE(from,to,cap,bB,0),list);
        AddMove(pos,MOVE(from,to,cap,bN,0),list);
    }
    else{
        AddMove(pos,MOVE(from,to,cap,EMPTY,0),list);
    }
}
static inline void AddBlackPawnMoves(const S_BOARD *pos,const int from,const int to,S_MOVELIST *list){


    if(ranksBoard[from]==RANK_2){
        AddMove(pos,MOVE(from,to,EMPTY,bQ,0),list);
        AddMove(pos,MOVE(from,to,EMPTY,bR,0),list);
        AddMove(pos,MOVE(from,to,EMPTY,bB,0),list);
        AddMove(pos,MOVE(from,to,EMPTY,bN,0),list);
    }
    else{
        AddMove(pos,MOVE(from,to,EMPTY,EMPTY,0),list);
    }
}
static inline void GenerateAllMovess(const S_BOARD *pos,S_MOVELIST *list){

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

                    // generate quite pawn moves
                    if ((!GETBIT(pos->occupancy[BOTH], target_square)))
                    {
                        AddWhitePawnMoves(pos,source_square,target_square,list);

                        // two squares ahead pawn move
                        if ((source_square >= A2 && source_square <= H2) && !GETBIT(pos->occupancy[BOTH], (target_square + 8)))
                            AddMove(pos,MOVE(source_square,(target_square+8),0,0,MVFLAGPS),list);
                    }

                    // init pawn attacks bitboard
                    attacks = pawn_attacks[side][source_square] & pos->occupancy[BLACK];

                    // generate pawn captures
                    while (attacks)
                    {
                        target_square = LSBINDEX(attacks);

                        AddWhitePawnCaptureMoves(pos,source_square,target_square,pos->pieces[target_square],list);

                        // pop ls1b of the pawn attacks
                        POPBIT(attacks, target_square);
                    }

                    // generate enpassant captures
                    if (pos->enPas != NO_SQ)
                    {
                        U64 enpassant_attacks = pawn_attacks[side][source_square] & (1ULL << pos->enPas);

                        if (enpassant_attacks)
                        {
                            // init enpassant capture target square
                            int target_enpassant = LSBINDEX(enpassant_attacks);
                            AddMove(pos,MOVE(source_square,target_enpassant,0,0,MVFLAGEP),list);
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
                            AddMove(pos,MOVE(E1,G1,0,0,MVFLAGCA),list);
                    }
                }

                if (pos->castleRights & WQCA)
                {
                    if (!GETBIT(pos->occupancy[BOTH], D1) && !GETBIT(pos->occupancy[BOTH], C1) && !GETBIT(pos->occupancy[BOTH], B1))
                    {
                        if (!is_square_attacked_BB(E1, BLACK,pos) && !is_square_attacked_BB(D1, BLACK,pos))
                            AddMove(pos,MOVE(E1,C1,0,0,MVFLAGCA),list);
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
                        AddBlackPawnMoves(pos,source_square,target_square,list);

                        if ((source_square >= A7 && source_square <= H7) && !GETBIT(pos->occupancy[BOTH], (target_square - 8)))
                            AddMove(pos,MOVE(source_square,(target_square-8),0,0,MVFLAGPS),list);
                    }

                    attacks = pawn_attacks[side][source_square] & pos->occupancy[WHITE];

                    while (attacks)
                    {
                        target_square = LSBINDEX(attacks);

                        AddBlackPawnCaptureMoves(pos,source_square,target_square,pos->pieces[target_square],list);

                        POPBIT(attacks, target_square);
                    }

                    if (pos->enPas != NO_SQ)
                    {
                        U64 enpassant_attacks = pawn_attacks[side][source_square] & (1ULL << pos->enPas);

                        if (enpassant_attacks)
                        {
                            int target_enpassant = LSBINDEX(enpassant_attacks);
                            AddMove(pos,MOVE(source_square,target_enpassant,0,0,MVFLAGEP),list);
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
                            AddMove(pos,MOVE(E8,G8,0,0,MVFLAGCA),list);
                    }
                }

                if (pos->castleRights & BQCA)
                {
                    if (!GETBIT(pos->occupancy[BOTH], D8) && !GETBIT(pos->occupancy[BOTH], C8) && !GETBIT(pos->occupancy[BOTH], B8))
                    {
                        if (!is_square_attacked_BB(E8, WHITE,pos) && !is_square_attacked_BB(D8, WHITE,pos))
                            AddMove(pos,MOVE(E8,C8,0,0,MVFLAGCA),list);
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
                        AddMove(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                        AddMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

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
                        AddMove(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                        AddMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

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
                        AddMove(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                        AddMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

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
                        AddMove(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                         AddMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

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
                        AddMove(pos,MOVE(source_square,target_square,0,0,0),list);

                    else
                         AddMove(pos,MOVE(source_square,target_square,pos->pieces[target_square],0,0),list);

                    POPBIT(attacks, target_square);
                }

                POPBIT(bitboard, source_square);
            }
        }
    }
}

U64 leafNodes;
static inline void Perft(int depth,S_BOARD *pos){

    if(depth==0){
        leafNodes++;
        return;
    }

    S_MOVELIST list[1];

    GenerateAllMoves(pos,list);

    int moveNum=0;
    for(moveNum=0;moveNum<list->count;++moveNum){
        if(!makeMove(pos,list->moves[moveNum].move)){
            continue;
        }

        Perft(depth-1,pos);
        takeMove(pos);
    }

    return;
}
void PerftTest(int depth,S_BOARD *pos){

    PrintBoard(pos);

    printf("\nStarting Perft Test to depth: %d\n",depth);

    leafNodes=0;
    int start=getTimeMs();
    S_MOVELIST list[1];
    GenerateAllMoves(pos,list);

    int move=0;
    int moveNum=0;
    for(moveNum=0;moveNum<list->count;++moveNum){
        move=list->moves[moveNum].move;
        if(!makeMove(pos,move)){
            continue;
        }

        U64 cumnodes=leafNodes;
        Perft(depth-1,pos);

        takeMove(pos);
        U64 oldnodes=leafNodes-cumnodes;

        printf(" %d : %s : %llu\n",moveNum+1,PrMove(move),oldnodes);
    }

    printf("Nodes Searched: %llu nodes, Time: %dms\n",leafNodes,getTimeMs()-start);
    //long nps=(leafNodes/(getTimeMs()-start))*1000;
    //printf("Perft Speed: %ld M nodes/s\n",nps/1000000);

    return;
}




static inline void Bench(int depth,S_BOARD *pos){

    if(depth==0){
        leafNodes++;
        return;
    }

    S_MOVELIST list[1];

    GenerateAllMovess(pos,list);

    int moveNum=0;
    for(moveNum=0;moveNum<list->count;++moveNum){
        if(!makeMoves(pos,list->moves[moveNum].move)){
            continue;
        }

        Bench(depth-1,pos);
        takeMoves(pos);
    }

    return;
}
void BenchTest(int depth,S_BOARD *pos){

    //PrintBoard(pos);

    printf("\nStarting Bench Test on current position to depth: %d\n",depth);

    leafNodes=0;
    int start;
    S_MOVELIST list[1];
    GenerateAllMovess(pos,list);

    int move=0;
    int moveNum=0;
    for(int depth1=1;depth1<=depth;++depth1){
        leafNodes=0;
        start=getTimeMs();
        for(moveNum=0;moveNum<list->count;++moveNum){
            move=list->moves[moveNum].move;
            if(!makeMoves(pos,move)){
                continue;
            }

            U64 cumnodes=leafNodes;
            Bench(depth1-1,pos);

            takeMoves(pos);
            U64 oldnodes=leafNodes-cumnodes;

            //printf(" %d : %s : %llu\n",moveNum+1,PrMove(move),oldnodes);
        }
        printf("Depth %d: %llu nodes, Time: %dms\n",depth1,leafNodes,getTimeMs()-start);
    }

    //printf("Nodes Searched: %llu nodes, Time: %dms\n",leafNodes,getTimeMs()-start);
    //long nps=(leafNodes/(getTimeMs()-start))*1000;
    //printf("Perft Speed: %ld M nodes/s\n",nps/1000000);

    return;
}

