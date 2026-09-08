#ifndef BOARD_H
#define BOARD_H


#include "defs.h"
#include "correction_types.h"
#include <string.h>

/* Must match the l1_size (per-perspective accumulator width) of the
   exported NNUE net. Update this to match whatever L1 size you train
   with — nnue_init() refuses to load a file whose l1_size disagrees
   (see nnue_loader.h). */
#define NNUE_ACC_SIZE 1024



typedef struct {
    int piece_remove[2];
    int from[2];
    int piece_add[2];
    int to[2];
    int remove_count;
    int add_count;
    int king_moved[COLOR_NB];
} DirtyPiece;

typedef struct {
    ALIGN64 int32_t accumulation[COLOR_NB][NNUE_ACC_SIZE];
    uint8_t computed[COLOR_NB];
} NNUE_Accumulator;

typedef struct {
    S_UNDO history[MAXGAMESMOVES]; //stores state of the board
    int eval_stack[MAXDEPTH];
    int reduction_stack[MAXDEPTH];
    int moveStack[MAXDEPTH];
    int pieceStack[MAXDEPTH];
    int pvArray[MAXDEPTH];
    int searchKillers[2][MAXDEPTH];
    LowPlyHistoryTable lowPlyHistory;
    int rootEffortMove[MAXPOSMOVES];
    U64 rootEffortNodes[MAXPOSMOVES];
    int rootEffortCount;
    int rootPvMove;
    int quietsTried[MAXDEPTH][MAXPOSMOVES];
    int capturesTried[MAXDEPTH][MAXPOSMOVES];
    S_MOVEPICKER movePickers[MAXDEPTH];
    S_MOVEPICKER singularMovePickers[MAXDEPTH];
    ALIGN64 NNUE_Accumulator nnue_accumulators[MAXDEPTH];
    DirtyPiece dirtyPieces[MAXDEPTH];
    SurpriseSRDStats srd_stats;
} S_SEARCH_THREAD;

static inline S_SEARCH_THREAD* alloc_search_thread(void) {
    size_t size = (sizeof(S_SEARCH_THREAD) + 63) & ~(size_t)63;
    S_SEARCH_THREAD *ptr = (S_SEARCH_THREAD*) aligned_alloc(64, size);
    if (ptr) memset(ptr, 0, sizeof(S_SEARCH_THREAD));
    return ptr;
}

//Board structure
typedef struct {
    ALIGN64 ContinuationTable    continuation;
    ALIGN64 CaptureHistoryTable  chist;
    ALIGN64 HistoryTable         histtable;
    ALIGN64 CounterMoveTable     cmtable;
    ALIGN64 PawnCorrectionTable  pawnCorrHist;
    ALIGN64 NonPawnCorrectionTable nonPawnCorrHist;
    ALIGN64 MinorCorrectionTable minorCorrHist;
    ALIGN64 PawnHistoryTable     pawnHist;
    ALIGN64 ContCorrectionTable  contCorrHist;
    int ttMoveHistory;
} S_SHARED_TABLES;

typedef struct StateInfo {
    U64 posKey;
    U64 pkHash;
    U64 npHash[COLOR_NB];
    U64 minorHash;
    int castleRights;
    int fiftyMove;
    int pliesFromNull;
    int enPas;
    int psqtmat;
    int repetition;
    U64 checkersBB;
    U64 blockersForKing[COLOR_NB];
    U64 pinners[COLOR_NB];
    int capturedPiece;
    DirtyPiece dirtyPiece;
    struct StateInfo *previous;
} StateInfo;

//Board structure
typedef struct {
    //important board things
    int8_t pieces[BOARD_NUMS_SQ]; // pieces stored in 64 square board array (values 0..15 fit)
    U64 byTypeBB[PIECE_TYPE_NB];  // bitboards by piece type: ALL_PIECES=0, PAWN=1..KING=6
    U64 byColorBB[COLOR_NB];      // occupancy for WHITE=0, BLACK=1
    int side; //side to move
    StateInfo *st;
    StateInfo stateTable[MAXGAMESMOVES];
    S_SEARCH_THREAD *search;
    int hisPly; //total number of moves played on the board
    int useFiftyMoveRule; //flag
    int contemptDrawPenalty; //penalty
    int contemptComplexity; //penalty
    int contempt; //stores the contempt score
    int gamePhase; //game phase
    int chess960; //flag

    //tables
    EVAL_TABLE   eTable[1]; //storing evaluation for positions
    PAWNKING_TABLE   pawnKingTable[1]; //stores scores and evaluation for pawn king

    //for search
    int ply; //search ply
    int seldepth;








    //per-thread low-ply history (cleared to 102 at the start of each search)



    int useNNUE;   // flag: use NNUE evaluation
    int usePKNet;

    int tbHit;
    int tbRootMoveCount;
    int tbRootMoves[MAXPOSMOVES];

    int excludedRootMoveCount;
    int excludedRootMoves[MAXPOSMOVES];

    int currentPvNum; 

    




    int nmpMinPly;   // null-move verification: ply threshold below which NMP is disabled


    S_SHARED_TABLES *shared;





} S_BOARD;

#define pieces_all(pos)          ((pos)->byTypeBB[ALL_PIECES])
#define pieces_color(pos, c)     ((pos)->byColorBB[(c)])
#define pieces_type(pos, pt)     ((pos)->byTypeBB[(pt)])
#define pieces_cp(pos, c, pt)    ((pos)->byColorBB[(c)] & (pos)->byTypeBB[(pt)])

#include "correction.h"

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
extern void update_slider_blockers(S_BOARD *pos, int c);
extern void set_check_info(S_BOARD *pos);

#endif