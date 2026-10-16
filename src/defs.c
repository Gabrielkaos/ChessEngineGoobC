#include "defs.h"

const char pieceChar[]=" PNBRQK  pnbrqk ";
const char sideChar[]="wb-";
const char fileChar[]="abcdefgh";
const char rankChar[]="12345678";

const int pieceKing[16]={FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE,
                        FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, FALSE};
const int pieceType[16] = {100,p_pawn,p_knight,p_bishop,p_rook,p_queen,p_king,100,
                           100,p_pawn,p_knight,p_bishop,p_rook,p_queen,p_king,100};