
#include "defs.h"
#include "hashkeys.h"
#include "init.h"

U64 GeneratePosKey(const S_BOARD *pos){
    int sq=0;
    int piece=EMPTY;
    U64 finalKey=0;

    //pieces
    for(sq=0;sq<BOARD_NUMS_SQ;++sq){
        piece=pos->pieces[sq];
        //piece=pos->piece64[sq];
        if(piece != EMPTY){
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

U64 GeneratePKHash(const S_BOARD *pos){
    int sq=0;
    int piece=EMPTY;
    U64 finalKey=0ULL;

    //pieces
    for(sq=0;sq<BOARD_NUMS_SQ;++sq){
        piece=pos->pieces[sq];
        //piece=pos->piece64[sq];
        if(piece != EMPTY && (piecePawn[piece] || pieceKing[piece])){
            finalKey ^= pieceKeys[piece][sq];
        }
    }

    return finalKey;

}

//Stockfish-style non-pawn material key: hash of every piece of one color
//except pawns (kings included)
U64 GenerateNonPawnHash(const S_BOARD *pos, int color){
    int sq=0;
    int piece=EMPTY;
    U64 finalKey=0ULL;

    for(sq=0;sq<BOARD_NUMS_SQ;++sq){
        piece=pos->pieces[sq];
        if(piece != EMPTY && pieceCol[piece]==color && !piecePawn[piece]){
            finalKey ^= pieceKeys[piece][sq];
        }
    }

    return finalKey;

}

//minor piece key: hash of all knights and bishops on the board, both colors
U64 GenerateMinorHash(const S_BOARD *pos){
    int sq=0;
    int piece=EMPTY;
    U64 finalKey=0ULL;

    for(sq=0;sq<BOARD_NUMS_SQ;++sq){
        piece=pos->pieces[sq];
        if(piece != EMPTY &&
           (pieceType[piece]==p_knight || pieceType[piece]==p_bishop)){
            finalKey ^= pieceKeys[piece][sq];
        }
    }

    return finalKey;

}
