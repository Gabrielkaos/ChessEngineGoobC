#include "defs.h"
#include "validate.h"
#include "string.h"
#include "stdio.h"
#include "evaluate.h"

int SqOnBoard(const int sq){
    return (sq >= 0 && sq < BOARD_NUMS_SQ) ? 1:0;
}

int SideValid(const int side){
    return (side >= 0 && side < BOTH) ? 1:0;
}

int PieceValid(const int pce){
    int pt = TYPE_OF(pce);
    return (pce >= wP && pce <= bK && pt >= PAWN && pt <= KING) ? 1 : 0;
}

int PieceValidEmpty(const int pce){
    return (pce == EMPTY || PieceValid(pce)) ? 1 : 0;
}

int moveValid(const int move){
    ASSERT(SqOnBoard(FROMSQ(move)));
    ASSERT(SqOnBoard(TOSQ(move)));
    ASSERT(FROMSQ(move) != TOSQ(move));
    if(PROMOTED(move))ASSERT(PieceValid(PROMOTED(move)));
    if(CAPTURED(move))ASSERT(PieceValidEmpty(CAPTURED(move)));
    return TRUE;
}
