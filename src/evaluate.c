//NOTE
/*
    Got the piece evaluation from Ethereal 12.75, credit to Andrew Grant
*/

#include "defs.h"
#include "stdio.h"
#include "math.h"
#include "evaluate.h"
#include "bitboards.h"
#include "some_maths.h"
#include "tt_eval.h"


/*General*/
int PSQTMATTABLE[13][64];
int DistanceBetween[64][64];
int distanceBetween(int sq1,int sq2){
    return DistanceBetween[sq1][sq2];
}
void initDistancesForEval(){
    for (int sq1 = 0; sq1 < 64; sq1++){
        for (int sq2 = 0; sq2 < 64; sq2++){
            DistanceBetween[sq1][sq2] = MAX(abs(filesBoard[sq1]-filesBoard[sq2]), abs(ranksBoard[sq1]-ranksBoard[sq2]));
        }
    }
}

/*Variables*/

//Piece Values
const int PiecesVal[7]={S(0,0),S(82,144),S(426,475),S(441,510),S(627,803),S(1292,1623),S(0,0)};

//PSQTs
const int QueenTabless[64]={
    S(  20, -34), S(   4, -26), S(   9, -34), S(  17, -16),
    S(  18, -18), S(  14, -46), S(   9, -28), S(  22, -44),
    S(   6, -15), S(  15, -22), S(  22, -42), S(  13,   2),
    S(  17,   0), S(  22, -49), S(  18, -29), S(   3, -18),
    S(   6,  -1), S(  21,   7), S(   5,  35), S(   0,  34),
    S(   2,  34), S(   5,  37), S(  24,   9), S(  13, -15),
    S(   9,  17), S(  12,  46), S(  -6,  59), S( -19, 109),
    S( -17, 106), S(  -4,  57), S(  18,  48), S(   8,  33),
    S( -10,  42), S(  -8,  79), S( -19,  66), S( -32, 121),
    S( -32, 127), S( -23,  80), S(  -8,  95), S( -10,  68),
    S( -28,  56), S( -23,  50), S( -33,  66), S( -18,  70),
    S( -17,  71), S( -19,  63), S( -18,  65), S( -28,  76),
    S( -16,  61), S( -72, 108), S( -19,  65), S( -52, 114),
    S( -54, 120), S( -14,  59), S( -69, 116), S( -11,  73),
    S(   8,  43), S(  19,  47), S(   0,  79), S(   3,  78),
    S(  -3,  89), S(  13,  65), S(  18,  79), S(  21,  56),
};
const int PawnTabless[64]={
    S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0),
    S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0),
    S( -13,   7), S(  -4,   0), S(   1,   4), S(   6,   1),
    S(   3,  10), S(  -9,   4), S(  -9,   3), S( -16,   7),
    S( -21,   5), S( -17,   6), S(  -1,  -6), S(  12, -14),
    S(   8, -10), S(  -4,  -5), S( -15,   7), S( -24,  11),
    S( -14,  16), S( -21,  17), S(   9, -10), S(  10, -24),
    S(   4, -22), S(   4, -10), S( -20,  17), S( -17,  18),
    S( -15,  18), S( -18,  11), S( -16,  -8), S(   4, -30),
    S(  -2, -24), S( -18,  -9), S( -23,  13), S( -17,  21),
    S( -20,  48), S(  -9,  44), S(   1,  31), S(  17,  -9),
    S(  36,  -6), S(  -9,  31), S(  -6,  45), S( -23,  49),
    S( -33, -70), S( -66,  -9), S( -16, -22), S(  65, -23),
    S(  41, -18), S(  39, -14), S( -47,   4), S( -62, -51),
    S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0),
    S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0),
};
const int KingTabless[64]={
    S(  87, -77), S(  67, -49), S(   4,  -7), S(  -9, -26),
    S( -10, -27), S(  -8,  -1), S(  57, -50), S(  79, -82),
    S(  35,   3), S( -27,  -3), S( -41,  16), S( -89,  29),
    S( -64,  26), S( -64,  28), S( -25,  -3), S(  30,  -4),
    S( -44, -19), S( -16, -19), S(  28,   7), S(   0,  35),
    S(  18,  32), S(  31,   9), S( -13, -18), S( -36, -13),
    S( -48, -44), S(  98, -39), S(  71,  12), S( -22,  45),
    S(  12,  41), S(  79,  10), S( 115, -34), S( -59, -38),
    S(  -6, -10), S(  95, -39), S(  39,  14), S( -49,  18),
    S( -27,  19), S(  35,  14), S(  81, -34), S( -50, -13),
    S(  24, -39), S( 123, -22), S( 105,  -1), S( -22, -21),
    S( -39, -20), S(  74, -15), S( 100, -23), S( -17, -49),
    S(   0, -98), S(  28, -21), S(   7, -18), S(  -3, -41),
    S( -57, -39), S(  12, -26), S(  22, -24), S( -15,-119),
    S( -16,-153), S(  49, -94), S( -21, -73), S( -19, -32),
    S( -51, -55), S( -42, -62), S(  53, -93), S( -58,-133),
};
const int KnightTabless[64]={
    S( -31, -38), S(  -6, -24), S( -20, -22), S( -16,  -1),
    S( -11,  -1), S( -22, -19), S(  -8, -20), S( -41, -30),
    S(   1,  -5), S( -11,   3), S(  -6, -19), S(  -1,  -2),
    S(   0,   0), S(  -9, -16), S(  -8,  -3), S(  -6,   1),
    S(   7, -21), S(   8,  -5), S(   7,   2), S(  10,  19),
    S(  10,  19), S(   4,   2), S(   8,  -4), S(   3, -19),
    S(  16,  21), S(  17,  30), S(  23,  41), S(  27,  50),
    S(  24,  53), S(  23,  41), S(  19,  28), S(  13,  26),
    S(  13,  30), S(  23,  30), S(  37,  51), S(  30,  70),
    S(  26,  67), S(  38,  50), S(  22,  33), S(  14,  28),
    S( -24,  25), S(  -5,  37), S(  25,  56), S(  22,  60),
    S(  27,  55), S(  29,  55), S(  -1,  32), S( -19,  25),
    S(  13,  -2), S( -11,  18), S(  27,  -2), S(  37,  24),
    S(  41,  24), S(  40,  -7), S( -13,  16), S(   2,  -2),
    S(-167,  -5), S( -91,  12), S(-117,  41), S( -38,  17),
    S( -18,  19), S(-105,  48), S(-119,  24), S(-165, -17),
};
const int BishopTabless[64]={
    S(   5, -21), S(   1,   1), S(  -1,   5), S(   1,   5),
    S(   2,   8), S(  -6,  -2), S(   0,   1), S(   4, -25),
    S(  26, -17), S(   2, -31), S(  15,  -2), S(   8,   8),
    S(   8,   8), S(  13,  -3), S(   9, -31), S(  26, -29),
    S(   9,   3), S(  22,   9), S(  -5,  -3), S(  18,  19),
    S(  17,  20), S(  -5,  -6), S(  20,   4), S(  15,   8),
    S(   0,  12), S(  10,  17), S(  17,  32), S(  20,  32),
    S(  24,  34), S(  12,  30), S(  15,  17), S(   0,  14),
    S( -20,  34), S(  13,  31), S(   1,  38), S(  21,  45),
    S(  12,  46), S(   6,  38), S(  13,  33), S( -14,  37),
    S( -13,  31), S( -11,  45), S(  -7,  23), S(   2,  40),
    S(   8,  38), S( -21,  34), S(  -5,  46), S(  -9,  35),
    S( -59,  38), S( -49,  22), S( -13,  30), S( -35,  36),
    S( -33,  36), S( -13,  33), S( -68,  21), S( -55,  35),
    S( -66,  18), S( -65,  36), S(-123,  48), S(-107,  56),
    S(-112,  53), S( -97,  43), S( -33,  22), S( -74,  15),
};
const int RookTabless[64]={
    S( -26,  -1), S( -21,   3), S( -14,   4), S(  -6,  -4),
    S(  -5,  -4), S( -10,   3), S( -13,  -2), S( -22, -14),
    S( -70,   5), S( -25, -10), S( -18,  -7), S( -11, -11),
    S(  -9, -13), S( -15, -15), S( -15, -17), S( -77,   3),
    S( -39,   3), S( -16,  14), S( -25,   9), S( -14,   2),
    S( -12,   3), S( -25,   8), S(  -4,   9), S( -39,   1),
    S( -32,  24), S( -21,  36), S( -21,  36), S(  -5,  26),
    S(  -8,  27), S( -19,  34), S( -13,  33), S( -30,  24),
    S( -22,  46), S(   4,  38), S(  16,  38), S(  35,  30),
    S(  33,  32), S(  10,  36), S(  17,  31), S( -14,  43),
    S( -33,  60), S(  17,  41), S(   0,  54), S(  33,  36),
    S(  29,  35), S(   3,  52), S(  33,  32), S( -26,  56),
    S( -18,  41), S( -24,  47), S(  -1,  38), S(  15,  38),
    S(  14,  37), S(  -2,  36), S( -24,  49), S( -12,  38),
    S(  33,  55), S(  24,  63), S(  -1,  73), S(   9,  66),
    S(  10,  67), S(   0,  69), S(  34,  59), S(  37,  56),
};


