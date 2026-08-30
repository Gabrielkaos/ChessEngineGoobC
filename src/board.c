#include "stdio.h"
#include "string.h"
#include "inttypes.h"
#include "bitboards.h"
#include "hashkeys.h"
#include "evaluate.h"
#include "io.h"
#include "board.h"
#include "attacks.h"
#include "nnue_loader.h"

void resetContinuationTable(S_BOARD *pos){
    memset(pos->shared->continuation,0,sizeof(ContinuationTable));
    memset(pos->shared->chist,0,sizeof(CaptureHistoryTable));
    memset(pos->shared->histtable,0,sizeof(HistoryTable));
    memset(pos->shared->cmtable,0,sizeof(CounterMoveTable));
    clearCorrectionHistory(pos);
    pos->shared->ttMoveHistory = 0;
}

void initStacks(S_BOARD *pos){
    int index;
    for(index=0;index<MAXDEPTH;++index){
        pos->search->eval_stack[index]=0;
        pos->search->moveStack[index] =NOMOVE;
        pos->search->pieceStack[index]=0;
        pos->search->reduction_stack[index]=0;
    }
    pos->shared->ttMoveHistory = 0;
}

int getGamePhase(const S_BOARD *pos){
    int gamePhase = 24 - 4 * COUNTBIT(pos->bitboards[wQ] | pos->bitboards[bQ])
                       - 2 * COUNTBIT(pos->bitboards[wR] | pos->bitboards[bR])
                       - 1 * COUNTBIT(pos->bitboards[wN] | pos->bitboards[bN] |
                                      pos->bitboards[wB] | pos->bitboards[bB]);

    gamePhase = gamePhase < 0 ? 0:gamePhase;
    return (gamePhase*256+12)/24;
}

int checkBoard(const S_BOARD *pos){
    int t_piece;

    //check number of pieces in arrays and bitboards
    int pce_count=0;
    for (t_piece=wP;t_piece<=bK;++t_piece){
        pce_count+=COUNTBIT(pos->bitboards[t_piece]);
    }
    ASSERT(COUNTBIT(pos->occupancy[BOTH])==pce_count);

    //check bitboard occupancy
    ASSERT((pos->occupancy[WHITE] & pos->occupancy[BLACK])==0);
    ASSERT((pos->occupancy[WHITE] | pos->occupancy[BLACK])==pos->occupancy[BOTH]);

    //check side
    ASSERT(pos->side >= WHITE && pos->side < BOTH);
    //check enPas
    if (pos->enPas != NO_SQ) ASSERT(ranksBoard[pos->enPas]==RANK_3 || ranksBoard[pos->enPas]==RANK_6);
    //check castleRights
    ASSERT(pos->castleRights >= 0 && pos->castleRights <=15);
    //check posKey and pawnPosKey
    ASSERT(pos->posKey==GeneratePosKey(pos));
    ASSERT(pos->pkHash==GeneratePKHash(pos));
    ASSERT(pos->npHash[WHITE]==GenerateNonPawnHash(pos,WHITE));
    ASSERT(pos->npHash[BLACK]==GenerateNonPawnHash(pos,BLACK));
    ASSERT(pos->minorHash==GenerateMinorHash(pos));

    //avoid variants make engine play chess960 or just standard chess
    //pawns
    ASSERT(COUNTBIT(pos->bitboards[wP]) <= 8);
    ASSERT(COUNTBIT(pos->bitboards[bP]) <= 8);

    //rooks
    ASSERT(COUNTBIT(pos->bitboards[wR]) <=10);
    ASSERT(COUNTBIT(pos->bitboards[bR]) <=10);

    //knights
    ASSERT(COUNTBIT(pos->bitboards[wN]) <=10);
    ASSERT(COUNTBIT(pos->bitboards[bN]) <=10);

    //bishops
    ASSERT(COUNTBIT(pos->bitboards[wB]) <=10);
    ASSERT(COUNTBIT(pos->bitboards[bB]) <=10);

    //queens
    ASSERT(COUNTBIT(pos->bitboards[wQ]) <=9);
    ASSERT(COUNTBIT(pos->bitboards[bQ]) <=9);

    //kings
    ASSERT(COUNTBIT(pos->bitboards[wK])==1);
    ASSERT(COUNTBIT(pos->bitboards[bK])==1);

    return TRUE;
}

