#include "defs.h"

const char pieceChar[]=" PNBRQK  pnbrqk ";
const char sideChar[]="wb-";
const char fileChar[]="abcdefgh";
const char rankChar[]="12345678";

// Pieces: EMPTY=0, wP..wK=1..6, bP..bK=9..14, PIECE_NB=16
const int pieceCol[16]={BOTH, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, BOTH,
                        BOTH, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BOTH};

const int piecePawn[16]={FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                         FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE};
const int pieceKing[16]={FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE,
                        FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE};
const int pieceType[16] = {100,p_pawn,p_knight,p_bishop,p_rook,p_queen,p_king,100,
                           100,p_pawn,p_knight,p_bishop,p_rook,p_queen,p_king,100};

const int filesBoard[BOARD_NUMS_SQ]={0,
    1,2,3,4,5,6,7,0,
    1,2,3,4,5,6,7,0,
    1,2,3,4,5,6,7,0,
    1,2,3,4,5,6,7,0,
    1,2,3,4,5,6,7,0,
    1,2,3,4,5,6,7,0,
    1,2,3,4,5,6,7,0,
    1,2,3,4,5,6,7
};
const int ranksBoard[BOARD_NUMS_SQ]={0,
0,0,0,0,0,0,0,1,
1,1,1,1,1,1,1,2,
2,2,2,2,2,2,2,3,
3,3,3,3,3,3,3,4,
4,4,4,4,4,4,4,5,
5,5,5,5,5,5,5,6,
6,6,6,6,6,6,6,7,
7,7,7,7,7,7,7};