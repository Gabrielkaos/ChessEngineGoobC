#include "defs.h"
#include "stdio.h"

#define HASH_PCE(pce,sq) (pos->posKey ^= (pieceKeys[(pce)][(sq)]))
#define HASH_SIDE (pos->posKey ^= (sideKey))
#define HASH_CA (pos->posKey ^= (castleKeys[(pos->castleRights)]))
#define HASH_EP (pos->posKey ^= (pieceKeys[EMPTY][pos->enPas]))


const int castlePerm[64]={
    13,15,15,15,12,15,15,14,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,
    7,15,15,15, 3,15,15,11

};

static inline void ClearPiece(const int sq,S_BOARD *pos){

    int pce=pos->pieces[sq];
    int col=pieceCol[pce];

    HASH_PCE(pce,sq);

    pos->pieces[sq]=EMPTY;
    pos->material[col]-=pieceVal[pce];

    if(pieceBig[pce]){
        pos->bigPiece[col]--;
        if(pieceMaj[pce]){
            pos->majPiece[col]--;
        }else{
            pos->minPiece[col]--;
        }
    }
    CLRBIT(pos->occupancy[col],(sq));
    CLRBIT(pos->occupancy[BOTH],(sq));
    CLRBIT(pos->bitboards[pce],(sq));

    pos->numPieces[pce]--;

    /*//remove piece from piece list
    for(index=0;index<pos->numPieces[pce];++index){
        if(pos->pieceList[pce][index]==sq){
            t_pieceNum=index;
            break;
        }
    }*/


    //pos->pieceList[pce][t_pieceNum]=pos->pieceList[pce][pos->numPieces[pce]];
}
static inline void AddPiece(const int sq,S_BOARD *pos,const int pce){

    int col=pieceCol[pce];

    HASH_PCE(pce,sq);

    pos->pieces[sq]=pce;

    if(pieceBig[pce]){
        pos->bigPiece[col]++;
        if(pieceMaj[pce]){
            pos->majPiece[col]++;
        }else{
            pos->minPiece[col]++;
        }
    }
    SETBIT(pos->occupancy[col],(sq));
    SETBIT(pos->occupancy[BOTH],(sq));
    SETBIT(pos->bitboards[pce],(sq));

    pos->material[col]+=pieceVal[pce];
    pos->numPieces[pce]++;
    //pos->pieceList[pce][pos->numPieces[pce]++]=sq;

}
static inline void MovePiece(const int from,const int to,S_BOARD *pos){


    //int index=0;
    int pce=pos->pieces[from];
    int col=pieceCol[pce];
/*
#ifdef DEBUG
    int t_pieceNum=FALSE;
#endif // DEBUG*/

    HASH_PCE(pce,from);
    pos->pieces[from]=EMPTY;

    HASH_PCE(pce,to);
    pos->pieces[to]=pce;

    CLRBIT(pos->occupancy[col],(from));
    SETBIT(pos->occupancy[col],(to));
    CLRBIT(pos->occupancy[BOTH],(from));
    SETBIT(pos->occupancy[BOTH],(to));

    CLRBIT(pos->bitboards[pce],(from));
    SETBIT(pos->bitboards[pce],(to));

    /*for(index=0;index<pos->numPieces[pce];++index){
        if(pos->pieceList[pce][index]==from){
            pos->pieceList[pce][index]=to;
#ifdef DEBUG
            t_pieceNum=TRUE;
#endif // DEBUG
            break;
        }
    }*/
}

