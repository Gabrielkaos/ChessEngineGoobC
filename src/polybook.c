#include "defs.h"
#include "polykeys.h"
#include "stdio.h"

typedef struct{
    U64 key;
    unsigned short move;
    unsigned short weight;
    unsigned int learn;


}S_POLYBOOK_ENTRY;

long NumEntries=0;

S_POLYBOOK_ENTRY *Entries;

void InitPolyBook(){
    EngineOptions->useBook=FALSE;

    FILE *pFile=fopen("Performance.bin","rb");

    if(pFile==NULL){
        //printf("'Performance.bin' BookFile Invalid or Not Found\n");
    }else{
        fseek(pFile,0,SEEK_END);
        long position=ftell(pFile);

        if(position<sizeof(S_POLYBOOK_ENTRY)){
            //printf("No Entries Found\n");
            return;
        }
        NumEntries=position/sizeof(S_POLYBOOK_ENTRY);
        //printf("%ld Entries Found in File\n",NumEntries);

        Entries=(S_POLYBOOK_ENTRY*)malloc(NumEntries*sizeof(S_POLYBOOK_ENTRY));

        rewind(pFile);

        size_t returnValue;

        returnValue=fread(Entries,sizeof(S_POLYBOOK_ENTRY),NumEntries,pFile);
        //printf("Book Performance.bin found\n");

        if(NumEntries > 0 ){
            EngineOptions->useBook=TRUE;
        }
    }
}
void CleanPolyBook(){
    free(Entries);
}

const polyPieceKind[13]={
    -1,1,3,5,7,9,11,0,2,4,6,8,10
};

int hasPawnForCaps(const S_BOARD *board){
    int sqHasPawn=0;
    int targetPiece=(board->side==WHITE) ? wP:bP;
    if(board->enPas != NO_SQ){
        if(board->side==WHITE){
            sqHasPawn=board->enPas-10;
        }else{
            sqHasPawn=board->enPas+10;
        }

        if(board->pieces[sqHasPawn + 1]==targetPiece){
            return TRUE;
        }else if(board->pieces[sqHasPawn - 1]==targetPiece){
            return TRUE;
        }
    }
    return FALSE;
}

U64 PolyKeyFromBoard(const S_BOARD *board){

    int sq=0,file=0,rank=0;

    U64 finalKey=0;

    int piece=EMPTY;
    int polyPiece=0;
    int offset=0;

    for(sq=0;sq<BOARD_NUMS_SQ;++sq){
        piece=board->pieces[sq];

        if( piece != NO_SQ && piece != EMPTY){
            polyPiece=polyPieceKind[piece];
            rank=ranksBoard[sq];
            file=filesBoard[sq];
            finalKey ^= Random64Poly[(64*polyPiece)+(8*rank)+file];
        }
    }

    //castling
    offset=768;
    if(board->castleRights & WKCA) finalKey^= Random64Poly[offset+0];
    if(board->castleRights & WQCA) finalKey^= Random64Poly[offset+1];
    if(board->castleRights & BKCA) finalKey^= Random64Poly[offset+2];
    if(board->castleRights & BQCA) finalKey^= Random64Poly[offset+3];

    //enpas
    offset=772;
    if(hasPawnForCaps(board)==TRUE){
        file=filesBoard[board->enPas];
        finalKey^= Random64Poly[offset+file];
    }

    if(board->side==WHITE){
        finalKey^= Random64Poly[780];
    }

    return finalKey;
}

unsigned short endian_swap_u16(unsigned short x){

    x=(x>>8)| (x<<8);

    return x;
}

unsigned int endian_swap_u32(unsigned int x){

    x=  (x>>24)|
        ((x<<8) & 0x00FF0000 )|
        ((x>>8) & 0x0000FF00 )|
        (x<<24);

    return x;
}

U64 endian_swap_u64(U64 x)
{
    x = (x>>56) |
        ((x<<40) & 0x00FF000000000000) |
        ((x<<24) & 0x0000FF0000000000) |
        ((x<<8)  & 0x000000FF00000000) |
        ((x>>8)  & 0x00000000FF000000) |
        ((x>>24) & 0x0000000000FF0000) |
        ((x>>40) & 0x000000000000FF00) |
        (x<<56);
    return x;
}

int ConvertPolyMoveToInternalMove(unsigned short polyMove,S_BOARD *pos){
    int ff=(polyMove>>6) & 7;
    int fr=(polyMove>>9) & 7;
    int tf=(polyMove>>0) & 7;
    int tr=(polyMove>>3) & 7;

    int pp=(polyMove>>12) & 7;

    char Movestring[6];
    if(pp==0){
        sprintf(Movestring,"%c%c%c%c",
                   fileChar[ff],
                   rankChar[fr],
                   fileChar[tf],
                   rankChar[tr]);
    }else{
        char promChar='q';
        switch(pp){
            case 1:promChar='n';break;
            case 2:promChar='b';break;
            case 3:promChar='r';break;
        }
        sprintf(Movestring,"%c%c%c%c%c",
                   fileChar[ff],
                   rankChar[fr],
                   fileChar[tf],
                   rankChar[tr],
                   promChar);
    }

    return ParseMove(Movestring,pos);

}

int getBookMove(S_BOARD *pos){

    U64 polyKey=PolyKeyFromBoard(pos);

    S_POLYBOOK_ENTRY *entry;

    unsigned short move;

    const int MAXBOOKMOVES=32;
    int bookMoves[MAXBOOKMOVES];

    int tempMove=NOMOVE;
    int counter=0;

    for(entry=Entries;entry<Entries+NumEntries;entry++){
        if(polyKey==endian_swap_u64(entry->key)){
            move=endian_swap_u16(entry->move);
            tempMove=ConvertPolyMoveToInternalMove(move,pos);
            if(tempMove != NOMOVE){
                bookMoves[counter++]=tempMove;
                if(counter >= MAXBOOKMOVES)
                    break;
            }
        }
    }
    if(counter != 0){
        int randMove=rand() % counter;
        return bookMoves[randMove];
    }else{
        return NOMOVE;
    }
}

