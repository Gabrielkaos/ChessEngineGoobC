#include "defs.h"

const char pieceChar[]=" PNBRQKpnbrqk";
const char sideChar[]="wb-";
const char fileChar[]="abcdefgh";
const char rankChar[]="12345678";

//enum { EMPTY, wP, wN, wB, wR, wQ, wK, bP, bN, bB, bR, bQ, bK };
const int pieceBig[13]={FALSE, FALSE, TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, TRUE, TRUE, TRUE, TRUE, TRUE };
const int pieceMin[13]={FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE };
const int pieceMaj[13]={FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE };
const int pieceCol[13]={BOTH, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE,
	BLACK, BLACK, BLACK, BLACK, BLACK, BLACK};

const int piecePawn[13]={FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE };
const int pieceKnight[13]={FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE};
const int pieceKing[13]={FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE};
const int pieceRookQueen[13]={FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE, FALSE };
const int pieceBishopQueen[13]={FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE };
const int pieceType[13] = {100,p_pawn,p_knight,p_bishop,p_rook,p_queen,p_king,p_pawn,p_knight,p_bishop,p_rook,p_queen,p_king};

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

const int Squares64To120[64]={
    21,22,23,24,25,26,27,28,
    31,32,33,34,35,36,37,38,
    41,42,43,44,45,46,47,48,
    51,52,53,54,55,56,57,58,
    61,62,63,64,65,66,67,68,
    71,72,73,74,75,76,77,78,
    81,82,83,84,85,86,87,88,
    91,92,93,94,95,96,97,98
};