//Passed Pawn Eval Things
const int PassedPawn[2][2][8] = {
  {{S(   0,   0), S( -39,  -4), S( -43,  25), S( -62,  28),
    S(   8,  19), S(  97,  -4), S( 162,  46), S(   0,   0)},
   {S(   0,   0), S( -28,  13), S( -40,  42), S( -56,  44),
    S(  -2,  56), S( 114,  54), S( 193,  94), S(   0,   0)}},
  {{S(   0,   0), S( -28,  29), S( -47,  36), S( -60,  54),
    S(   8,  65), S( 106,  76), S( 258, 124), S(   0,   0)},
   {S(   0,   0), S( -28,  23), S( -40,  35), S( -55,  60),
    S(   8,  89), S(  95, 166), S( 124, 293), S(   0,   0)}},
};
const int PassedFriendlyDistance[8] = {
    S(   0,   0), S(  -3,   1), S(   0,  -4), S(   5, -13),
    S(   6, -19), S(  -9, -19), S(  -9,  -7), S(   0,   0),
};
const int PassedEnemyDistance[8] = {
    S(   0,   0), S(   5,  -1), S(   7,   0), S(   9,  11),
    S(   0,  25), S(   1,  37), S(  16,  37), S(   0,   0),
};
const int PassedSafePromotionPath = S( -49,  57);
const int PassedProtectedByRook = S( 10,  20);

//Safety Eval Things
const int SafetyKnightWeight    = S(  48,  41);
const int SafetyBishopWeight    = S(  24,  35);
const int SafetyRookWeight      = S(  36,   8);
const int SafetyQueenWeight     = S(  30,   6);
const int SafetyAttackValue     = S(  45,  34);
const int SafetyWeakSquares     = S(  42,  41);
const int SafetyNoEnemyQueens   = S(-237,-259);
const int SafetySafeQueenCheck  = S(  93,  83);
const int SafetySafeRookCheck   = S(  90,  98);
const int SafetySafeBishopCheck = S(  59,  59);
const int SafetySafeKnightCheck = S( 112, 117);
const int SafetyAdjustment      = S( -74, -26);
const int SafetyStorm[2][8] = {
   {S(  -4,  -1), S(  -8,   3), S(   0,   5), S(   1,  -1),
    S(   3,   6), S(  -2,  20), S(  -2,  18), S(   2, -12)},
   {S(   0,   0), S(   1,   0), S(  -1,   4), S(   0,   0),
    S(   0,   5), S(  -1,   1), S(   1,   0), S(   1,   0)},
};
const int SafetyShelter[2][8] = {
   {S(  -2,   7), S(  -1,  13), S(   0,   8), S(   4,   7),
    S(   6,   2), S(  -1,   0), S(   2,   0), S(   0, -13)},
   {S(   0,   0), S(  -2,  13), S(  -2,   9), S(   4,   5),
    S(   3,   1), S(  -3,   0), S(  -2,   0), S(  -1,  -9)},
};

