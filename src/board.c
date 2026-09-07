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
        pos->search->nnue_accumulators[index].computed[WHITE]=0;
        pos->search->nnue_accumulators[index].computed[BLACK]=0;
    }
    pos->shared->ttMoveHistory = 0;
}

int getGamePhase(const S_BOARD *pos){
    int gamePhase = 24 - 4 * COUNTBIT(pos->byTypeBB[QUEEN])
                       - 2 * COUNTBIT(pos->byTypeBB[ROOK])
                       - 1 * COUNTBIT(pos->byTypeBB[KNIGHT] | pos->byTypeBB[BISHOP]);

    gamePhase = gamePhase < 0 ? 0 : gamePhase;
    return (gamePhase * 256 + 12) / 24;
}

int checkBoard(const S_BOARD *pos){
    int pt;

    //check number of pieces in arrays and bitboards
    int pce_count=0;
    for (pt = PAWN; pt <= KING; ++pt){
        pce_count += COUNTBIT(pos->byTypeBB[pt]);
    }
    ASSERT(COUNTBIT(pos->byTypeBB[ALL_PIECES]) == pce_count);

    //check bitboard occupancy
    ASSERT((pos->byColorBB[WHITE] & pos->byColorBB[BLACK]) == 0);
    ASSERT((pos->byColorBB[WHITE] | pos->byColorBB[BLACK]) == pos->byTypeBB[ALL_PIECES]);

    //check side
    ASSERT(pos->side >= WHITE && pos->side < BOTH);
    //check enPas
    if (pos->st->enPas != NO_SQ) ASSERT(RANK_OF(pos->st->enPas)==RANK_3 || RANK_OF(pos->st->enPas)==RANK_6);
    //check castleRights
    ASSERT(pos->st->castleRights >= 0 && pos->st->castleRights <=15);
    //check posKey and pawnPosKey
    ASSERT(pos->st->posKey==GeneratePosKey(pos));
    ASSERT(pos->st->pkHash==GeneratePKHash(pos));
    ASSERT(pos->st->npHash[WHITE]==GenerateNonPawnHash(pos,WHITE));
    ASSERT(pos->st->npHash[BLACK]==GenerateNonPawnHash(pos,BLACK));
    ASSERT(pos->st->minorHash==GenerateMinorHash(pos));

    //avoid variants make engine play chess960 or just standard chess
    //pawns
    ASSERT(COUNTBIT(pieces_cp(pos, WHITE, PAWN)) <= 8);
    ASSERT(COUNTBIT(pieces_cp(pos, BLACK, PAWN)) <= 8);

    //rooks
    ASSERT(COUNTBIT(pieces_cp(pos, WHITE, ROOK)) <= 10);
    ASSERT(COUNTBIT(pieces_cp(pos, BLACK, ROOK)) <= 10);

    //knights
    ASSERT(COUNTBIT(pieces_cp(pos, WHITE, KNIGHT)) <= 10);
    ASSERT(COUNTBIT(pieces_cp(pos, BLACK, KNIGHT)) <= 10);

    //bishops
    ASSERT(COUNTBIT(pieces_cp(pos, WHITE, BISHOP)) <= 10);
    ASSERT(COUNTBIT(pieces_cp(pos, BLACK, BISHOP)) <= 10);

    //queens
    ASSERT(COUNTBIT(pieces_cp(pos, WHITE, QUEEN)) <= 9);
    ASSERT(COUNTBIT(pieces_cp(pos, BLACK, QUEEN)) <= 9);

    //kings
    ASSERT(COUNTBIT(pieces_cp(pos, WHITE, KING)) == 1);
    ASSERT(COUNTBIT(pieces_cp(pos, BLACK, KING)) == 1);

    return TRUE;
}

void MirrorBoard(S_BOARD *pos){
    int tempPiecesArray[64];
    int tempSide=pos->side^1;
    int tempcasteRights=0;
    int tempEnPass=NO_SQ;

    int sq;

    if(pos->st->castleRights & WKCA) tempcasteRights |= BKCA;
    if(pos->st->castleRights & WQCA) tempcasteRights |= BQCA;
    if(pos->st->castleRights & BKCA) tempcasteRights |= WKCA;
    if(pos->st->castleRights & BQCA) tempcasteRights |= WQCA;

    if(pos->st->enPas != NO_SQ){
        tempEnPass=MIRROR64(pos->st->enPas);
    }

    for(sq=0;sq<64;sq++){
        tempPiecesArray[sq]=pos->pieces[MIRROR64(sq)];
    }

    ResetBoard(pos);

    for(sq=0;sq<64;sq++){
        int pce = tempPiecesArray[sq];
        pos->pieces[sq] = pce ? (pce ^ 8) : EMPTY;
    }

    pos->side=tempSide;
    pos->st->castleRights=tempcasteRights;
    pos->st->enPas=tempEnPass;

    pos->st->posKey=GeneratePosKey(pos);
    pos->st->pkHash=GeneratePKHash(pos);
    pos->st->npHash[WHITE]=GenerateNonPawnHash(pos,WHITE);
    pos->st->npHash[BLACK]=GenerateNonPawnHash(pos,BLACK);
    pos->st->minorHash=GenerateMinorHash(pos);

    updateListMaterial(pos);
    set_check_info(pos);
}

