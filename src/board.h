#ifndef BOARD_H
#define BOARD_H


#include "defs.h"
#include "trace.h"
#include "correction_types.h"
#include <stddef.h>
#include <string.h>

/* Must match the l1_size (per-perspective accumulator width) of the
   exported NNUE net. Update this to match whatever L1 size you train
   with — nnue_init() refuses to load a file whose l1_size disagrees
   (see nnue_loader.h). */
#define NNUE_ACC_SIZE 1024

#ifndef NNUE_MIRROR
#define NNUE_MIRROR 1
#endif
#ifndef NNUE_KING_BUCKETS
#define NNUE_KING_BUCKETS 4
#endif

typedef struct {
    int piece_remove[2];
    int from[2];
    int piece_add[2];
    int to[2];
    int remove_count;
    int add_count;
    int king_moved[COLOR_NB];
    uint8_t refresh[COLOR_NB];  /* 1 when side's king crossed a mirror or king-bucket boundary */
} DirtyPiece;

typedef struct {
    ALIGN64 int16_t accumulation[COLOR_NB][NNUE_ACC_SIZE];
    uint8_t computed[COLOR_NB];
    uint8_t flip[COLOR_NB];     /* 0 or 7: horizontal square XOR mask for perspective */
    uint8_t bucket[COLOR_NB];   /* 0..NNUE_KING_BUCKETS-1: own-king bucket for perspective */
} NNUE_Accumulator;

/* Finny table entry: caches accumulator and piece bitboards per (perspective, flip, bucket) */
typedef struct {
    ALIGN64 int16_t acc[NNUE_ACC_SIZE];
    U64 pieceBB[COLOR_NB][PIECE_TYPE_NB];
    uint32_t gen;
} NNUE_CacheEntry;

typedef struct {
    //per-thread counters, written only by the owning thread and summed by
    //NodesSearchedThreadPool()/TbHitsThreadPool() for reports and limits
    U64 nodes;
    U64 tbhits;
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
    int quietsTried[MAXDEPTH][MAXPOSMOVES];
    int capturesTried[MAXDEPTH][MAXPOSMOVES];
    S_MOVEPICKER movePickers[MAXDEPTH];
    S_MOVEPICKER singularMovePickers[MAXDEPTH];
    ALIGN64 NNUE_Accumulator nnue_accumulators[MAXDEPTH];
    DirtyPiece dirtyPieces[MAXDEPTH];
    int ttMoveHistory;
#ifdef TRACE
    SearchTrace trace;
#endif
    ALIGN64 NNUE_CacheEntry nnue_cache[COLOR_NB][2][NNUE_KING_BUCKETS];
} S_SEARCH_THREAD;

static inline S_SEARCH_THREAD* alloc_search_thread(void) {
    size_t size = (sizeof(S_SEARCH_THREAD) + 63) & ~(size_t)63;
    S_SEARCH_THREAD *ptr = (S_SEARCH_THREAD*) goob_aligned_alloc(64, size);
    if (ptr) memset(ptr, 0, sizeof(S_SEARCH_THREAD));
    return ptr;
}

static inline void free_search_thread(S_SEARCH_THREAD *ptr) {
    if (ptr) goob_aligned_free(ptr);
}


//Board structure
typedef struct {
    ALIGN64 ContinuationTable    continuation;
    ALIGN64 CaptureHistoryTable  chist;
    ALIGN64 HistoryTable         histtable;
    ALIGN64 CounterMoveTable     cmtable;
    ALIGN64 FollowupMoveTable    followupTable;
    ALIGN64 PawnCorrectionTable  pawnCorrHist;
    ALIGN64 NonPawnCorrectionTable nonPawnCorrHist;
    ALIGN64 MinorCorrectionTable minorCorrHist;
    ALIGN64 PawnHistoryTable     pawnHist;
    ALIGN64 ContCorrectionTable  contCorrHist;
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
    int repetition;
    U64 checkersBB;
    U64 blockersForKing[COLOR_NB];
    U64 pinners[COLOR_NB];
    int capturedPiece;
    DirtyPiece dirtyPiece;
    struct StateInfo *previous;
} StateInfo;

//Board structure
typedef struct S_BOARD {
    //important board things
    int8_t pieces[BOARD_NUMS_SQ]; // pieces stored in 64 square board array (values 0..15 fit)
    U64 byTypeBB[PIECE_TYPE_NB];  // bitboards by piece type: ALL_PIECES=0, PAWN=1..KING=6
    U64 byColorBB[COLOR_NB];      // occupancy for WHITE=0, BLACK=1
    int side; //side to move
    StateInfo *st;
    StateInfo stateTable[MAXGAMESMOVES];
    S_SEARCH_THREAD *search;
    int hisPly; //total number of moves played on the board
    int gamePlyOffset; //plies dropped from stateTable by compactStateHistory(); game ply = hisPly + gamePlyOffset
    int useFiftyMoveRule; //flag


    //for search
    int ply; //search ply
    int seldepth;

    int tbHit;
    int tbRootMoveCount;
    int tbRootMoves[MAXPOSMOVES];

    int excludedRootMoveCount;
    int excludedRootMoves[MAXPOSMOVES];

    int currentPvNum; 

    




    int nmpMinPly;   // null-move verification: ply threshold below which NMP is disabled


    S_SHARED_TABLES *shared;





} S_BOARD;

#define pieces_cp(pos, c, pt)    ((pos)->byColorBB[(c)] & (pos)->byTypeBB[(pt)])

#include "correction.h"

//board.c
extern void resetContinuationTable(S_BOARD *pos);
extern void initStacks(S_BOARD *pos);
extern int checkBoard(const S_BOARD *pos);
extern void ResetBoard(S_BOARD *pos);
extern int ParseFEN(char *fen ,S_BOARD *pos);
extern void PrintBoard(const S_BOARD *pos);
extern void updateListMaterial(S_BOARD *pos);
extern void MirrorBoard(S_BOARD *pos);
extern void HMirrorBoard(S_BOARD *pos);
extern void update_slider_blockers(S_BOARD *pos, int c);
extern void set_check_info(S_BOARD *pos);
extern void compactStateHistory(S_BOARD *pos);

#endif