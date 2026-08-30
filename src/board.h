#ifndef BOARD_H
#define BOARD_H


#include "defs.h"
#include "correction_types.h"

/* Must match the l1_size (per-perspective accumulator width) of the
   exported NNUE net. Update this to match whatever L1 size you train
   with — nnue_init() refuses to load a file whose l1_size disagrees
   (see nnue_loader.h). */
#define NNUE_ACC_SIZE 1024



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
    int32_t nnue_acc[2][NNUE_ACC_SIZE];
} S_SEARCH_THREAD;

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

//Board structure
typedef struct {
    //important board things
    int8_t pieces[BOARD_NUMS_SQ]; // pieces stored in 64 square board array (one cache line; values 0..12 fit)
    U64 bitboards[13]; // bitboards for the pieces including empty
    U64 occupancy[3]; // occupancy for white, black, both
    int side; //side to move
    int enPas; //where the enpas in 64 square
    int fiftyMove; //counter for fifty move
    int castleRights; //castling rights
    U64 posKey; //position key
    U64 pkHash; //pawn king key
    U64 npHash[2]; //non-pawn material keys, one per color (kings included)
    U64 minorHash; //minor piece key (all knights + bishops, both colors)
    S_SEARCH_THREAD *search;
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
    int pliesFromNull;


    S_SHARED_TABLES *shared;





} S_BOARD;

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

#endif