//King Eval Things
const int KingStorm[2][8/2][8] = {
  {{S(  -6,  36), S( 144,  -4), S( -13,  26), S(  -7,   1),
    S( -12,  -3), S(  -8,  -7), S( -19,   8), S( -28,  -2)},
   {S( -17,  60), S(  64,  17), S(  -9,  21), S(   8,  12),
    S(   3,   9), S(   6,  -2), S(  -5,   2), S( -16,   8)},
   {S(   2,  48), S(  15,  30), S( -17,  20), S( -13,  10),
    S(  -1,   6), S(   7,   3), S(   8,  -7), S(   7,   8)},
   {S(  -1,  25), S(  15,  22), S( -31,  10), S( -22,   1),
    S( -15,   4), S(  13, -10), S(   3,  -5), S( -20,   8)}},
  {{S(   0,   0), S( -18, -16), S( -18,  -2), S(  27, -24),
    S(  10,  -6), S(  15, -24), S(  -6,   9), S(   9,  30)},
   {S(   0,   0), S( -15, -42), S(  -3, -15), S(  53, -17),
    S(  15,  -5), S(  20, -28), S( -12, -17), S( -34,   5)},
   {S(   0,   0), S( -34, -62), S( -15, -13), S(   9,  -6),
    S(   6,  -2), S(  -2, -17), S(  -5, -21), S(  -3,   3)},
   {S(   0,   0), S(  -1, -26), S( -27, -19), S( -21,   4),
    S( -10,  -6), S(   7, -35), S(  66, -29), S(  11,  25)}},
};
const int KingDefenders[12] = {
    S( -37,  -3), S( -17,   2), S(   0,   6), S(  11,   8),
    S(  21,   8), S(  32,   0), S(  38, -14), S(  10,  -5),
    S(  12,   6), S(  12,   6), S(  12,   6), S(  12,   6),
};
const int KingShelter[2][8][8] = {
  {{S(  -5,  -5), S(  17, -31), S(  26,  -3), S(  24,   8),
    S(   4,   1), S( -12,   4), S( -16, -33), S( -59,  24)},
   {S(  11,  -6), S(   3, -15), S(  -5,  -2), S(   5,  -4),
    S( -11,   7), S( -53,  70), S(  81,  82), S( -19,   1)},
   {S(  38,  -3), S(   5,  -6), S( -34,   5), S( -17, -15),
    S(  -9,  -5), S( -26,  12), S(  11,  73), S( -16,  -1)},
   {S(  18,  11), S(  25, -18), S(   0, -14), S(  10, -21),
    S(  22, -34), S( -48,   9), S(-140,  49), S(  -5,  -5)},
   {S( -11,  15), S(   1,  -3), S( -44,   6), S( -28,  10),
    S( -24,  -2), S( -35,  -5), S(  40, -24), S( -13,   3)},
   {S(  51, -14), S(  15, -14), S( -24,   5), S( -10, -20),
    S(  10, -34), S(  34, -20), S(  48, -38), S( -21,   1)},
   {S(  40, -17), S(   2, -24), S( -31,  -1), S( -24,  -8),
    S( -31,   2), S( -20,  29), S(   4,  49), S( -16,   3)},
   {S(  10, -20), S(   4, -24), S(  10,   2), S(   2,  16),
    S( -10,  24), S( -10,  44), S(-184,  81), S( -17,  17)}},
  {{S(   0,   0), S( -15, -39), S(   9, -29), S( -49,  14),
    S( -36,   6), S(  -8,  50), S(-168,  -3), S( -59,  19)},
   {S(   0,   0), S(  17, -18), S(   9, -11), S( -11,  -5),
    S(  -1, -24), S(  26,  73), S(-186,   4), S( -32,  11)},
   {S(   0,   0), S(  19,  -9), S(   1, -11), S(   9, -26),
    S(  28,  -5), S( -92,  56), S( -88, -74), S(  -8,   1)},
   {S(   0,   0), S(   0,   3), S(  -6,  -6), S( -35,  10),
    S( -46,  13), S( -98,  33), S(  -7, -45), S( -35,  -5)},
   {S(   0,   0), S(  12,  -3), S(  17, -15), S(  17, -15),
    S(  -5, -14), S( -36,   5), S(-101, -52), S( -18,  -1)},
   {S(   0,   0), S(  -8,  -5), S( -22,   1), S( -16,  -6),
    S(  25, -22), S( -27,  10), S(  52,  39), S( -14,  -2)},
   {S(   0,   0), S(  32, -22), S(  19, -15), S(  -9,  -6),
    S( -29,  13), S(  -7,  23), S( -50, -39), S( -27,  18)},
   {S(   0,   0), S(  16, -57), S(  17, -32), S( -18,  -7),
    S( -31,  24), S( -11,  24), S(-225, -49), S( -30,   5)}},
};
const int KingPawnFileProximity[8] = {
    S(  36,  46), S(  22,  31), S(  13,  15), S(  -8, -22),
    S(  -5, -62), S(  -3, -75), S( -15, -81), S( -12, -75),
};

//Queen Eval Things
const int QueenRelativePin = S( -22, -13);
const int QueenMobility[28] = {
    S(-111,-273), S(-253,-401), S(-127,-228), S( -46,-236),
    S( -20,-173), S(  -9, -86), S(  -1, -35), S(   2,  -1),
    S(   8,   8), S(  10,  31), S(  15,  37), S(  17,  55),
    S(  20,  46), S(  23,  57), S(  22,  58), S(  21,  64),
    S(  24,  62), S(  16,  65), S(  13,  63), S(  18,  48),
    S(  25,  30), S(  38,   8), S(  34, -12), S(  28, -29),
    S(  10, -44), S(   7, -79), S( -42, -30), S( -23, -50),
};

//Rook Eval Things
const int RookFile[2]   = { S(  10,   9), S(  34,   8) };
const int RookOnSeventh = S(  -1,  42);
const int RookMobility[15] = {
    S(-127,-148), S( -56,-127), S( -25, -85), S( -12, -28),
    S( -10,   2), S( -12,  27), S( -11,  42), S(  -4,  46),
    S(   4,  52), S(   9,  55), S(  11,  64), S(  19,  68),
    S(  19,  73), S(  37,  60), S(  97,  15),
};

//Knight Eval Things
const int KnightBehindPawn = S(   3,  28);
const int KnightOutpost[2][2] = {
   {S(  12, -32), S(  40,   0)},
   {S(   7, -24), S(  21,  -3)},
};
const int KnightMobility[9] = {
    S(-104,-139), S( -45,-114), S( -22, -37), S(  -8,   3),
    S(   6,  15), S(  11,  34), S(  19,  38), S(  30,  37),
    S(  43,  17),
};
const int KnightInSiberia[4] = {
    S(  -9,  -6), S( -12, -20), S( -27, -20), S( -47, -19),
};

//Bishop Eval Things
const int BishopMobility[14] = {
    S( -99,-186), S( -46,-124), S( -16, -54), S(  -4, -14),
    S(   6,   1), S(  14,  20), S(  17,  35), S(  19,  39),
    S(  19,  49), S(  27,  48), S(  26,  48), S(  52,  32),
    S(  55,  47), S(  83,   2),
};
const int BishopRammedPawns = S(  -8, -17);
const int bishopPair = S(22,88);
const int BishopLongDiagonal = S(  26,  20);
const int BishopBehindPawn = S(   4,  24);
const int BishopOutpost[2][2] = {
   {S(  16, -16), S(  50,  -3)},
   {S(   9,  -9), S(  -4,  -4)},
};

//Pawn Eval Things
const int PawnCandidatePasser[2][8] = {
   {S(   0,   0), S( -11, -18), S( -16,  18), S( -18,  29),
    S( -22,  61), S(  21,  59), S(   0,   0), S(   0,   0)},
   {S(   0,   0), S( -12,  21), S(  -7,  27), S(   2,  53),
    S(  22, 116), S(  49,  78), S(   0,   0), S(   0,   0)},
};
const int PawnIsolated[8] = {
    S( -13, -12), S(  -1, -16), S(   1, -16), S(   3, -18),
    S(   7, -19), S(   3, -15), S(  -4, -14), S(  -4, -17),
};
const int PawnStacked[2][8] = {
   {S(  10, -29), S(  -2, -26), S(   0, -23), S(   0, -20),
    S(   3, -20), S(   5, -26), S(   4, -30), S(   8, -31)},
   {S(   3, -14), S(   0, -15), S(  -6,  -9), S(  -7, -10),
    S(  -4,  -9), S(  -2, -10), S(   0, -13), S(   0, -17)},
};
const int PawnBackwards[2][8] = {
   {S(   0,   0), S(   0,  -7), S(   7,  -7), S(   6, -18),
    S(  -4, -29), S(   0,   0), S(   0,   0), S(   0,   0)},
   {S(   0,   0), S(  -9, -32), S(  -5, -30), S(   3, -31),
    S(  29, -41), S(   0,   0), S(   0,   0), S(   0,   0)},
};
const int PawnConnected32[32] = {
    S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0),
    S(  -1, -11), S(  12,  -4), S(   0,  -2), S(   6,   8),
    S(  14,   0), S(  20,  -6), S(  19,   3), S(  17,   8),
    S(   6,  -1), S(  20,   1), S(   6,   3), S(  14,  10),
    S(   8,  14), S(  21,  17), S(  31,  23), S(  25,  18),
    S(  45,  40), S(  36,  64), S(  58,  74), S(  64,  88),
    S( 108,  35), S( 214,  45), S( 216,  70), S( 233,  61),
    S(   0,   0), S(   0,   0), S(   0,   0), S(   0,   0),
};

