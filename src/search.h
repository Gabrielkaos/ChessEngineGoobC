#ifndef SEARCH_H
#define SEARCH_H
#include "defs.h"
#include "board.h"
//NOTE
/*
    Some variables shamelessly copied from Ethereal
*/
#include "tune.h"

//The tunable search constants (margins, depth limits, LMR/NMP formula terms)
//live in tune.h. The ones below are not tuned.
static const int CounterMovePruningDepth[] = { 3, 2 };
static const int FollowUpMovePruningDepth[] = { 3, 2 };
static const int LateMovePruningDepth = 8;   //last index of LateMovePruningCounts
static const int LateMovePruningCounts[2][9] = {
    {  0,  3,  4,  6, 10, 14, 19, 25, 31},
    {  0,  5,  7, 11, 17, 26, 36, 48, 63},
};
static const int UciCurrMoveTime = 2500;
static const int BoundReportTime = 2500;
static const int DepthOneGraceMs = 300;
static const int SingularQuietLimit = 6;
static const int SingularTacticalLimit = 3;

// Surprise-SRD: Dynamic LMR via Sibling History & Eval Expectation
#ifndef USE_SURPRISE_SRD
#define USE_SURPRISE_SRD 1
#endif

extern int SurpriseSRDEnabled;

//FUNCTIONS
extern void initLMRTable();
extern void SearchPosition(S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table);
extern int StaticExchangeEvaluation(S_BOARD *pos,int move,int threshold);
extern int SearchPositionThread(void *data);
extern void EnsureThreadPool(int numThreads);
extern void FreeThreadPool(void);
//totals over the threads of the current/last search
extern U64 NodesSearchedThreadPool(const S_SEARCHINFO *info);
extern U64 TbHitsThreadPool(const S_SEARCHINFO *info);
#endif // SEARCH_H