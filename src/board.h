#ifndef BOARD_H
#define BOARD_H


#include "defs.h"

/* Must match the l1_size (accumulator width) of the exported NNUE net.
   Our trained net uses 256; if you ever retrain with a different L1
   size this must be updated to match, or nnue_init() will refuse to
   load (see nnue_loader.h). */
#define NNUE_ACC_SIZE 512

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

    /* NNUE accumulator: fc1's output for the CURRENT position, kept in
       sync incrementally by ClearPiece/AddPiece/MovePiece (makemove.c)
       instead of being recomputed from scratch on every eval call.
       Quantized fixed-point (see nnue_loader.h: scale = qa_scale from
       the loaded weight file, typically 64). Only meaningful/maintained
       while nnue_loaded is true. */
    int32_t nnue_acc[NNUE_ACC_SIZE];
    
    /* Modern NNUE state - HalfKP architecture with dual accumulators */
    void *nnue_state;  /* Pointer to NNUE_State (defined in nnue.h) */

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