void updateListMaterial(S_BOARD *pos){
    int piece,sq,index,color,pt;

    pos->st->psqtmat = 0;

    for(index=0;index<BOARD_NUMS_SQ;++index){
        sq=index;
        piece=pos->pieces[index];
        if(piece != EMPTY){
            color=COLOR_OF(piece);
            pt=TYPE_OF(piece);

            //psqtmat
            pos->st->psqtmat += PSQTMATTABLE[piece][sq];

            U64 mask = 1ULL << sq;
            pos->byColorBB[color] |= mask;
            pos->byTypeBB[pt] |= mask;
        }
    }
    //update occupancy for both
    pos->byTypeBB[ALL_PIECES] = (pos->byColorBB[WHITE] | pos->byColorBB[BLACK]);

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
            case 'K':pos->st->castleRights |= WKCA;break;
            case 'Q':pos->st->castleRights |= WQCA;break;
            case 'k':pos->st->castleRights |= BKCA;break;
            case 'q':pos->st->castleRights |= BQCA;break;
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
        pos->st->enPas=FRtoSQ(epFile,epRank);
        fen+=2;
    }else{
        pos->st->enPas=NO_SQ;
        if(*fen=='-') fen++;
    }

    //-------- fifty move counter / fullmove number --------
    pos->st->fiftyMove=0;
    int fullmove=0;
    sscanf(fen, " %d %d", &pos->st->fiftyMove, &fullmove);
    if(pos->st->fiftyMove < 0) pos->st->fiftyMove=0;

    updateListMaterial(pos);

    //-------- material sanity: one king per side, no pawns on back ranks --------
    int kings[2]={0,0};
    for(i=0;i<64;i++){
        int pce=pos->pieces[i];
        if(pce==EMPTY) continue;
        if(TYPE_OF(pce) == KING) kings[COLOR_OF(pce)]++;
        if(TYPE_OF(pce) == PAWN && (RANK_OF(i)==RANK_1 || RANK_OF(i)==RANK_8)){
            printf("FEN Not Valid \n");
            return -1;
        }
    }
    if(kings[WHITE]!=1 || kings[BLACK]!=1){ printf("FEN Not Valid \n"); return -1; }

    pos->st->pkHash=GeneratePKHash(pos);
    pos->st->npHash[WHITE]=GenerateNonPawnHash(pos,WHITE);
    pos->st->npHash[BLACK]=GenerateNonPawnHash(pos,BLACK);
    pos->st->minorHash=GenerateMinorHash(pos);
    pos->st->posKey=GeneratePosKey(pos);
    set_check_info(pos);

    return 0;
}

void ResetBoard(S_BOARD *pos){
    pos->st = &pos->stateTable[0];
    memset(pos->st, 0, sizeof(StateInfo));

    int index=0;

    for(index=0;index<PIECE_TYPE_NB;++index){
        pos->byTypeBB[index]=0ULL;
    }

    //making them empty in 64 board
    for(index=0;index<64;++index){
        pos->pieces[index]=EMPTY;
    }

    //making the pieces value 0 in bitboards
    for(index=0;index<COLOR_NB;++index){
        pos->byColorBB[index]=0ULL;
    }

    pos->side=BOTH;
    pos->st->enPas=NO_SQ;
    pos->st->fiftyMove=0;
    pos->st->pliesFromNull=0;

    pos->st->castleRights=0;
    pos->ply=0;
    pos->hisPly=0;

    pos->st->posKey=0ULL;
    pos->st->pkHash=0ULL;
    pos->st->npHash[WHITE]=0ULL;
    pos->st->npHash[BLACK]=0ULL;
    pos->st->minorHash=0ULL;
    pos->st->psqtmat=0;
    pos->st->repetition=0;
    pos->st->previous=NULL;
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
    printf(" Key: %"PRIu64"\n",pos->st->posKey);

    U64 checkers=attackersToKingSq(pos,pos->side);

    printf(" Checkers: ");
    while(checkers){
        int sq=poplsb(&checkers);
        printf("%s,",PrSq(sq));
    }
    printf("\n");
}