#ifndef DEFS_H
#define DEFS_H

#include "stdlib.h"
#include <stdalign.h>
#include <stdint.h>

/////////////////////////////////////////
//DEBUG Assertion function
/////////////////////////////////////////
//#define DEBUG
#ifndef DEBUG
#define NDEBUG
#endif // DEBUG
#ifdef DEBUG
#undef NDEBUG
#endif // DEBUG
#include "assert.h"
#define ASSERT(n) assert((n))
/////////////////////////////////////////

typedef unsigned long long U64;
typedef int16_t ContinuationTable[2][6][64][6][64];
typedef int16_t HistoryTable[2][64][64];
typedef int16_t CaptureHistoryTable[6][64][5];
typedef int CounterMoveTable[2][6][64];
#define ALIGN64 alignas(64)

#define NAME "GOOB"
#define AUTHOR "Gabriel Montes"
#define VER "1.8.9"

#define MATEIN5 "2kr4/p1p2pQ1/P1p2Np1/2P4p/7B/1P6/5PPP/R4K2 w - - 0 3"
#define QUEENG3 "5rk1/pp4pp/4p3/2R3Q1/3n4/2q4r/P1P2PPP/5RK1 b - - 0 1"
#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
#define TEST_SEARCH "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
#define KING_RACE "8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - - 0 1"
#define QUEENXF3 "4r1k1/p1pb1ppp/Qbp1r3/8/1P6/2Pq1B2/R2P1PPP/2B2RK1 b - - 0 1"
#define MVFLAGEP 0x40000
#define MVFLAGPS 0x80000
#define MVFLAGCA 0x1000000
#define MVFLAGCAP 0x7C000
#define MVFLAGPROM 0xF00000
#define NOMOVE 0
#define NULLMOVE 507904

enum{p_pawn,p_knight,p_bishop,p_rook,p_queen,p_king};
enum{PAWN=1,KNIGHT,BISHOP,ROOK,QUEEN,KING};
enum {  MAXDEPTH=128,
        MAXPOSMOVES=256,
        MAXGAMESMOVES=550,
        INFINITE=40000,
        ISMATE=INFINITE-MAXDEPTH,
        VALUE_NONE=INFINITE+1};
enum {pawnHashMB=16,evalHashMB=32,defaultElo=2700,defaultHash=64,maxHash=1024};
enum {OFFBOARD=100,BOARD_NUMS_SQ=64};
enum {OPENING,ENDING};
enum { EMPTY, wP, wN, wB, wR, wQ, wK, bP, bN, bB, bR, bQ, bK };
enum {FILE_A,FILE_B,FILE_C,FILE_D,FILE_E,FILE_F,FILE_G,FILE_H,FILE_NONE};
enum {RANK_1,RANK_2,RANK_3,RANK_4,RANK_5,RANK_6,RANK_7,RANK_8,RANK_NONE};
enum {WHITE, BLACK, BOTH};
enum {UCIMODE,XBOARDMODE,CONSOLEMODE};
enum {
    A1,B1,C1,D1,E1,F1,G1,H1,
    A2,B2,C2,D2,E2,F2,G2,H2,
    A3,B3,C3,D3,E3,F3,G3,H3,
    A4,B4,C4,D4,E4,F4,G4,H4,
    A5,B5,C5,D5,E5,F5,G5,H5,
    A6,B6,C6,D6,E6,F6,G6,H6,
    A7,B7,C7,D7,E7,F7,G7,H7,
    A8,B8,C8,D8,E8,F8,G8,H8,NO_SQ

};
enum {FALSE,TRUE};
enum {WKCA=1,WQCA=2,BKCA=4,BQCA=8};
enum {HFNONE,HFALPHA,HFBETA,HFEXACT};

//Eval Entry
typedef struct{
    U64 posKey;
    int EvalScore;

} EVAL_ENTRY;

//Eval Tables
typedef struct {
    EVAL_ENTRY *evalTable;
    int numEntries;
} EVAL_TABLE;

//Pawn Entry
typedef struct{
    U64 pawnPosKey;
    int whiteScore;
    int blackScore;
    //int pkEval;
    U64 passed[2];

} PAWNKING_ENTRY;

//Pawn Tables
typedef struct {
    PAWNKING_ENTRY *paTable;
    int numEntries;
} PAWNKING_TABLE;

//Move Entry
typedef struct {
    int move;
    int score;
} S_MOVE;

//PV ENtry
typedef struct{
    U64 posKey;
    int move;
    int score;
    int depth;
    int flags;
    int eval;
    int generation;
} S_PVENTRY;

//PV tables
typedef struct{
    S_PVENTRY *pTable;
    int numEntries;
    int generation;
} S_PVTABLE;

//Move list
typedef struct {
    int count;
    S_MOVE moves[MAXPOSMOVES];
}S_MOVELIST;

