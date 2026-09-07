#include "stdio.h"
#include "search.h"
#include "bitboards.h"
#include "evaluate.h"
#include "movegen.h"
#include "makemove.h"
#include "init.h"
#include "attacks.h"
#include "validate.h"
#include "nnue_loader.h"

#define HASH_PCE(pce,sq) (pos->posKey ^= (pieceKeys[(pce)][(sq)]))
#define HASH_SIDE (pos->posKey ^= (sideKey))
#define HASH_CA (pos->posKey ^= (castleKeys[(pos->castleRights)]))
#define HASH_EP (pos->posKey ^= (pieceKeys[EMPTY][pos->enPas]))
#define HASH_PK(pce,sq) (pos->pkHash^=(pieceKeys[(pce)][(sq)]))
//correction-history keys: non-pawn material per color, minors of both colors
#define HASH_NP(pce,sq,col) (pos->npHash[(col)]^=(pieceKeys[(pce)][(sq)]))
#define HASH_MINOR(pce,sq) (pos->minorHash^=(pieceKeys[(pce)][(sq)]))


int moveIsTactical(S_BOARD *pos,int move){
    return (pos->pieces[TOSQ(move)] != EMPTY && (move & MVFLAGCA)==0) ||
            (move & MVFLAGEP || PROMOTED(move) != 0);

}
int MoveBestCaseValue(S_BOARD *pos){
    ASSERT(checkBoard(pos));

    U64 enemy = pos->byColorBB[!pos->side];
    int value = SEEPieceValues[wP];

    // Check from most valuable to least valuable piece type present
    if (pos->byTypeBB[QUEEN] & enemy){
        value = SEEPieceValues[wQ];
    } else if (pos->byTypeBB[ROOK] & enemy){
        value = SEEPieceValues[wR];
    } else if ((pos->byTypeBB[BISHOP] | pos->byTypeBB[KNIGHT]) & enemy){
        value = SEEPieceValues[wB]; // bishop and knight share the same SEE value
    }

    U64 pawns = pos->byTypeBB[PAWN] & pos->byColorBB[pos->side];
    if(pawns & (pos->side==WHITE ? RankBBMask[RANK_7]:RankBBMask[RANK_2])){
        value += SEEPieceValues[wQ] - SEEPieceValues[wP];
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
    int col=COLOR_OF(pce);
    int pt=TYPE_OF(pce);

    ASSERT(SqOnBoard(sq));
    ASSERT(SideValid(col));
    ASSERT(PieceValidEmpty(pce));

    HASH_PCE(pce,sq);

    pos->pieces[sq]=EMPTY;

    if(pt == PAWN || pt == KING){
        HASH_PK(pce,sq);
    }

    if(pt != PAWN){
        HASH_NP(pce,sq,col);
        if(pt == KNIGHT || pt == BISHOP)
            HASH_MINOR(pce,sq);
    }

    //psqtmat
    pos->psqtmat -= PSQTMATTABLE[pce][sq];
    nnue_update_remove(pos, pce, sq);

    U64 mask = 1ULL << sq;
    pos->byColorBB[col] ^= mask;
    pos->byTypeBB[pt] ^= mask;
    pos->byTypeBB[ALL_PIECES] ^= mask;
}
INLINE void AddPiece(const int sq,S_BOARD *pos,const int pce){

    int col=COLOR_OF(pce);
    int pt=TYPE_OF(pce);

    ASSERT(SqOnBoard(sq));
    ASSERT(SideValid(col));
    ASSERT(PieceValidEmpty(pce));

    HASH_PCE(pce,sq);

    pos->pieces[sq]=pce;

    if(pt == PAWN || pt == KING){
        HASH_PK(pce,sq);
    }

    if(pt != PAWN){
        HASH_NP(pce,sq,col);
        if(pt == KNIGHT || pt == BISHOP)
            HASH_MINOR(pce,sq);
    }

    //psqt mat
    pos->psqtmat+=PSQTMATTABLE[pce][sq];
    nnue_update_add(pos, pce, sq);

    U64 mask = 1ULL << sq;
    pos->byColorBB[col] ^= mask;
    pos->byTypeBB[pt] ^= mask;
    pos->byTypeBB[ALL_PIECES] ^= mask;
}
INLINE void MovePiece(const int from,const int to,S_BOARD *pos){

    ASSERT(SqOnBoard(from));
    ASSERT(SqOnBoard(to));

    int pce=pos->pieces[from];
    int col=COLOR_OF(pce);
    int pt=TYPE_OF(pce);
    int to_pce = pos->pieces[to];

    ASSERT(SideValid(col));
    ASSERT(PieceValidEmpty(pce));

    HASH_PCE(pce,from);
    pos->pieces[from]=EMPTY;

    HASH_PCE(pce,to);
    pos->pieces[to]=pce;


    if(pt == PAWN || pt == KING){
        HASH_PK(pce,from);
        HASH_PK(pce,to);
    }

    if(pt != PAWN){
        HASH_NP(pce,from,col);
        HASH_NP(pce,to,col);
        if(pt == KNIGHT || pt == BISHOP){
            HASH_MINOR(pce,from);
            HASH_MINOR(pce,to);
        }
    }

    //psqt mat
    pos->psqtmat += PSQTMATTABLE[pce][to]
                   -PSQTMATTABLE[pce][from]
                   -PSQTMATTABLE[to_pce][to];
    nnue_update_move(pos, pce, from, to);
    if (to_pce != EMPTY) nnue_update_remove(pos, to_pce, to);

    U64 mask = (1ULL << from) | (1ULL << to);
    pos->byColorBB[col] ^= mask;
    pos->byTypeBB[pt] ^= mask;
    pos->byTypeBB[ALL_PIECES] ^= mask;
}

int makeMove(S_BOARD *pos,int move){

    ASSERT(moveValid(move));

    int from=FROMSQ(move);
    int to=TOSQ(move);
    int side=pos->side;

    //a king can never be captured - once the enemy king bitboard is cleared,
    //the post-move legality check below loses its reference point and a bogus
    //"legal" move would send the search into a kingless position (empty king
    //bb -> ctzll(0) -> out-of-bounds table access). reject before mutating.
    if(TYPE_OF(pos->pieces[to]) == KING){
        return FALSE;
    }

    pos->search->moveStack[pos->ply] = move;
    pos->search->pieceStack[pos->ply] = PTYPE_OF(pos->pieces[from]);

    pos->search->history[pos->hisPly].posKey=pos->posKey;

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

    //the move itself is stored in pos->search->moveStack[pos->ply], not in history
    pos->search->history[pos->hisPly].enPas=pos->enPas;
    pos->search->history[pos->hisPly].fiftyMove=pos->fiftyMove;
    pos->search->history[pos->hisPly].pliesFromNull=pos->pliesFromNull;
    pos->search->history[pos->hisPly].castleRights=pos->castleRights;
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

    if(TYPE_OF(pos->pieces[from]) == PAWN){
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

    pos->pliesFromNull++;
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

    //the move was stored by makeMove at the pre-increment ply
    int move=pos->search->moveStack[pos->ply];
    int from =FROMSQ(move);
    int to=TOSQ(move);

    if(pos->enPas != NO_SQ){
        HASH_EP;
    }
    HASH_CA;

    pos->castleRights      = pos->search->history[pos->hisPly].castleRights;
    pos->fiftyMove         = pos->search->history[pos->hisPly].fiftyMove;
    pos->enPas             = pos->search->history[pos->hisPly].enPas;
    pos->pliesFromNull     = pos->search->history[pos->hisPly].pliesFromNull;

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
        AddPiece(from,pos,MAKE_PIECE(COLOR_OF(PROMOTED(move)), PAWN));
    }
}

void makeNullMove(S_BOARD *pos){

    pos->search->moveStack[pos->ply] = NULLMOVE;
    pos->ply++;
    pos->search->history[pos->hisPly].posKey=pos->posKey;


    if(pos->enPas != NO_SQ) HASH_EP;

    //the move itself lives in pos->search->moveStack[ply] (NULLMOVE already stored)
    pos->search->history[pos->hisPly].fiftyMove=pos->fiftyMove;
    pos->search->history[pos->hisPly].enPas=pos->enPas;
    pos->search->history[pos->hisPly].castleRights=pos->castleRights;
    pos->search->history[pos->hisPly].pliesFromNull=pos->pliesFromNull;

    pos->enPas=NO_SQ;
    pos->pliesFromNull=0;


    pos->side^=1;
    pos->hisPly++;
    HASH_SIDE;

}
void takeNullMove(S_BOARD *pos){

    pos->hisPly--;
    pos->ply--;

    if(pos->enPas != NO_SQ) HASH_EP;

    pos->castleRights  = pos->search->history[pos->hisPly].castleRights;
    pos->fiftyMove     = pos->search->history[pos->hisPly].fiftyMove;
    pos->enPas         = pos->search->history[pos->hisPly].enPas;
    pos->pliesFromNull = pos->search->history[pos->hisPly].pliesFromNull;

    if(pos->enPas != NO_SQ) HASH_EP;

    pos->side^=1;
    HASH_SIDE;


}