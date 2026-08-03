#ifndef BOARD_H
#define BOARD_H


#include "defs.h"

/* Must match the l1_size (per-perspective accumulator width) of the
   exported NNUE net. Update this to match whatever L1 size you train
   with — nnue_init() refuses to load a file whose l1_size disagrees
   (see nnue_loader.h). */
#define NNUE_ACC_SIZE 1024

//Board structure
typedef struct {
    //important board things
    int pieces[BOARD_NUMS_SQ]; // pieces stored in 64 square board array
    U64 bitboards[13]; // bitboards for the pieces including empty
    U64 occupancy[3]; // occupancy for white, black, both
    int side; //side to move
    int enPas; //where the enpas in 64 square
    int fiftyMove; //counter for fifty move
    int castleRights; //castling rights
    U64 posKey; //position key
    U64 pkHash; //pawn king key
    S_UNDO history[MAXGAMESMOVES]; //stores state of the board
    int hisPly; //total number of moves played on the board
    int psqtmat; //stores the score for the piece square table (updated while making move)
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
    int pvArray[MAXDEPTH];
    int searchKillers[2][MAXDEPTH];
    int eval_stack[MAXDEPTH];
    int moveStack[MAXDEPTH];
    int pieceStack[MAXDEPTH];
    ALIGN64 ContinuationTable continuation;
    ALIGN64 CaptureHistoryTable chist;
    ALIGN64 HistoryTable histtable;
    ALIGN64 CounterMoveTable cmtable;

    int useNNUE;   // flag: use NNUE evaluation
    int usePKNet;

    /* Syzygy tablebase root-move filtering (see syzygy.c/TBProbeRoot).
       Populated once per "go" command, before the depth loop starts, and
       left untouched for the remainder of that search. tbHit==1 means
       AlphaBeta's root move loop should skip any move not listed in
       tbRootMoves[0..tbRootMoveCount-1] -- every listed move preserves
       the position's tablebase-proven game-theoretic result, so the
       ordinary search is free to pick whichever of them it likes best. */
    int tbHit;
    int tbRootMoveCount;
    int tbRootMoves[MAXPOSMOVES];

    /* NNUE accumulators: the feature-transformer output for the CURRENT
       position, ONE PER PERSPECTIVE (index by WHITE/BLACK, see defs.h),
       kept in sync incrementally by ClearPiece/AddPiece/MovePiece
       (makemove.c) instead of being recomputed from scratch on every
       eval call. Quantized fixed-point (see nnue_loader.h: scale =
       qa_scale from the loaded weight file). Only meaningful/maintained
       while nnue_loaded is true. See nnue_loader.h for why two
       accumulators are needed (king-relative HalfKA-style features). */
    int32_t nnue_acc[2][NNUE_ACC_SIZE];

} S_BOARD;

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

#endif