//Undoing Moves
typedef struct {
    int move;
    int castleRights;
    int enPas;
    int fiftyMove;
    U64 posKey;
} S_UNDO;

//Board structure
typedef struct {
    //important board things
    int pieces[BOARD_NUMS_SQ];
    U64 bitboards[13];
    U64 occupancy[3];
    int side;
    int enPas;
    int fiftyMove;
    int castleRights;
    U64 posKey;
    U64 pkHash;
    //U64 pawnPosKey;
    S_UNDO history[MAXGAMESMOVES];
    int ply;
    int hisPly;
    int chess960;
    EVAL_TABLE   eTable[1];
    PAWNKING_TABLE   pawnKingTable[1];
    int useFiftyMoveRule;

    //for evaluation
    int psqtmat;
    int contemptDrawPenalty;
    int contemptComplexity;
    int contempt;
    //int usePKNN;
    //int useEGNN;
    //int USE_NNUE;
    int kingSq[2];
    int pkSafety[2];
    U64 attackedByBishops[2];
    U64 attackedByKnights[2];
    U64 occupiedMinusBishops[2];
    U64 occupiedMinusRooks[2];
    U64 mobilityAreas[2];
    int attWeight[2];
    int attCnt[2];
    int kingAttacksCount[2];
    U64 passers[2];
    int pawnEval[2];
    //int pkEval;
    U64 attacks_array_minors[2];
    U64 attacks_array_rooks[2];
    U64 attacks_array_queens[2];
    U64 attacks_array_pawns[2];
    U64 rammedPawns[2];
    U64 attackedBy2[2];
    U64 attacked[2];
    U64 kingAreas[2];
    U64 pawnAttackedBy2[2];
    int gamePhase;

    //for search
    int seldepth;
    S_PVTABLE pvTable[1];
    int pvArray[MAXDEPTH];
    //int searchHistory[13][BOARD_NUMS_SQ];
    int searchKillers[2][MAXDEPTH];
    int eval_stack[MAXDEPTH];
    int moveStack[MAXDEPTH];
    int pieceStack[MAXDEPTH];
    ALIGN64 ContinuationTable continuation;
    ALIGN64 CaptureHistoryTable chist;
    ALIGN64 HistoryTable histtable;
    ALIGN64 CounterMoveTable cmtable;

} S_BOARD;

//Search Details
typedef struct {
    int starttime;
    int stoptime;
    int stopped;
    U64 nodes;
    int depth;
    int movestogo;
    U64 EloNodelimit;
    U64 nodeLimit;
    int mateLimit;
    int quit;

    //limits
    int timeSet;
    int depthSet;
    int nodeSet;
    int EloNodeSet;
    int UciInfinite;

    //options for uci
    int analyzeMode;
    //int useRazoring;
    int ponder;
    int bruteForceMode;
    int setOptionPonder;

    //int GAMEMODE;
    //int POST_THINKING;
} S_SEARCHINFO;

//Engine options
typedef struct{
    //int useBook;
    int analysisMode;
    int uciElo;
} S_OPTIONS;

/*MACROS */

#define INLINE static inline __attribute__((always_inline))
#define NOINLINE __attribute__((noinline))
#define MOVE(f,t,cap,prom,fl) ((f) | (t<<7) |(cap<<14) | (prom <<20) | (fl))
#define FROMSQ(m) ((m) & 0x7F)
#define TOSQ(m) (((m)>>7) & 0x7F)
#define CAPTURED(m) (((m)>>14) & 0xF)
#define PROMOTED(m) (((m)>>20) & 0xF)
#define LSBINDEX(x) __builtin_ctzll(x)
#define COUNTBIT(bitboard) __builtin_popcountll(bitboard)
#define FRtoSQ(f,r) (r*8+f)
#define GETBIT(bitboard,square) (bitboard & (1ULL << square))
#define POPBIT(bb,sq) (GETBIT(bb,sq) ? bb ^= (1ULL << sq):0)
#define SETBIT(bitboard,square) (bitboard |= (1ULL << square))
#define ISBQ(p) (pieceBishopQueen[(p)])
#define ISRQ(p) (pieceRookQueen[(p)])
#define ISKni(p) (pieceKnight[(p)])
#define MIRROR64(sq) (Mirror64[(sq)])
#define SQ64TO120(sq) (Squares64To120[(sq)])