int makeMove(S_BOARD *pos,int move){

    int from=FROMSQ(move);
    int to=TOSQ(move);
    int side=pos->side;

    pos->history[pos->hisPly].posKey=pos->posKey;

    if(move & MVFLAGEP){
        if(side==WHITE){
            ClearPiece(to-8,pos);
        }else{
            ClearPiece(to+8,pos);
        }
    }else if(move & MVFLAGCA){
        switch(to){
            case C1:
                MovePiece(A1,D1,pos);
                break;
            case G1:
                MovePiece(H1,F1,pos);
                break;
            case C8:
                MovePiece(A8,D8,pos);
                break;
            case G8:
                MovePiece(H8,F8,pos);
                break;

        }
    }
    if(pos->enPas != NO_SQ){
        HASH_EP;
    }
    HASH_CA;

    pos->history[pos->hisPly].move=move;
    pos->history[pos->hisPly].enPas=pos->enPas;
    pos->history[pos->hisPly].fiftyMove=pos->fiftyMove;
    pos->history[pos->hisPly].castleRights=pos->castleRights;
    pos->castleRights &= castlePerm[from];
    pos->castleRights &= castlePerm[to];
    pos->enPas=NO_SQ;

    HASH_CA;

    int captured=CAPTURED(move);
    pos->fiftyMove++;

    if(captured != EMPTY){
        ClearPiece(to,pos);
        pos->fiftyMove=0;
    }

    pos->hisPly++;
    pos->ply++;


    if(piecePawn[pos->pieces[from]]){
        pos->fiftyMove=0;
        if(move & MVFLAGPS){
            if(side==WHITE){
                pos->enPas=from+8;
            }else{
                pos->enPas=from-8;
            }
            HASH_EP;
        }
    }

    MovePiece(from,to,pos);

    int promotedPiece=PROMOTED(move);
    if(promotedPiece != EMPTY){
        ClearPiece(to,pos);
        AddPiece(to,pos,promotedPiece);
    }

    if(pieceKing[pos->pieces[to]]){
        pos->kingSq[side]=to;
    }

    pos->side ^=1;
    HASH_SIDE;

    //update occupancy for both
    //pos->occupancy[BOTH] = (pos->occupancy[WHITE] | pos->occupancy[BLACK]);


    if(is_square_attacked_BB((pos->kingSq[side]),pos->side,pos)){
        takeMove(pos);
        return FALSE;
    }

    return TRUE;
}
void takeMove(S_BOARD *pos){

    pos->hisPly--;
    pos->ply--;

    int move=pos->history[pos->hisPly].move;
    int from =FROMSQ(move);
    int to=TOSQ(move);

    if(pos->enPas != NO_SQ){
        HASH_EP;
    }
    HASH_CA;

    pos->castleRights=pos->history[pos->hisPly].castleRights;
    pos->fiftyMove=pos->history[pos->hisPly].fiftyMove;
    pos->enPas=pos->history[pos->hisPly].enPas;

    if(pos->enPas != NO_SQ){
        HASH_EP;
    }
    HASH_CA;

    pos->side ^= 1;
    HASH_SIDE;

    if(move & MVFLAGEP){
        if(pos->side==WHITE){
            AddPiece(to-8,pos,bP);
        }else if(pos->side==BLACK){
            AddPiece(to+8,pos,wP);
        }
    }else if(move & MVFLAGCA){
        switch(to){
            case C1: MovePiece(D1,A1,pos);break;
            case C8: MovePiece(D8,A8,pos);break;
            case G1: MovePiece(F1,H1,pos);break;
            case G8: MovePiece(F8,H8,pos);break;

        }
    }

    MovePiece(to,from,pos);

    if(pieceKing[pos->pieces[from]]){
        pos->kingSq[pos->side]=from;
    }

    int captured=CAPTURED(move);

    if(captured != EMPTY){
        AddPiece(to,pos,captured);
    }

    if(PROMOTED(move) != EMPTY){
        ClearPiece(from,pos);
        AddPiece(from,pos,(pieceCol[PROMOTED(move)]==WHITE ? wP:bP));
    }
    //update occupancy for both
    //pos->occupancy[BOTH] = (pos->occupancy[WHITE] | pos->occupancy[BLACK]);


}

void makeNullMove(S_BOARD *pos){

    pos->ply++;
    pos->history[pos->hisPly].posKey=pos->posKey;


    if(pos->enPas != NO_SQ) HASH_EP;

    pos->history[pos->hisPly].move=NOMOVE;
    pos->history[pos->hisPly].fiftyMove=pos->fiftyMove;
    pos->history[pos->hisPly].enPas=pos->enPas;
    pos->history[pos->hisPly].castleRights=pos->castleRights;
    pos->enPas=NO_SQ;


    pos->side^=1;
    pos->hisPly++;
    HASH_SIDE;

}
void takeNullMove(S_BOARD *pos){

    pos->hisPly--;
    pos->ply--;

    if(pos->enPas != NO_SQ) HASH_EP;
    pos->castleRights=pos->history[pos->hisPly].castleRights;
    pos->fiftyMove=pos->history[pos->hisPly].fiftyMove;
    pos->enPas=pos->history[pos->hisPly].enPas;
    if(pos->enPas != NO_SQ) HASH_EP;

    pos->side^=1;
    HASH_SIDE;


}
