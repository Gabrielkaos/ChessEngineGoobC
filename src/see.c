#include "defs.h"
#include "see.h"
#include "stdio.h"
#include "search.h"

int SEE(int move,const S_BOARD *pos){
    int from     =FROMSQ(move);
    int to       =TOSQ(move);
    int pce_from =pos->pieces[from];
    int pce_to   =pos->pieces[to];


    ASSERT(moveValid(move));
    ASSERT(PieceValid(pce_from));
    ASSERT(PieceValid(pce_to));

    if(piecePawn[pce_from]) return 1;
    if(SEEPieceValues[pce_to]>=SEEPieceValues[pce_from]-50)return 1;
    if(!is_square_attacked_BB(to,!pos->side,pos))return 1;

    return 0;
}

int SEE_BB(int move,const S_BOARD *pos){

    moveValid(move);

    int winningSide    = BLACK;
    int to             = TOSQ(move);
    U64 attackers      = allAttackersToSquare(pos,pos->occupancy[BOTH],to);
    U64 whiteAttackers = pos->occupancy[WHITE] & attackers;
    U64 blackAttackers = pos->occupancy[BLACK] & attackers;


    if(COUNTBIT(whiteAttackers)>COUNTBIT(blackAttackers))winningSide=WHITE;
    if(COUNTBIT(whiteAttackers)==COUNTBIT(blackAttackers))return 0;

    return pos->side==winningSide;
}
