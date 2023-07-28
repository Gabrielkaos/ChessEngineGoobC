#include "defs.h"
#include "string.h"
#include "stdio.h"

//getting square string
char * PrSq(const int sq){

    static char SqStr[3];

    int file=filesBoard[sq];
    int rank=ranksBoard[sq];

    sprintf(SqStr,"%c%c",('a'+file),('1'+rank));

    return SqStr;
}

char * PrMove(const int move){
    static char moveStr[6];

    int fromFile=filesBoard[FROMSQ(move)];
    int fromRank=ranksBoard[FROMSQ(move)];

    int toFile=filesBoard[TOSQ(move)];
    int toRank=ranksBoard[TOSQ(move)];

    int promoted=PROMOTED(move);

    if(promoted){
        char pchar='q';
        if(ISKni(promoted)){
            pchar='n';
        }else if(ISRQ(promoted) && !ISBQ(promoted)){
            pchar='r';
        }else if(!ISRQ(promoted) && ISBQ(promoted)){
            pchar='b';
        }
        sprintf(moveStr,"%c%c%c%c%c",('a'+fromFile),('1'+fromRank),('a'+toFile),('1'+toRank),pchar);
    }else{
        sprintf(moveStr,"%c%c%c%c",('a'+fromFile),('1'+fromRank),('a'+toFile),('1'+toRank));
    }

    return moveStr;
}


void printFen(const S_BOARD *pos,char *fen){

    int empty,r,c;

    for(r=7;r>=0;r--){
        for(c=0;c<8;c++){
            for(empty=0;c<8 && (pos->pieces[8*r+c]==EMPTY);c++)
                empty++;
            if(empty) *fen++='0'+empty;
            if(c<8) *fen++=pieceChar[pos->pieces[8*r+c]];
        }
        if(r>0) *fen++='/';
    }
    *fen++ = ' ';
    *fen++ = pos->side == WHITE ? 'w' : 'b';
    *fen++ = ' ';

    int cr=pos->castleRights;

    if(cr & WKCA) *fen++='K';
    if(cr & WQCA) *fen++='Q';
    if(cr & BKCA) *fen++='k';
    if(cr & BQCA) *fen++='q';

    if(!cr){
        *fen++='-';
    }
    *fen++ = ' ';

    if(pos->enPas != NO_SQ){
        *fen++='a'+filesBoard[pos->enPas];
        *fen++='1'+ranksBoard[pos->enPas];
    }else{
        *fen++='-';
    }


    sprintf(fen," %d %d",pos->fiftyMove,1+(pos->hisPly-(pos->side==BLACK))/2);
}

void printBitBoard(U64 bb) {

	U64 shiftMe = 1ULL;

	int rank = 0;
	int file = 0;
	int sq64 = 0;

	for(rank = RANK_8; rank >= RANK_1; --rank) {
        printf("%d",rank+1);
		for(file = FILE_A; file <= FILE_H; ++file) {
			sq64 = FRtoSQ(file,rank);	// 120 based
			//sq64 = SQ64(sq); // 64 based

			if((shiftMe << sq64) & bb)
				printf("X");
			else
				printf("-");

		}
		printf("\n");
	}
	printf(" abcdefgh\n");
    printf("\n\n");
}

int ParseMove(char *ptrChar, S_BOARD *pos){
    if (ptrChar[1] > '8' || ptrChar[1] < '1') return NOMOVE;
    if (ptrChar[3] > '8' || ptrChar[3] < '1') return NOMOVE;
    if (ptrChar[0] > 'h' || ptrChar[0] < 'a') return NOMOVE;
    if (ptrChar[2] > 'h' || ptrChar[2] < 'a') return NOMOVE;


    //int from=FRtoSQ(ptrChar[0]-'a',ptrChar[1]-'1');
    //int to=FRtoSQ(ptrChar[2]-'a',ptrChar[3]-'1');

    int from=(ptrChar[0]-'a')+(8 - (ptrChar[1] - '0')) * 8;
    int to=(ptrChar[2]-'a')+(8 - (ptrChar[3] - '0')) * 8;

    from=MIRROR64(from);
    to=MIRROR64(to);

    S_MOVELIST list[1];
    GenerateAllMoves(pos,list);

    int moveNum=0;
    int Move=0;
    int prom_piece=EMPTY;

    for(moveNum=0;moveNum<list->count;++moveNum){
        Move=list->moves[moveNum].move;
        if(FROMSQ(Move)==from && TOSQ(Move)==to){
            prom_piece=PROMOTED(Move);
            if (prom_piece != EMPTY){
                if(ISRQ(prom_piece) && ! ISBQ(prom_piece) && ptrChar[4]=='r'){
                    return Move;
                }else if(!ISRQ(prom_piece) && ISBQ(prom_piece) && ptrChar[4]=='b'){
                    return Move;
                }else if(ISRQ(prom_piece) && ISBQ(prom_piece) && ptrChar[4]=='q'){
                    return Move;
                }else if(ISKni(prom_piece) && ptrChar[4]=='n'){
                    return Move;
                }
                continue;
            }
            return Move;
        }
    }

    return NOMOVE;
}

void PrintMoveList(const S_MOVELIST *list,S_BOARD *pos){
    int index;
    int move=0;
    int illegal=0;
    int moveNumber=0;

    printf("MoveList: \n");

    for(index=0;index<list->count;++index){
        move=list->moves[index].move;
        if(!MoveExists(pos,move)){
            illegal++;
            continue;
        }
        printf("Move:%d -> %s\n",++moveNumber,PrMove(move));
    }

    printf("\nTotal Moves: %d",list->count-illegal);
}
