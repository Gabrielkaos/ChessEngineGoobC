#include "defs.h"
#include "string.h"
#include "stdio.h"

int SqOnBoard(const int sq){
    return ((filesBoard[sq]>=0 && filesBoard[sq]<=7) && (ranksBoard[sq]>=0 && ranksBoard[sq]<=7) && (sq>=0 && sq<=63)) ? 1:0;
}

int SideValid(const int side){
    return (side==WHITE || side==BLACK) ? 1:0;
}

int FileRankValid(const int fr){
    return (fr >=0 && fr <=7) ? 1:0;
}

int PieceValidEmpty(const int pce){
    return (pce >=EMPTY && pce<=bK) ? 1:0;
}

int PieceValid(const int pce){
    return (pce >=wP && pce<=bK) ? 1:0;
}

void test(S_BOARD *pos){
    FILE *file;
    char lineIn[2000];
    file=fopen("perfttest.txt","r");
    int eval1;
    int eval2;
    int positions=0;

    if (file == NULL){
        printf("No file Found\n");
        return;
    }else{
        while((fgets(lineIn,2000,file)) != NULL){
            char *fen=strtok(lineIn,";");
            ParseFEN(fen,pos);
            eval1=EvalPosition(pos);
            MirrorBoard(pos);
            eval2=EvalPosition(pos);
            MirrorBoard(pos);
            positions++;

            if(eval1 != eval2){
                printf("%d  %d\n",eval1,eval2);
                PrintBoard(pos);
                MirrorBoard(pos);
                PrintBoard(pos);
                MirrorBoard(pos);
                printf("TEST FAILED\n");
                printf("In position %d\n",positions);
                return;
            }
        }
        memset(&lineIn[0], 0, sizeof(lineIn));

        printf("TEST PASSED, Total positions %d\n",positions);
    }
}
