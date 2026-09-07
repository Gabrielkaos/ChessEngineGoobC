#include "defs.h"

const char pieceChar[]=" PNBRQK  pnbrqk ";
const char sideChar[]="wb-";
const char fileChar[]="abcdefgh";
const char rankChar[]="12345678";

// Pieces: EMPTY=0, wP..wK=1..6, bP..bK=9..14, PIECE_NB=16
const int pieceBig[16]={FALSE, FALSE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE,
                        FALSE, FALSE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE};
const int pieceMin[16]={FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE,
                        FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE};
const int pieceMaj[16]={FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE,
                        FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE};
const int pieceCol[16]={BOTH, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, BOTH,
                        BOTH, BLACK, BLACK, BLACK, BLACK, BLACK, BLACK, BOTH};

const int piecePawn[16]={FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE,
                         FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, FALSE};
const int pieceKnight[16]={FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE,
                          FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE};
const int pieceKing[16]={FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE,
                        FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE};
const int pieceRookQueen[16]={FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE,
                             FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE};
const int pieceBishopQueen[16]={FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE,
                               FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE};
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

const int Mirror64[64] = {
56	,	57	,	58	,	59	,	60	,	61	,	62	,	63	,
48	,	49	,	50	,	51	,	52	,	53	,	54	,	55	,
40	,	41	,	42	,	43	,	44	,	45	,	46	,	47	,
32	,	33	,	34	,	35	,	36	,	37	,	38	,	39	,
24	,	25	,	26	,	27	,	28	,	29	,	30	,	31	,
16	,	17	,	18	,	19	,	20	,	21	,	22	,	23	,
8	,	9	,	10	,	11	,	12	,	13	,	14	,	15	,
0	,	1	,	2	,	3	,	4	,	5	,	6	,	7
};