void MirrorBoard(S_BOARD *pos){
    int tempPiecesArray[64];
    int tempSide=pos->side^1;
    int swapPieces[13]={EMPTY, bP, bN, bB, bR, bQ, bK,wP, wN, wB, wR, wQ, wK};
    int tempcasteRights=0;
    int tempEnPass=NO_SQ;

    int sq;
    int tp;

    if(pos->castleRights & WKCA) tempcasteRights |= BKCA;
    if(pos->castleRights & WQCA) tempcasteRights |= BQCA;
    if(pos->castleRights & BKCA) tempcasteRights |= WKCA;
    if(pos->castleRights & BQCA) tempcasteRights |= WQCA;

    if(pos->enPas != NO_SQ){
        tempEnPass=Mirror64[pos->enPas];
    }

    for(sq=0;sq<64;sq++){
        tempPiecesArray[sq]=pos->pieces[Mirror64[sq]];
    }

    ResetBoard(pos);

    for(sq=0;sq<64;sq++){

        tp=swapPieces[tempPiecesArray[sq]];
        pos->pieces[sq]=tp;

    }

    pos->side=tempSide;
    pos->castleRights=tempcasteRights;
    pos->enPas=tempEnPass;

    pos->posKey=GeneratePosKey(pos);
    pos->pkHash=GeneratePKHash(pos);
    pos->npHash[WHITE]=GenerateNonPawnHash(pos,WHITE);
    pos->npHash[BLACK]=GenerateNonPawnHash(pos,BLACK);
    pos->minorHash=GenerateMinorHash(pos);

    updateListMaterial(pos);

}

void updateListMaterial(S_BOARD *pos){
    int piece,sq,index,color;

    for(index=0;index<BOARD_NUMS_SQ;++index){
        sq=index;
        piece=pos->pieces[index];
        if(piece != EMPTY){
            color=pieceCol[piece];

            //psqtmat
            pos->psqtmat += PSQTMATTABLE[piece][sq];

            SETBIT(pos->occupancy[color],sq);
            SETBIT(pos->bitboards[piece],sq);
        }
    }
    //update occupancy for both
    pos->occupancy[BOTH] = (pos->occupancy[WHITE] | pos->occupancy[BLACK]);

    if (!tuneMode) nnue_refresh_accumulator(pos);
}

