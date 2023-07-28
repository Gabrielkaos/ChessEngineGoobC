#include "defs.h"
#include "stdio.h"
#include "search.h"
#include "bitboards.h"
#include "evaluate.h"

#define HASH_PCE(pce,sq) (pos->posKey ^= (pieceKeys[(pce)][(sq)]))
#define HASH_SIDE (pos->posKey ^= (sideKey))
#define HASH_CA (pos->posKey ^= (castleKeys[(pos->castleRights)]))
#define HASH_EP (pos->posKey ^= (pieceKeys[EMPTY][pos->enPas]))
//#define HASH_PAWN(pce,sq) (pos->pawnPosKey^=(pieceKeys[(pce)][(sq)]))
#define HASH_PK(pce,sq) (pos->pkHash^=(pieceKeys[(pce)][(sq)]))

int getCapturedPiece(int move){
    int captured = CAPTURED(move);

    if(!captured){
        if(MVFLAGEP & move)captured=wP;
        if(MVFLAGPROM & move) captured=wP;
    }

    return captured;

}
int moveIsTactical(S_BOARD *pos,int move){
    return (pos->pieces[TOSQ(move)] != EMPTY && (move & MVFLAGCA)==0) ||
            (move & MVFLAGEP || PROMOTED(move) != 0);

}
int MoveBestCaseValue(S_BOARD *pos){
    int pce;
    ASSERT(checkBoard(pos));

    int value = SEEPieceValues[wP];

    for(pce=wK;pce<=bK;++pce){
        if (pce==wP || pce==bP || pce==wK || pce==bK)continue;
        if(pos->bitboards[pce] & pos->occupancy[!pos->side]){
            value=SEEPieceValues[pce];
            break;
        }
    }
    U64 pawns=pos->bitboards[wP] | pos->bitboards[bP];
    if(pawns & pos->occupancy[pos->side] &
       (pos->side==WHITE ? RankBBMask[RANK_7]:RankBBMask[RANK_2])){
        value+=SEEPieceValues[wQ]-SEEPieceValues[wP];
    }
    return value;
}
int moveEstimatedValue(S_BOARD *pos, int move) {

    ASSERT(moveValid(move));

    // Start with the value of the piece on the target square
    int value = SEEPieceValues[pos->pieces[TOSQ(move)]];

    // Factor in the new piece's value and remove our promoted pawn
    if (PROMOTED(move))
        value += SEEPieceValues[PROMOTED(move)] - SEEPieceValues[wP];

    // Target square is encoded as empty for enpass moves
    else if (move & MVFLAGEP)
        value = SEEPieceValues[wP];

    // We encode Castle moves as KxR, so the initial step is wrong
    else if (move & MVFLAGCA)
        value = 0;

    return value;
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

INLINE void ClearPiece(const int sq,S_BOARD *pos){

    int pce=pos->pieces[sq];
    int col=pieceCol[pce];

    ASSERT(SqOnBoard(sq));
    ASSERT(SideValid(col));
    ASSERT(PieceValidEmpty(pce));

    HASH_PCE(pce,sq);

    pos->pieces[sq]=EMPTY;

    if(piecePawn[pce] || pieceKing[pce]){
        HASH_PK(pce,sq);
    }

    //psqtmat
    pos->psqtmat -= PSQTMATTABLE[pce][sq];

    CLRBIT(pos->occupancy[col],(sq));
    CLRBIT(pos->occupancy[BOTH],(sq));
    CLRBIT(pos->bitboards[pce],(sq));
}
INLINE void AddPiece(const int sq,S_BOARD *pos,const int pce){

    int col=pieceCol[pce];

    ASSERT(SqOnBoard(sq));
    ASSERT(SideValid(col));
    ASSERT(PieceValidEmpty(pce));

    HASH_PCE(pce,sq);

    pos->pieces[sq]=pce;

    if(piecePawn[pce] || pieceKing[pce]){
        HASH_PK(pce,sq);
    }

    //psqt mat
    pos->psqtmat+=PSQTMATTABLE[pce][sq];

    SETBIT(pos->occupancy[col],(sq));
    SETBIT(pos->occupancy[BOTH],(sq));
    SETBIT(pos->bitboards[pce],(sq));
}
INLINE void MovePiece(const int from,const int to,S_BOARD *pos){

    ASSERT(SqOnBoard(from));
    ASSERT(SqOnBoard(to));

    int pce=pos->pieces[from];
    int col=pieceCol[pce];
    int to_pce = pos->pieces[to];

    ASSERT(SideValid(col));
    ASSERT(PieceValidEmpty(pce));

    HASH_PCE(pce,from);
    pos->pieces[from]=EMPTY;

    HASH_PCE(pce,to);
    pos->pieces[to]=pce;


    if(piecePawn[pce] || pieceKing[pce]){
        HASH_PK(pce,from);
        HASH_PK(pce,to);
    }

    //psqt mat
    pos->psqtmat += PSQTMATTABLE[pce][to]
                   -PSQTMATTABLE[pce][from]
                   -PSQTMATTABLE[to_pce][to];

    CLRBIT(pos->occupancy[col],(from));
    SETBIT(pos->occupancy[col],(to));
    CLRBIT(pos->occupancy[BOTH],(from));
    SETBIT(pos->occupancy[BOTH],(to));

    CLRBIT(pos->bitboards[pce],(from));
    SETBIT(pos->bitboards[pce],(to));
}

int makeMove(S_BOARD *pos,int move){

    ASSERT(moveValid(move));

    int from=FROMSQ(move);
    int to=TOSQ(move);
    int side=pos->side;

    pos->moveStack[pos->ply] = move;
    pos->pieceStack[pos->ply] = pieceType[pos->pieces[from]];

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

    pos->hisPly++;
    pos->ply++;
    pos->side ^=1;
    HASH_SIDE;

    U64 kingAttackers = attackersToKingSq(pos,side);
    if(kingAttackers){
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

    int captured=CAPTURED(move);
    if(captured != EMPTY){
        AddPiece(to,pos,captured);
    }

    if(PROMOTED(move) != EMPTY){
        ClearPiece(from,pos);
        AddPiece(from,pos,(pieceCol[PROMOTED(move)]==WHITE ? wP:bP));
    }
}

void makeNullMove(S_BOARD *pos){

    pos->moveStack[pos->ply] = NULLMOVE;
    pos->ply++;
    pos->history[pos->hisPly].posKey=pos->posKey;


    if(pos->enPas != NO_SQ) HASH_EP;

    pos->history[pos->hisPly].move=NULLMOVE;
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
