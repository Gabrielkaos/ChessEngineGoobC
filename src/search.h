#ifndef SEARCH_H
#define SEARCH_H
#include "defs.h"
#include "board.h"
#include "tune.h"
//NOTE
/*
    Some variables shamelessly copied from Ethereal
*/
/* SEE piece values. Not tuned: these are material values, not search
   parameters, and the NNUE already supplies the real ones. */
static const int SEEPieceValues[16] = {
     0, 100, 450, 450, 675, 1300, 0, 0,
     0, 100, 450, 450, 675, 1300, 0, 0
};

/* Every other search constant used to be a `static const int` in this header
   and is now a field of ST (see tune.h). They are read as ST.fooMargin etc.
   straight out of search.c. Defaults, bounds and tuning steps live in
   tune.c. */

// Surprise-SRD: Dynamic LMR via Sibling History & Eval Expectation
#ifndef USE_SURPRISE_SRD
#define USE_SURPRISE_SRD 1
#endif

extern int SurpriseSRDEnabled;

//FUNCTIONS
extern void SearchPosition(S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table);
extern int StaticExchangeEvaluation(S_BOARD *pos,int move,int threshold);
extern int SearchPositionThread(void *data);
extern void EnsureThreadPool(int numThreads);
extern void FreeThreadPool(void);

/* Single-threaded, silent, fixed-budget entry point used by the texel/SPT
   tuner (tools/search_tuner.c). It runs the real IterativeDeepening() on a
   stack-local worker -- no thread pool, no UCI output, no time management --
   so the numbers the tuner sees come from the same code the engine plays
   with. Returns the final root score, or VALUE_NONE if the position has no
   legal move. `nodeLimit` of 0 means "no node limit"; `maxDepth` of 0 means
   "no depth limit" (one of the two must be set). */
extern int SearchPositionFixed(S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table,
                               int maxDepth, U64 nodeLimit);

/* When set, IterativeDeepening() and AlphaBeta() print nothing. Constant for
   the whole tuning run (set once before any worker starts), so it is safe to
   read from several threads at once. */
extern int tuneQuiet;
#endif // SEARCH_H