/*GLOBALS*/
extern const int Squares64To120[64];
extern U64 king_attacks[BOARD_NUMS_SQ];
extern U64 knight_attacks[BOARD_NUMS_SQ];
extern U64 pawn_attacks[BOTH][BOARD_NUMS_SQ];
extern const int pieceKnight[13];
extern const int pieceKing[13];
extern const int pieceRookQueen[13];
extern const int pieceBishopQueen[13];
extern U64 pieceKeys[13][BOARD_NUMS_SQ];
extern U64 sideKey;
extern U64 castleKeys[16];
extern const char pieceChar[];
extern const char sideChar[];
extern const char fileChar[];
extern const char rankChar[];
extern const int pieceBig[13];
extern const int pieceMin[13];
extern const int pieceMaj[13];
extern const int pieceCol[13];
extern const int piecePawn[13];
extern const int pieceType[13];
extern const int filesBoard[BOARD_NUMS_SQ];
extern const int ranksBoard[BOARD_NUMS_SQ];
extern const int Mirror64[64];
extern S_OPTIONS EngineOptions[1];
//extern int mvvLvaScore[13][13];

/*FUNCTIONS */

//attacks.c
extern U64 pawnRightAttacks(U64 pawns, U64 targets, int colour);
extern U64 pawnLeftAttacks(U64 pawns, U64 targets, int colour);
extern U64 pawnAttackSpan(U64 pawns, U64 targets, int colour);
extern U64 pawnAttackDouble(U64 pawns, U64 targets, int colour);
extern U64 discoveredAttacks(S_BOARD *pos, int sq, int US);
extern U64 attackersToKingSq(const S_BOARD *pos,int side);
extern U64 pawnAttacks(int color,int sq);
extern U64 allAttackersToSquare(const S_BOARD *pos, U64 occupied, int sq);
extern U64 get_rook_attacks(int square,U64 occupancy);
extern U64 get_bishop_attacks(int square,U64 occupancy);
extern U64 get_queen_attacks(int square,U64 occupancy);
extern void InitAttacks();
extern int is_square_attacked_BB(const int square, const int side,const S_BOARD *pos);

//board.c
extern void resetContinuationTable(S_BOARD *pos);
extern void initStacks(S_BOARD *pos);
extern int getGamePhase(const S_BOARD *pos);
extern int checkBoard(const S_BOARD *pos);
extern void ResetBoard(S_BOARD *pos);
extern int ParseFEN(char *fen ,S_BOARD *pos);
extern void PrintBoard(const S_BOARD *pos);
extern void updateListMaterial(S_BOARD *pos);
extern void MirrorBoard(S_BOARD *pos);

//init.c
extern void AllInit();

//io.c
extern char * PrSq(const int sq);
extern void PrintMoveList(const S_MOVELIST *list,S_BOARD *pos);
extern char * PrMove(const int move);
extern void printBitBoard(U64 bitboard);
extern int ParseMove(char *ptrChar, S_BOARD *pos);
extern void printFen(const S_BOARD *pos,char *fen);

//makemove.c
extern int getCapturedPiece(int move);
extern int moveIsTactical(S_BOARD *pos,int move);
extern int MoveBestCaseValue(S_BOARD *pos);
extern int makeMove(S_BOARD *pos,int move);
extern void takeMove(S_BOARD *pos);
extern void takeNullMove(S_BOARD *pos);
extern void makeNullMove(S_BOARD *pos);
extern int moveEstimatedValue(S_BOARD *pos, int move);

//misc.c
extern int getTimeMs();
extern void ReadInput(S_SEARCHINFO *info);

//movegen.c
//extern void GenerateAllQuiets(const S_BOARD *pos,S_MOVELIST *list);
extern void GenerateAllMoves(const S_BOARD *pos,S_MOVELIST *list);
extern void GenerateAllNoisy(const S_BOARD *pos,S_MOVELIST *list);
extern int MoveExists(S_BOARD *pos,const int move);
//extern void InitMvvLva();

//perft.c
extern void PerftTest(int depth,S_BOARD *pos);
extern void BenchTest(int depth,S_BOARD *pos);
extern U64 countNps(U64 nodes, int time);

//polybook.c
extern void CleanPolyBook();
extern void InitPolyBook(char *namefile);
extern int getBookMove(S_BOARD *pos);

//pvtable.c
//extern void updateKillers(S_BOARD *pos,int move);
extern int hashfullTT(S_PVTABLE *table);
extern void updateAge(S_PVTABLE *table);
extern int valueFromTT(int value,int ply);
extern int valueToTT(int value,int ply);
extern void InitPvTable(S_PVTABLE *table,const int mb,int noisy);
extern int ProbePvTable(const S_BOARD *pos);
extern void StoreHashEntry(S_BOARD *pos,const int move, int score, const int flags, const int depth,const int eval);
extern void clearPvTable(S_PVTABLE *table);
extern int getPvLine(const int depth,S_BOARD *pos);
extern int ProbeHashEntry(S_BOARD *pos, int *move, int *score, int *ttDepth,int *ttBound,int *ttEval);

//validate.c
extern int moveValid(const int move);
extern void Evaltest(S_BOARD *pos);
extern int SqOnBoard(const int sq);
extern int SideValid(const int side);
extern int FileRankValid(const int fr);
extern int PieceValidEmpty(const int pce);
extern int PieceValid(const int pce);

#endif // DEFS_H