//Chess960 specific Eval Things
const int BishopTrapped[2] = {S(-10,-15),S(-14,-20)};
const int RookTrapped      = S(-5,-22);

//tempo
const int tempo = 20;


/*Functions*/
INLINE int RewardForOppKingDistanceFromCenter(int oppKing,int friendKing){
        int evaluation=0;
        int oppRank=ranksBoard[oppKing];
        int oppFile=filesBoard[oppKing];

        evaluation+=(MAX(3-oppFile,oppFile-4)+MAX(3-oppRank,oppRank-4));
        evaluation-=(abs(filesBoard[friendKing]-oppFile)+abs(ranksBoard[friendKing]-oppRank));

        return S(0,evaluation*2);

}
INLINE int evaluateChess960Trapped(S_BOARD *pos,int color){

    U64 ourBishop    = color==WHITE ? pos->bitboards[wB] : pos->bitboards[bB];
    U64 ourPawns     = color==WHITE ? pos->bitboards[wP] : pos->bitboards[bP];
    U64 ourRooks     = color==WHITE ? pos->bitboards[wR] : pos->bitboards[bR];
    U64 ourKings     = color==WHITE ? pos->bitboards[wK] : pos->bitboards[bK];
    U64 ourOccupancy = color==WHITE ? pos->occupancy[WHITE] : pos->occupancy[BLACK];

    int flag;
    int eval=0;
    if(color==WHITE){

        //bishop trapped
        if(testBit(ourBishop,A1) && testBit(ourPawns,B2)){
            flag=!!testBit(ourOccupancy,B3);
            eval+=BishopTrapped[flag];
        }
        if(testBit(ourBishop,H1) && testBit(ourPawns,G2)){
            flag=!!testBit(ourOccupancy,G3);
            eval+=BishopTrapped[flag];
        }

        //rook trapped
        if(testBit(ourRooks,A1) && (testBit(ourKings,B1) || testBit(ourKings,C1)) && testBit(ourPawns,A2)){
            eval+=RookTrapped;
        }else if(testBit(ourRooks,H1) && (testBit(ourKings,G1) || testBit(ourKings,F1)) && testBit(ourPawns,H2)){
            eval+=RookTrapped;
        }


        return eval;
    }
    else{

        //bishop trapped
        if(testBit(ourBishop,A8) && testBit(ourPawns,B7)){
            flag=!!testBit(pos->occupancy[BLACK],B6);
            eval+=BishopTrapped[flag];
        }
        if(testBit(ourBishop,H8) && testBit(ourPawns,G7)){
            flag=!!testBit(ourOccupancy,G6);
            eval+=BishopTrapped[flag];
        }

        //rook trapped
        if(testBit(ourRooks,A8) && (testBit(ourKings,B8) || testBit(ourKings,C8)) && testBit(ourPawns,A7)){
            eval+=RookTrapped;
        }else if(testBit(ourRooks,H8) && (testBit(ourKings,G8) || testBit(ourKings,F8)) && testBit(ourPawns,H7)){
            eval+=RookTrapped;
        }

        return eval;
    }

    return eval;
}
INLINE int evaluateKingsPawns(S_BOARD *pos, int colour) {

    const int US = colour, THEM = !colour;

    int dist, blocked;
    int eval=0;

    U64 pawns       = pos->bitboards[bP] | pos->bitboards[wP];
    U64 myPawns     = pawns & pos->occupancy[  US];
    U64 enemyPawns  = pawns & pos->occupancy[THEM];

    int kingSq = pos->kingSq[US];

    dist = kingPawnFileDistance(pawns, kingSq);
    eval += KingPawnFileProximity[dist];

    for (int file = MAX(0, filesBoard[kingSq] - 1); file <= MIN(8 - 1, filesBoard[kingSq] + 1); file++) {

        U64 ours = myPawns & FileBBMask[file] & forwardRanksMasks(US, ranksBoard[kingSq]);
        int ourDist = !ours ? 7 : abs(ranksBoard[kingSq] - ranksBoard[backmost(US, ours)]);

        U64 theirs = enemyPawns & FileBBMask[file] & forwardRanksMasks(US, ranksBoard[kingSq]);
        int theirDist = !theirs ? 7 : abs(ranksBoard[kingSq] - ranksBoard[backmost(US, theirs)]);

        eval += KingShelter[file == filesBoard[kingSq]][file][ourDist];

        pos->pkSafety[US] += SafetyShelter[file == filesBoard[kingSq]][ourDist];

        blocked = (ourDist != 7 && (ourDist == theirDist - 1));
        eval += KingStorm[blocked][mirrorFile(file)][theirDist];

        pos->pkSafety[US] += SafetyStorm[blocked][theirDist];
    }

    return eval;
}
INLINE void EvalPawn(S_BOARD *pos){
    int sq,pce;
    int file,rank;
    int flag;
    U64 stoppers,threats,backups,neighbors,supports;
    U64 pushThreats,pushSupport,leftovers;
    U64 enemyPawns,friendlyPawn,bitboard;

    pce=wP;
    enemyPawns=pos->bitboards[bP];
    friendlyPawn=pos->bitboards[wP];
    int US=WHITE;
    int THEM=!US;
    int FORWARD=8;
    bitboard=pos->bitboards[pce];
    while(bitboard){

        sq=LSBINDEX(bitboard);
        file=filesBoard[sq];
        rank=ranksBoard[sq];

        //psq table
        //pos->pawnEval[WHITE]+=PawnTabless[sq];

        neighbors=friendlyPawn & IsolatedMask[file];
        backups=friendlyPawn & pawnPassedMark(THEM,sq);
        stoppers=enemyPawns & pawnPassedMark(US,sq);
        threats=enemyPawns & pawnAttacks(US,sq);
        supports=friendlyPawn & pawnAttacks(THEM,sq);
        pushThreats=enemyPawns & pawnAttacks(US,sq+FORWARD);
        pushSupport=friendlyPawn & pawnAttacks(THEM,sq+FORWARD);
        leftovers=threats^stoppers^pushThreats;

        //passed pawn
        if(!stoppers){
            SETBIT(pos->passers[WHITE],sq);
        }

        //candidate passers
        else if(!leftovers && COUNTBIT(pushSupport)>=COUNTBIT(pushThreats)){
            flag=COUNTBIT(supports)>=COUNTBIT(threats);
            pos->pawnEval[WHITE]+=PawnCandidatePasser[flag][rank];
        }

        //isolated pawn
        if(!neighbors && !threats){
            pos->pawnEval[WHITE]+=PawnIsolated[file];
        }

        //doubled pawn
        if(several(FileBBMask[file] & pos->bitboards[wP])){
            flag=(stoppers && (threats || neighbors)) ||
                (stoppers & ~ForwardFileMasks[WHITE][sq]);
            pos->pawnEval[WHITE]+=PawnStacked[flag][file];
        }

        //backwards pawn
        if(!backups && pushThreats && neighbors){
            flag=!(FileBBMask[file] & pos->bitboards[bP]);
            pos->pawnEval[WHITE]+=PawnBackwards[flag][rank];
        }

        //connected pawns
        else if(PawnConnectedMasks[WHITE][sq] & pos->bitboards[wP]){
            pos->pawnEval[WHITE]+=PawnConnected32[relativeSquare32(WHITE,sq)];
        }
        POPBIT(bitboard,sq);
    }

    pce=bP;
    enemyPawns=pos->bitboards[wP];
    friendlyPawn=pos->bitboards[bP];
    US=BLACK;
    THEM=!US;
    FORWARD=-FORWARD;
    bitboard=pos->bitboards[pce];
    while(bitboard){

        sq=LSBINDEX(bitboard);
        file=filesBoard[sq];
        rank=ranksBoard[sq];

        //psq table
        //pos->pawnEval[BLACK]+=PawnTabless[MIRROR64(sq)];

        neighbors=friendlyPawn & IsolatedMask[file];
        backups=friendlyPawn & pawnPassedMark(THEM,sq);
        stoppers=enemyPawns & pawnPassedMark(US,sq);
        threats=enemyPawns & pawnAttacks(US,sq);
        supports=friendlyPawn & pawnAttacks(THEM,sq);
        pushThreats=enemyPawns & pawnAttacks(US,sq+FORWARD);
        pushSupport=friendlyPawn & pawnAttacks(THEM,sq+FORWARD);
        leftovers=threats^stoppers^pushThreats;

        //passed pawn
        if(!stoppers){
            SETBIT(pos->passers[BLACK],sq);
        }

        //candidate passers
        else if(!leftovers && COUNTBIT(pushSupport)>=COUNTBIT(pushThreats)){
            flag=COUNTBIT(supports)>=COUNTBIT(threats);
            pos->pawnEval[BLACK]+=PawnCandidatePasser[flag][7-rank];
        }

        //isolated pawn
        if(!neighbors && !threats){
            pos->pawnEval[BLACK]+=PawnIsolated[file];
        }

        //doubled pawn
        if(several(FileBBMask[file] & pos->bitboards[bP])){
            flag=(stoppers && (threats || neighbors)) ||
                (stoppers & ~ForwardFileMasks[BLACK][sq]);
            pos->pawnEval[BLACK]+=PawnStacked[flag][file];
        }

        //backwards pawn
        if(!backups && pushThreats && neighbors){
            flag=!(FileBBMask[file] & pos->bitboards[wP]);
            pos->pawnEval[BLACK]+=PawnBackwards[flag][7-rank];
        }

        //connected pawns
        else if(PawnConnectedMasks[BLACK][sq] & pos->bitboards[bP]){
            pos->pawnEval[BLACK]+=PawnConnected32[relativeSquare32(BLACK,sq)];
        }
        POPBIT(bitboard,sq);
    }
}
INLINE int evalKnights(S_BOARD *pos,int color){
    int eval=0;
    int count;
    int kingDistance;
    int side=color;
    int pce,sq;
    int defended,outside;
    U64 attacks,bitboard;
    U64 pawns=pos->bitboards[wP] | pos->bitboards[bP];
    U64 enemyPawns=color==WHITE ? pos->bitboards[bP]:pos->bitboards[wP];
    pce=color==WHITE ? wN:bN;
    bitboard=pos->bitboards[pce];
    while(bitboard){

        sq=LSBINDEX(bitboard);

        //piece square tables
        //eval+=color==WHITE ? KnightTabless[sq]:KnightTabless[MIRROR64(sq)];

        //knight outposts
        if(testBit(getOutpostRanksMasks(color),sq) &&
           !(getOutpostSquareMasks(color,sq) & enemyPawns)){
            outside  = testBit(FileBBMask[FILE_A] | FileBBMask[FILE_H], sq);
            defended = testBit(pos->attacks_array_pawns[color], sq);
            eval += KnightOutpost[outside][defended];
           }

        //knight behind a pawn
        if (testBit(pawnAdvance(pawns, 0ULL, !color), sq)) {
            eval += KnightBehindPawn;
        }

        //knight in siberia
        kingDistance = MIN(distanceBetween(sq, pos->kingSq[!color]), distanceBetween(sq, pos->kingSq[color]));
        if (kingDistance >= 4) {
            eval += KnightInSiberia[kingDistance - 4];
        }

        attacks = knight_attacks[sq];
        pos->attacks_array_minors[side] |= attacks;
        pos->attackedBy2[side] |= attacks & pos->attacked[side];
        pos->attacked[side] |= attacks;
        pos->attackedByKnights[side] |= attacks;

        //mobility
        count=COUNTBIT(pos->mobilityAreas[side] & attacks);
        eval+=KnightMobility[count];

        //king safety calculation
        if((attacks &= pos->kingAreas[!side] & ~pos->pawnAttackedBy2[!side])){
            pos->kingAttacksCount[!side]+=COUNTBIT(attacks);
            pos->attCnt[!side]+=1;
            pos->attWeight[!side]+=SafetyKnightWeight;
        }

        POPBIT(bitboard,sq);
    }
    return eval;
}
INLINE int evalBishops(S_BOARD *pos,int color){

    int eval=0;
    int side=color;
    int count,sq;
    int defended,outside;
    int pce=color==WHITE ? wB:bB;
    U64 attacks,bitboard;
    U64 enemyPawns=color==WHITE ? pos->bitboards[bP]:pos->bitboards[wP];
    U64 pawns=pos->bitboards[wP] | pos->bitboards[bP];
    bitboard=pos->bitboards[pce];

    //bishop pair
    if((bitboard & lightsquaresBB) && (bitboard & darksquaresBB)){
        eval+=bishopPair;
    }

    while(bitboard){
        sq=LSBINDEX(bitboard);

        //piece square tables
        //eval+=color==WHITE ? BishopTabless[sq]:BishopTabless[MIRROR64(sq)];

        //rammed pawns of same color
        count=COUNTBIT(pos->rammedPawns[color] & squaresOfMatchingColour(sq));
        eval+=count*BishopRammedPawns;

        // Long diagonal and center square control
        if (testBit(LONG_DIAGONALS & ~CENTER_SQUARES, sq)
            && several(get_bishop_attacks(sq,pawns) & CENTER_SQUARES)) {
            eval += BishopLongDiagonal;
        }

        //bishop outposts
        if(testBit(getOutpostRanksMasks(color),sq) &&
           !(getOutpostSquareMasks(color,sq) & enemyPawns)){
            outside  = testBit(FileBBMask[FILE_A] | FileBBMask[FILE_H], sq);
            defended = testBit(pos->attacks_array_pawns[color], sq);
            eval += BishopOutpost[outside][defended];
           }

        //bishop behind a pawn
        if (testBit(pawnAdvance(pawns, 0ull, !color), sq)) {
            eval += BishopBehindPawn;
        }

        attacks = get_bishop_attacks(sq, pos->occupiedMinusBishops[side]);
        pos->attacks_array_minors[side] |= attacks;
        pos->attackedBy2[side] |= attacks & pos->attacked[side];
        pos->attacked[side] |= attacks;
        pos->attackedByBishops[side] |= attacks;

        //mobility
        count=COUNTBIT(pos->mobilityAreas[side] & attacks);
        eval+=BishopMobility[count];

        //king safety calculation
        if((attacks &= pos->kingAreas[!side] & ~pos->pawnAttackedBy2[!side])){
            pos->kingAttacksCount[!side]+=COUNTBIT(attacks);
            pos->attCnt[!side]+=1;
            pos->attWeight[!side]+=SafetyBishopWeight;
        }

        POPBIT(bitboard,sq);
    }

    return eval;
}
INLINE int evalKing(S_BOARD *pos,int color){

    int US=color;
    int THEM=!US;
    int sq=pos->kingSq[US];
    int safety, mg, eg;
    int eval=0;

    U64 enemyQueens=(pos->bitboards[bQ] | pos->bitboards[wQ]) & pos->occupancy[THEM];
    U64 pawns   = pos->bitboards[wP] | pos->bitboards[bP];
    U64 knights = pos->bitboards[wN] | pos->bitboards[bN];
    U64 bishops = pos->bitboards[wB] | pos->bitboards[bB];

    //endgame king usage
    eval+=RewardForOppKingDistanceFromCenter(pos->kingSq[THEM],pos->kingSq[US]);

    //piece square tables
    //eval+=color==WHITE ? KingTabless[sq]:KingTabless[MIRROR64(sq)];

    //king defenders
    U64 defenders  = (pawns & pos->occupancy[US])
                        | (knights & pos->occupancy[US])
                        | (bishops & pos->occupancy[US]);

    int count = COUNTBIT(defenders & pos->kingAreas[US]);
    eval += KingDefenders[count];

    if(pos->attCnt[US]>1-COUNTBIT(enemyQueens)){

        U64 weak = pos->attacked[THEM]
                    & ~pos->attackedBy2[US]
                    & (~pos->attacked[US] | pos->attacks_array_queens[US] | king_attacks[sq]);
        int scaledAttackCounts = 9.0 * pos->kingAttacksCount[US] / COUNTBIT(pos->kingAreas[US]);

        U64 safe = ~pos->occupancy[THEM]
                    & (~pos->attacked[US] | (weak & pos->attackedBy2[THEM]));

        U64 occupied = pos->occupancy[BOTH];
        U64 knightThreats = knight_attacks[sq];
        U64 bishopThreats = get_bishop_attacks(sq,occupied);
        U64 rookThreats   = get_rook_attacks(sq,occupied);
        U64 queenThreats  = rookThreats | bishopThreats;

        U64 knightChecks = knightThreats & safe & pos->attackedByKnights[THEM];
        U64 rookChecks   = rookThreats & safe & pos->attacks_array_rooks[THEM];
        U64 bishopChecks = bishopThreats & safe & pos->attackedByBishops[THEM];
        U64 queenChecks  = queenThreats & safe & pos->attacks_array_queens[THEM];

        safety=pos->attWeight[US];

        safety+=SafetyAttackValue     * scaledAttackCounts
                + SafetyWeakSquares     * COUNTBIT(weak & pos->kingAreas[US])
                + SafetyNoEnemyQueens   * !enemyQueens
                + SafetySafeQueenCheck  * COUNTBIT(queenChecks)
                + SafetySafeRookCheck   * COUNTBIT(rookChecks)
                + SafetySafeBishopCheck * COUNTBIT(bishopChecks)
                + SafetySafeKnightCheck * COUNTBIT(knightChecks)
                + pos->pkSafety[US]
                + SafetyAdjustment;

        mg = ScoreMG(safety), eg = ScoreEG(safety);
        eval += MakeScore(-mg * MAX(0, mg) / 720, -MAX(0, eg) / 20);
    }

    return eval;
}
INLINE int evalQueens(S_BOARD *pos,int color){

    int sq,count;
    int side=color;
    U64 attacks;
    int eval=0;
    int pce=color==WHITE ? wQ:bQ;
    U64 bitboard=pos->bitboards[pce];

    while(bitboard){

        sq=LSBINDEX(bitboard);

        //piece square tables
        //eval+=color==WHITE ? QueenTabless[sq]:QueenTabless[MIRROR64(sq)];

        //pin risk
        if (discoveredAttacks(pos, sq, color)) {
            eval += QueenRelativePin;
        }

        attacks = get_queen_attacks(sq, pos->occupancy[BOTH]);
        pos->attacks_array_queens[side] |= attacks;
        pos->attackedBy2[side] |= attacks & pos->attacked[side];
        pos->attacked[side] |= attacks;

        //mobility
        count=COUNTBIT(pos->mobilityAreas[side] & attacks);
        eval+=QueenMobility[count];

        //king safety calculation
        if((attacks &= pos->kingAreas[!side] & ~pos->pawnAttackedBy2[!side])){
            pos->kingAttacksCount[!side]+=COUNTBIT(attacks);
            pos->attCnt[!side]+=1;
            pos->attWeight[!side]+=SafetyQueenWeight;
        }

        POPBIT(bitboard,sq);
    }
    return eval;
}
INLINE int evalRooks(S_BOARD *pos,int color){

    int sq,count;
    int open;
    int side=color;
    int eval=0;
    int pce=color==WHITE ? wR:bR;
    U64 attacks;
    U64 pawns=pos->bitboards[wP] | pos->bitboards[bP];
    U64 myPawns=color==WHITE ? pos->bitboards[wP]:pos->bitboards[bP];
    U64 notMyPawns=pos->occupancy[!color] & pawns;
    U64 bitboard=pos->bitboards[pce];
    while(bitboard){

        sq=LSBINDEX(bitboard);

        //piece square tables
        //eval+=color==WHITE ? RookTabless[sq]:RookTabless[MIRROR64(sq)];

        //open files
        if (!(myPawns & FileBBMask[filesBoard[sq]])) {
            open = !(notMyPawns & FileBBMask[filesBoard[sq]]);
            eval += RookFile[open];
        }

        //rook on seventh
        if ( relativeRankOf(color, sq) == RANK_7
            && relativeRankOf(color, pos->kingSq[!color]) >= RANK_7) {
            eval += RookOnSeventh;
        }

        attacks = get_rook_attacks(sq, pos->occupiedMinusRooks[side]);
        pos->attacks_array_rooks[side] |= attacks;
        pos->attackedBy2[side] |= attacks & pos->attacked[side];
        pos->attacked[side] |= attacks;

        //mobility
        count=COUNTBIT(pos->mobilityAreas[side] & attacks);
        eval+=RookMobility[count];

        //king safety calculation
        if((attacks &= pos->kingAreas[!side] & ~pos->pawnAttackedBy2[!side])){
            pos->kingAttacksCount[!side]+=COUNTBIT(attacks);
            pos->attCnt[!side]+=1;
            pos->attWeight[!side]+=SafetyRookWeight;
        }

        POPBIT(bitboard,sq);
    }
    return eval;
}
INLINE int ScaleFactor(S_BOARD *pos,int eval){

    const U64 pawns   = pos->bitboards[wP] | pos->bitboards[bP];
    const U64 knights = pos->bitboards[wN] | pos->bitboards[bN];
    const U64 bishops = pos->bitboards[wB] | pos->bitboards[bB];
    const U64 rooks   = pos->bitboards[wR] | pos->bitboards[bR];
    const U64 queens  = pos->bitboards[wQ] | pos->bitboards[bQ];

    const U64 minors  = knights | bishops;
    const U64 pieces  = knights | bishops | rooks;

    const U64 white   = pos->occupancy[WHITE];
    const U64 black   = pos->occupancy[BLACK];

    const U64 weak    = eval < 0 ? white : black;
    const U64 strong  = eval < 0 ? black : white;


    // Check for opposite coloured bishops
    if (   onlyOne(white & bishops)
        && onlyOne(black & bishops)
        && onlyOne(bishops & lightsquaresBB)) {

        // Scale factor for OCB + knights
        if ( !(rooks | queens)
            && onlyOne(white & knights)
            && onlyOne(black & knights))
            return SCALE_OCB_ONE_KNIGHT;

        // Scale factor for OCB + rooks
        if ( !(knights | queens)
            && onlyOne(white & rooks)
            && onlyOne(black & rooks))
            return SCALE_OCB_ONE_ROOK;

        // Scale factor for lone OCB
        if (!(knights | rooks | queens))
            return SCALE_OCB_BISHOPS_ONLY;
    }

    // Lone Queens are weak against multiple pieces
    if (onlyOne(queens) && several(pieces) && pieces == (weak & pieces))
        return SCALE_LONE_QUEEN;

    // Lone Minor vs King + Pawns should never be won
    if ((strong & minors) && COUNTBIT(strong) == 2)
        return SCALE_DRAW;

    // Scale up lone pieces with massive pawn advantages
    if (   !queens
        && !several(pieces & white)
        && !several(pieces & black)
        &&  COUNTBIT(strong & pawns) - COUNTBIT(weak & pawns) > 2)
        return SCALE_LARGE_PAWN_ADV;

    return SCALE_NORMAL;

}
INLINE int evaluatePassed(S_BOARD *pos, int colour) {

    const int US = colour, THEM = !colour;

    int sq, rank, dist, flag, canAdvance, safeAdvance, eval = 0;

    U64 bitboard;
    //U64 ourRooks = colour==WHITE ? pos->bitboards[wR]:pos->bitboards[bR];
    U64 myPassers = colour==WHITE ? pos->passers[WHITE]:pos->passers[BLACK];
    U64 occupied  = pos->occupancy[BOTH];
    U64 tempPawns = myPassers;

    while (tempPawns) {

        sq = LSBINDEX(tempPawns);
        POPBIT(tempPawns,sq);
        rank = relativeRankOf(US, sq);
        bitboard = pawnAdvance(1ull << sq, 0ull, US);

        canAdvance = !(bitboard & occupied);
        safeAdvance = !(bitboard & pos->attacked[THEM]);
        eval += PassedPawn[canAdvance][safeAdvance][rank];

        if (several(ForwardFileMasks[US][sq] & myPassers)) continue;

        dist = distanceBetween(sq, pos->kingSq[US]);
        eval += dist * PassedFriendlyDistance[rank];

        dist = distanceBetween(sq, pos->kingSq[THEM]);
        eval += dist * PassedEnemyDistance[rank];

        //Apply a bonus if a rook is beneath this passed pawn
        //if(ForwardFileMasks[THEM][sq] & ourRooks)eval+=PassedProtectedByRook;

        //Add bonus the lesser the material on board
        //eval+=S(0,pos->gamePhase);

        // Apply a bonus when the path to promoting is uncontested
        bitboard = forwardRanksMasks(US, ranksBoard[sq]) & FileBBMask[filesBoard[sq]];
        flag = !(bitboard & (pos->occupancy[THEM] | pos->attacked[THEM]));
        eval += flag * PassedSafePromotionPath;
    }

    return eval;
}
INLINE int materialScore(S_BOARD *pos){

    //init
    int whiteMaterial=0;
    int blackMaterial=0;

    //pawns
    whiteMaterial+=COUNTBIT(pos->bitboards[wP]) * PiecesVal[PAWN];
    blackMaterial+=COUNTBIT(pos->bitboards[bP]) * PiecesVal[PAWN];

    //knights
    whiteMaterial+=COUNTBIT(pos->bitboards[wN]) * PiecesVal[KNIGHT];
    blackMaterial+=COUNTBIT(pos->bitboards[bN]) * PiecesVal[KNIGHT];

    //bishops
    whiteMaterial+=COUNTBIT(pos->bitboards[wB]) * PiecesVal[BISHOP];
    blackMaterial+=COUNTBIT(pos->bitboards[bB]) * PiecesVal[BISHOP];

    //rooks
    whiteMaterial+=COUNTBIT(pos->bitboards[wR]) * PiecesVal[ROOK];
    blackMaterial+=COUNTBIT(pos->bitboards[bR]) * PiecesVal[ROOK];

    //queens
    whiteMaterial+=COUNTBIT(pos->bitboards[wQ]) * PiecesVal[QUEEN];
    blackMaterial+=COUNTBIT(pos->bitboards[bQ]) * PiecesVal[QUEEN];

    return whiteMaterial - blackMaterial;

}
INLINE void initEvalThings(S_BOARD *pos){
    int index;
    //king sq
    pos->kingSq[WHITE]=LSBINDEX(pos->bitboards[wK]);
    pos->kingSq[BLACK]=LSBINDEX(pos->bitboards[bK]);
    //pos->pkEval = 0;

    U64 white=pos->occupancy[WHITE];
    U64 black=pos->occupancy[BLACK];
    U64 pawns=pos->bitboards[wP] | pos->bitboards[bP];
    U64 kings=pos->bitboards[wK] | pos->bitboards[bK];
    U64 rooks=pos->bitboards[wR] | pos->bitboards[bR];
    U64 bishops=pos->bitboards[wB] | pos->bitboards[bB];
    for(index=0;index<2;++index){
        pos->attCnt[index]=0;
        pos->passers[index]=0ULL;
        pos->pkSafety[index]=0;
        pos->attacks_array_minors[index]=0ULL;
        pos->attacks_array_rooks[index]=0ULL;
        pos->attacks_array_queens[index]=0ULL;
        pos->attackedByBishops[index]=0ULL;
        pos->attackedByKnights[index]=0ULL;
        pos->attWeight[index]=0;
        pos->kingAttacksCount[index]=0;
        pos->pawnEval[index]=0;
    }

    //king areas
    pos->kingAreas[WHITE] = KingAreaMasks(WHITE,pos->kingSq[WHITE]);
    pos->kingAreas[BLACK] = KingAreaMasks(BLACK,pos->kingSq[BLACK]);

    //init pawn things
    pos->attacks_array_pawns[WHITE]    = pawnAttackSpan(white & pawns, ~0ull, WHITE);
    pos->attacks_array_pawns[BLACK]    = pawnAttackSpan(black & pawns, ~0ull, BLACK);
    pos->pawnAttackedBy2[WHITE] = pawnAttackDouble(white & pawns, ~0ull, WHITE);
    pos->pawnAttackedBy2[BLACK] = pawnAttackDouble(black & pawns, ~0ull, BLACK);
    pos->rammedPawns[WHITE]=pawnAdvance(pos->occupancy[BLACK] & pawns, ~(pos->occupancy[WHITE] & pawns), BLACK);
    pos->rammedPawns[BLACK]=pawnAdvance(pos->occupancy[WHITE] & pawns, ~(pos->occupancy[BLACK] & pawns), WHITE);
    U64 BlockedPawnWhite  = pawnAdvance(white | black, ~(white & pawns), BLACK);
    U64 BlockedPawnBlack  = pawnAdvance(white | black, ~(black & pawns), WHITE);

    //init attacks
    pos->attacked[WHITE]=king_attacks[pos->kingSq[WHITE]];
    pos->attacked[BLACK]=king_attacks[pos->kingSq[BLACK]];
    pos->attackedBy2[WHITE] = pos->attacks_array_pawns[WHITE] & pos->attacked[WHITE];
    pos->attackedBy2[BLACK] = pos->attacks_array_pawns[BLACK] & pos->attacked[BLACK];
    pos->attacked[WHITE] |= pos->attacks_array_pawns[WHITE];
    pos->attacked[BLACK] |= pos->attacks_array_pawns[BLACK];

    //mobility areas
    pos->mobilityAreas[WHITE] = ~(pos->attacks_array_pawns[BLACK] | (white & kings) | BlockedPawnWhite);
    pos->mobilityAreas[BLACK] = ~(pos->attacks_array_pawns[WHITE] | (black & kings) | BlockedPawnBlack);

    //mobilityMinus
    pos->occupiedMinusBishops[WHITE]=(white | black) ^ (white & bishops);
    pos->occupiedMinusBishops[BLACK]=(white | black) ^ (black & bishops);
    pos->occupiedMinusRooks[WHITE]=(white | black) ^ (white & rooks);
    pos->occupiedMinusRooks[BLACK]=(white | black) ^ (black & rooks);
}
INLINE int evaluatePieces(S_BOARD *pos){
    int eval=0;

    eval+= pos->pawnEval[WHITE] - pos->pawnEval[BLACK];
    eval+= evalKnights(pos,WHITE)- evalKnights(pos,BLACK);
    eval+= evalBishops(pos,WHITE)- evalBishops(pos,BLACK);
    eval+= evalRooks(pos,WHITE)  - evalRooks(pos,BLACK);
    eval+= evalQueens(pos,WHITE) - evalQueens(pos,BLACK);
    eval+= evaluateKingsPawns(pos,WHITE)-evaluateKingsPawns(pos,BLACK);
    eval+= evalKing(pos,WHITE) - evalKing(pos,BLACK);
    eval+= evaluatePassed(pos,WHITE) - evaluatePassed(pos,BLACK);
    //eval+= evaluateSpace(pos,WHITE)-evaluateSpace(pos,BLACK);
    //eval+= evaluateThreats(pos,WHITE)-evaluateThreats(pos,BLACK);

    //if chess960
    if(pos->chess960){
        eval+=evaluateChess960Trapped(pos,WHITE)-evaluateChess960Trapped(pos,BLACK);
    }

    return eval;
}
INLINE int getPSQT(S_BOARD *pos,int col){
    int PSQT=0;
    U64 bitboard;
    int sq;
    int start = col==WHITE ? wP:bP;
    int end = col==WHITE ? wK:bK;

    for (int piece=start;piece<=end;++piece){
        bitboard = pos->bitboards[piece];
        while(bitboard){
            sq = LSBINDEX(bitboard);

            if(piece==wP || piece==bP){
                PSQT+=col==WHITE? PawnTabless[sq]:PawnTabless[MIRROR64(sq)];
            }else if(piece==wN || piece==bN){
                PSQT+=col==WHITE? KnightTabless[sq]:KnightTabless[MIRROR64(sq)];
            }else if(piece==wR || piece==bR){
                PSQT+=col==WHITE? RookTabless[sq]:RookTabless[MIRROR64(sq)];
            }else if(piece==wB || piece==bB){
                PSQT+=col==WHITE? BishopTabless[sq]:BishopTabless[MIRROR64(sq)];
            }else if(piece==wQ || piece==bQ){
                PSQT+=col==WHITE? QueenTabless[sq]:QueenTabless[MIRROR64(sq)];
            }else if(piece==wK || piece==bK){
                PSQT+=col==WHITE? KingTabless[sq]:KingTabless[MIRROR64(sq)];
            }


            POPBIT(bitboard,sq);
        }
    }

    return PSQT;
}
INLINE int getClassicalEval(S_BOARD *pos){

    int eval=0;

    // store and probe pawns
    if (!ProbePawnKingEval(pos)){
        EvalPawn(pos);
        StorePawnKingEval(pos);
    }

    //pieces
    eval+=evaluatePieces(pos);
    //others
    eval+=pos->contempt+pos->psqtmat;

    return eval;
}


