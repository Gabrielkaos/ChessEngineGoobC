
#include "defs.h"
#include "recog.h"
#include "stdio.h"
#include "bitboards.h"

int drawFiftyMoveRule(const S_BOARD *pos){
    return pos->useFiftyMoveRule ? pos->fiftyMove > 99 : FALSE;
}

int drawRepetition(const S_BOARD *pos){
    int index=0;
    for(index=(pos->hisPly-pos->fiftyMove);index<pos->hisPly-1;++index){

        if(pos->posKey==pos->search->history[index].posKey){
            return TRUE;
        }
    }

    return FALSE;
}

int drawByRepetitionEthereals(const S_BOARD *pos){
    int reps = 0;

    for (int i = pos->hisPly - 2; i >= 0; i -= 2) {
        if (i < pos->hisPly - pos->fiftyMove)
            break;

        if (    pos->search->history[i].posKey == pos->posKey
            && (i > pos->hisPly - pos->ply || ++reps == 2))
            return 1;
    }

    return 0;
}

int drawByMaterial(const S_BOARD *pos){
    U64 pawns   = pos->byTypeBB[PAWN];
    U64 rooks   = pos->byTypeBB[ROOK];
    U64 queens  = pos->byTypeBB[QUEEN];
    U64 bishops = pos->byTypeBB[BISHOP];
    U64 knights = pos->byTypeBB[KNIGHT];

    return !(pawns | rooks | queens)
        && (!several(pos->byColorBB[WHITE]) || !several(pos->byColorBB[BLACK]))
        && (!several(knights | bishops)
            || (!bishops && COUNTBIT(knights) <= 2));
}

int recog_draw(const S_BOARD *pos){

    ASSERT(checkBoard(pos));

    return  drawFiftyMoveRule(pos)   ||
            drawByRepetitionEthereals(pos)      ||
            drawByMaterial(pos);
}
