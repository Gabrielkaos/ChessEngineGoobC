#ifndef DEFS_H
#define DEFS_H

#include "stdlib.h"

//#define DEBUG

#ifndef DEBUG
#define ASSERT(n)
#else
#define ASSERT(n)\
if(!(n)){\
printf("%s- FAILED",#n);\
printf("On %s ",__DATE__);\
printf("At %s ",__TIME__);\
printf("In File %s ",__FILE__);\
printf("In Line %d\n",__LINE__);\
exit(1);}
#endif // DEBUG

typedef unsigned long long U64;

#define NAME "GOOB"
#define BOARD_NUMS_SQ 64

#define MAXGAMESMOVES 2048
#define MAXPOSMOVES 256
#define MAXDEPTH 64

#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
#define TEST_SEARCH "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
#define KING_RACE "8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - - 0 1"

#define INFINITE 30000
//#define MATE 29000
#define ISMATE (INFINITE - MAXDEPTH)

enum { EMPTY, wP, wN, wB, wR, wQ, wK, bP, bN, bB, bR, bQ, bK };
enum {FILE_A,FILE_B,FILE_C,FILE_D,FILE_E,FILE_F,FILE_G,FILE_H};
enum {RANK_1,RANK_2,RANK_3,RANK_4,RANK_5,RANK_6,RANK_7,RANK_8};
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

//Move Format
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

} S_PVENTRY;

//PV tables
typedef struct{

    S_PVENTRY *pTable;
    int numEntries;
    //int newwrite;
    //int cut;
    //int hit;
    //int overwrite;

} S_PVTABLE;

//Move list
typedef struct {
    int count;
    S_MOVE moves[MAXPOSMOVES];

}S_MOVELIST;

//Undoing Moves
typedef struct {
    //things needed for the game state

    int move;
    int castleRights;
    int enPas;
    int fiftyMove;
    U64 posKey;

} S_UNDO;


//Board structure
typedef struct {

    int pieces[BOARD_NUMS_SQ];

    U64 bitboards[13];
    U64 occupancy[3];

    int kingSq[2];

    int side;
    int enPas;
    int fiftyMove;
    int castleRights;
    U64 posKey;

    int numPieces[13];
    int bigPiece[2];
    int majPiece[2];
    int minPiece[2];
    int material[2];

    S_UNDO history[MAXGAMESMOVES];

    //int pieceList[13][10];
    int ply;
    int hisPly;

    S_PVTABLE pvTable[1];
    int pvArray[MAXDEPTH];

    int searchHistory[13][BOARD_NUMS_SQ];
    int searchKillers[2][MAXDEPTH];

} S_BOARD;

//Search Details
typedef struct {

    int starttime;
    int stoptime;
    int depth;
    int movestogo;
    int timeset;

    U64 nodes;

    int quit;
    int stopped;

    int GAMEMODE;
    int POST_THINKING;


} S_SEARCHINFO;

//Engine options
typedef struct{
    int useBook;
} S_OPTIONS;

/*MACROS */

//for moves
#define FROMSQ(m) ((m) & 0x7F)
#define TOSQ(m) (((m)>>7) & 0x7F)
#define CAPTURED(m) (((m)>>14) & 0xF)
#define PROMOTED(m) (((m)>>20) & 0xF)

//en passant move wether
#define MVFLAGEP 0x40000
//pawn start
#define MVFLAGPS 0x80000
//wether castle move
#define MVFLAGCA 0x1000000

//is it a capturing move
#define MVFLAGCAP 0x7C000

//is the move a promotion
#define MVFLAGPROM 0xF00000

#define NOMOVE 0


#define LSBINDEX(x) __builtin_ctzll(x)
#define COUNTBIT(bitboard) __builtin_popcountll(bitboard)
#define FRtoSQ(f,r) (r*8+f)
#define CLRBIT(bb,sq) (bb &= ClearMask[(sq)])
#define GETBIT(bitboard,square) (bitboard & (1ULL << square))
#define POPBIT(bb,sq) (GETBIT(bb,sq) ? bb ^= (1ULL << sq):0)
#define SETBIT(bitboard,square) (bitboard |= (1ULL << square))
#define ISBQ(p) (pieceBishopQueen[(p)])
#define ISRQ(p) (pieceRookQueen[(p)])
#define ISKni(p) (pieceKnight[(p)])
#define MIRROR64(sq) (Mirror64[(sq)])