int EvalPosition(S_BOARD *pos){

    int score;

    //null move recognizer
    if(pos->ply > 0 && pos->moveStack[pos->ply - 1]==NULLMOVE){
        return -pos->eval_stack[pos->ply - 1] + 2*tempo;
    }

    //probing cached eval
    int hashedEval=ProbeTTEval(pos);
    if(hashedEval != VALUE_NONE){
        //score = pos->USE_NNUE ? hashedEval:(pos->side==WHITE ? hashedEval:-hashedEval)+tempo;
        score = (pos->side==WHITE ? hashedEval:-hashedEval)+tempo;
        return pos->useFiftyMoveRule ? score * (100-pos->fiftyMove)/100:score;
    }

    //Initialization
    initEvalThings(pos);

    //get classical eval
    int eval = getClassicalEval(pos);

    //scale factor
    int factor=ScaleFactor(pos,ScoreEG(eval));

    //phase
    pos->gamePhase = getGamePhase(pos);

    //interpolate
    score = (ScoreMG(eval) * (256-pos->gamePhase)
            +  ScoreEG(eval) * pos->gamePhase * factor / SCALE_NORMAL) / 256;

    //store score
    StoreTTEval(pos,score);

    ASSERT((pos->side==WHITE ? score : -score)+tempo < INFINITE);
    score = (pos->side==WHITE ? score : -score)+tempo;
    return pos->useFiftyMoveRule ? score * (100-pos->fiftyMove)/100:score;
}