int ParseFEN(char *fen ,S_BOARD *pos){

    int rank=RANK_8;
    int file=FILE_A;
    int piece=0;
    int i=0;

    ResetBoard(pos);

    //-------- board field: exactly 8 '/'-separated ranks, each summing to 8 files --------
    //the loop stops at the field terminator so a malformed board section can
    //never bleed into the side/castling/ep fields (that used to place pieces
    //on rank 1 and compute a wild out-of-range enPas square -> corrupt search)
    while(*fen && *fen != ' '){
        switch(*fen){
            case '/':
                if(file != 8){ printf("FEN Not Valid \n"); return -1; }
                rank--;
                file=FILE_A;
                if(rank < RANK_1){ printf("FEN Not Valid \n"); return -1; }
                fen++;
                continue;

            case 'p':piece=bP;break;
            case 'r':piece=bR;break;
            case 'n':piece=bN;break;
            case 'b':piece=bB;break;
            case 'q':piece=bQ;break;
            case 'k':piece=bK;break;
            case 'P':piece=wP;break;
            case 'R':piece=wR;break;
            case 'N':piece=wN;break;
            case 'B':piece=wB;break;
            case 'Q':piece=wQ;break;
            case 'K':piece=wK;break;

            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
                piece=EMPTY;
                file+=*fen-'0';
                break;

            default:
                printf("FEN Not Valid \n");
                return -1;
        }

        if(*fen >= '1' && *fen <= '8'){
            //digit: file already advanced by the skip count
        }else{
            if(file > FILE_H){ printf("FEN Not Valid \n"); return -1; }
            pos->pieces[rank*8+file]=piece;
            file++;
        }
        if(file > 8){ printf("FEN Not Valid \n"); return -1; }
        fen++;
    }

    if(rank != RANK_1 || file != 8){ printf("FEN Not Valid \n"); return -1; }

    //-------- side to move --------
    while(*fen==' ') fen++;
    if(*fen=='w')       pos->side=WHITE;
    else if(*fen=='b')  pos->side=BLACK;
    else{ printf("FEN Not Valid \n"); return -1; }
    fen++;

    //-------- castling rights (lenient: unknown chars ignored) --------
    while(*fen==' ') fen++;
    while(*fen && *fen != ' '){
        switch(*fen){
            case 'K':pos->castleRights |= WKCA;break;
            case 'Q':pos->castleRights |= WQCA;break;
            case 'k':pos->castleRights |= BKCA;break;
            case 'q':pos->castleRights |= BQCA;break;
            default:
                break;
        }
        fen++;
    }

    //-------- en passant square (must be '-' or a real rank-3/rank-6 square) --------
    while(*fen==' ') fen++;
    if(*fen!='-' && *fen!='\0'){
        int epFile=fen[0]-'a';
        int epRank=fen[1]-'1';
        if(epFile<FILE_A || epFile>FILE_H || epRank<RANK_1 || epRank>RANK_8 ||
           (ranksBoard[FRtoSQ(epFile,epRank)]!=RANK_3 && ranksBoard[FRtoSQ(epFile,epRank)]!=RANK_6)){
            printf("FEN Not Valid \n");
            return -1;
        }
        pos->enPas=FRtoSQ(epFile,epRank);
        fen+=2;
    }else{
        pos->enPas=NO_SQ;
        if(*fen=='-') fen++;
    }

    //-------- fifty move counter / fullmove number --------
    pos->fiftyMove=0;
    int fullmove=0;
    sscanf(fen, " %d %d", &pos->fiftyMove, &fullmove);
    if(pos->fiftyMove < 0) pos->fiftyMove=0;

    updateListMaterial(pos);

    //-------- material sanity: one king per side, no pawns on back ranks --------
    int kings[2]={0,0};
    for(i=0;i<64;i++){
        int pce=pos->pieces[i];
        if(pce==EMPTY) continue;
        if(pieceKing[pce]) kings[pieceCol[pce]]++;
        if(piecePawn[pce] && (ranksBoard[i]==RANK_1 || ranksBoard[i]==RANK_8)){
            printf("FEN Not Valid \n");
            return -1;
        }
    }
    if(kings[WHITE]!=1 || kings[BLACK]!=1){ printf("FEN Not Valid \n"); return -1; }

    pos->pkHash=GeneratePKHash(pos);
    pos->npHash[WHITE]=GenerateNonPawnHash(pos,WHITE);
    pos->npHash[BLACK]=GenerateNonPawnHash(pos,BLACK);
    pos->minorHash=GenerateMinorHash(pos);
    pos->posKey=GeneratePosKey(pos);

    return 0;
}

void ResetBoard(S_BOARD *pos){
    int index=0;

    pos->psqtmat = 0;

     for(index=0;index<13;++index){
         pos->bitboards[index]=0ULL;
     }

    //making them empty in 64 board
    for(index=0;index<64;++index){
        pos->pieces[index]=EMPTY;
    }

    //making the pieces value 0 in bitboards
    for(index=0;index<3;++index){
        pos->occupancy[index]=0ULL;
    }

    pos->side=BOTH;
    pos->enPas=NO_SQ;
    pos->fiftyMove=0;
    pos->pliesFromNull=0;

    pos->castleRights=0;
    pos->ply=0;
    pos->hisPly=0;

    pos->posKey=0ULL;
    pos->pkHash=0ULL;
    pos->npHash[WHITE]=0ULL;
    pos->npHash[BLACK]=0ULL;
    pos->minorHash=0ULL;
}

void PrintBoard(const S_BOARD *pos){

    char fen[300];
    printFen(pos,fen);

    int sq,file,rank,piece;

    printf("\n +---+---+---+---+---+---+---+---+\n");

    for(rank=RANK_8;rank>=RANK_1;rank--){
        //printf("  %d ",rank+1);
        for(file=FILE_A;file<=FILE_H;file++){
            sq=FRtoSQ(file,rank);
            piece=pos->pieces[sq];
            printf(" | %c",pieceChar[piece]);
        }
        //printf("\n");
        printf(" | %d\n +---+---+---+---+---+---+---+---+\n",rank+1);
    }
    printf("   a   b   c   d   e   f   g   h\n\n Fen: %s\n",fen);
    printf(" Key: %"PRIu64"\n",pos->posKey);

    U64 checkers=attackersToKingSq(pos,pos->side);

    printf(" Checkers: ");
    while(checkers){
        int sq=poplsb(&checkers);
        printf("%s,",PrSq(sq));
    }
    printf("\n");
}