/*GLOBALS */
extern U64 king_attacks[64];
extern U64 knight_attacks[64];
extern U64 pawn_attacks[2][64];
extern const int pieceKnight[13];
extern const int pieceKing[13];
extern const int pieceRookQueen[13];
extern const int pieceBishopQueen[13];
extern U64 SetMask[64];
extern U64 ClearMask[64];
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
extern const int pieceVal[13];
extern const int pieceCol[13];
extern const int piecePawn[13];
extern int filesBoard[BOARD_NUMS_SQ];
extern int ranksBoard[BOARD_NUMS_SQ];
extern U64 FileBBMask[8];
extern U64 RankBBMask[8];
extern U64 BlackPassedMask[64];
extern U64 WhitePassedMark[64];
extern U64 IsolatedMask[64];
extern const int Mirror64[64];
extern S_OPTIONS EngineOptions[1];

/*FUNCTIONS */

//attacks.c
extern U64 get_rook_attacks(int square,U64 occupancy);
extern U64 get_bishop_attacks(int square,U64 occupancy);
extern U64 get_queen_attacks(int square,U64 occupancy);
extern void InitAttacks();
extern int is_square_attacked_BB(const int square, const int side,const S_BOARD *pos);

//bitboards.c
extern int LSBINDEX(U64 bitboard);

//board.c
extern void ResetBoard(S_BOARD *pos);
extern int ParseFEN(char *fen ,S_BOARD *pos);
extern void PrintBoard(const S_BOARD *pos);
extern void updateListMaterial(S_BOARD *pos);
extern void MirrorBoard(S_BOARD *pos);

//consolemode.c
extern void Console_Loop(S_BOARD *pos, S_SEARCHINFO *info);

//evaluate.c
extern int EvalPosition(const S_BOARD *pos);

//hashkeys.c
extern U64 GeneratePosKey(const S_BOARD *pos);

//init.c
extern void AllInit();

//io.c
extern void PrintMoveList(const S_MOVELIST *list);
extern char * PrMove(const int move);
extern void printBitBoard(U64 bitboard);
extern int ParseMove(char *ptrChar, S_BOARD *pos);
extern void printFen(const S_BOARD *pos,char *fen);

//makemove.c
extern int makeMove(S_BOARD *pos,int move);
extern void takeMove(S_BOARD *pos);
extern void takeNullMove(S_BOARD *pos);
extern void makeNullMove(S_BOARD *pos);

//misc.c
extern int getTimeMs();
extern void ReadInput(S_SEARCHINFO *info);

//movegen.c
extern void GenerateAllMoves(const S_BOARD *pos,S_MOVELIST *list);
extern void GenerateAllCaptures(const S_BOARD *pos,S_MOVELIST *list);
extern int MoveExists(S_BOARD *pos,const int move);
extern void InitMvvLva();

//perft.c
extern void PerftTest(int depth,S_BOARD *pos);
extern void BenchTest(int depth,S_BOARD *pos);

//polybook.c
extern void CleanPolyBook();
extern void InitPolyBook();
extern int getBookMove(S_BOARD *pos);

//pvtable.c
extern void InitPvTable(S_PVTABLE *table,const int mb);
extern int ProbePvTable(const S_BOARD *pos);
extern void StorePvTable(S_BOARD *pos,const int move, int score, const int flags, const int depth);
extern void clearPvTable(S_PVTABLE *table);
extern int getPvLine(const int depth,S_BOARD *pos);
extern int ProbeHashEntry(S_BOARD *pos, int *move, int *score, int alpha, int beta, int depth);

//search.c
extern void SearchPosition(S_BOARD *pos,S_SEARCHINFO *info);

//uci.c
extern void UCI_loop(S_BOARD *pos,S_SEARCHINFO *info);

//validate.c
extern void test(S_BOARD *pos);
extern int SqOnBoard(const int sq);
extern int SideValid(const int side);
extern int FileRankValid(const int fr);
extern int PieceValidEmpty(const int pce);
extern int PieceValid(const int pce);

#endif // DEFS_H