void initPQSTMAT(){
    int sq2;
    for(int sq=0;sq < 64;++sq){

        sq2 = MIRROR64(sq);

        PSQTMATTABLE[wP][sq] = PiecesVal[PAWN] + PawnTabless[sq];
        PSQTMATTABLE[wN][sq] = PiecesVal[KNIGHT] + KnightTabless[sq];
        PSQTMATTABLE[wB][sq] = PiecesVal[BISHOP] + BishopTabless[sq];
        PSQTMATTABLE[wR][sq] = PiecesVal[ROOK] + RookTabless[sq];
        PSQTMATTABLE[wQ][sq] = PiecesVal[QUEEN] + QueenTabless[sq];
        PSQTMATTABLE[wK][sq] = PiecesVal[KING] + KingTabless[sq];

        PSQTMATTABLE[bP][sq] = -PiecesVal[PAWN] - PawnTabless[sq2];
        PSQTMATTABLE[bN][sq] = -PiecesVal[KNIGHT] - KnightTabless[sq2];
        PSQTMATTABLE[bB][sq] = -PiecesVal[BISHOP] - BishopTabless[sq2];
        PSQTMATTABLE[bR][sq] = -PiecesVal[ROOK] - RookTabless[sq2];
        PSQTMATTABLE[bQ][sq] = -PiecesVal[QUEEN] - QueenTabless[sq2];
        PSQTMATTABLE[bK][sq] = -PiecesVal[KING] - KingTabless[sq2];
    }
}
