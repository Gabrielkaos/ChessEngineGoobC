#include "defs.h"

U64 GeneratePosKey(const S_BOARD *pos){
    int sq=0;
    int piece=EMPTY;
    U64 finalKey=0;

    //pieces
    for(sq=0;sq<BOARD_NUMS_SQ;++sq){
        piece=pos->pieces[sq];
        //piece=pos->piece64[sq];
        if(piece != EMPTY && piece != NO_SQ){
            finalKey ^= pieceKeys[piece][sq];
        }
    }

    if (pos->side==WHITE){
        finalKey ^=sideKey;
    }

    if(pos->enPas != NO_SQ){
        finalKey^=pieceKeys[EMPTY][pos->enPas];
    }

    finalKey^= castleKeys[pos->castleRights];

    return finalKey;

}
