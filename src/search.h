#ifndef SEARCH_H
#define SEARCH_H
#include "defs.h"
#include "board.h"
//NOTE
/*
    Some variables shamelessly copied from Ethereal
*/
static const int SEEPieceValues[16] = {
     0, 100, 450, 450, 675, 1300, 0, 0,
     0, 100, 450, 450, 675, 1300, 0, 0
};
static const int SEEPruningDepth = 9;
static const int SEEQuietMargin  = -64;
static const int SEENoisyMargin  = -19;


static const int DeltaMarginQ    = 150;

static const int QSSeeMargin     = 110;


static const int ScoreWindow = 10;

static const int probCutDepth = 5;

static const int probCutMargin = 80;

static const int HistexLimit = 10000;
static const int FutilityMargin = 65;
static const int FutilityMarginNoHistory = 210;
static const int FutilityPruningDepth = 8;
static const int FutilityPruningHistoryLimit[] = { 12000, 6000 };
static const int CounterMovePruningDepth[] = { 3, 2 };
static const int CounterMoveHistoryLimit[] = { 0, -1000 };
static const int FollowUpMovePruningDepth[] = { 3, 2 };
static const int FollowUpMoveHistoryLimit[] = { -2000, -4000 };
static const int defaultNullMoveDepth = 2;
static const int LateMovePruningDepth = 8;
static const int LateMovePruningCounts[2][9] = {
    {  0,  3,  4,  6, 10, 14, 19, 25, 31},
    {  0,  5,  7, 11, 17, 26, 36, 48, 63},
};
static const int UciCurrMoveTime = 2500;
static const int BoundReportTime = 2500;
static const int DepthOneGraceMs = 300;
static const int SingularQuietLimit = 6;
static const int SingularTacticalLimit = 3;
static const int BetaPruningDepth = 8;
static const int BetaMargin = 85;
static const int WindowDepth = 5;

#define RazoringDepth      2      
#define RazorMarginBase    316    
#define RazorMarginCoeff   259


// #define SmallProbCutMargin 400

#define IIRDepth 6

#define AllNodeScale 276
#define AllNodeBase  268

#define HindsightMargin 166   // Stockfish's value

#define NMPVerifyDepth 16   // Stockfish's threshold

#define DoubleExtMargin 15 * 8
#define TripleExtMargin 90 //unused for now


#define TTMoveHistoryMax 8192   // tunable
#define TTMoveHistoryScale 40   // tunable

// Surprise-SRD (Sibling Refutation Density with Search Surprise)
#ifndef USE_SURPRISE_SRD
#define USE_SURPRISE_SRD 1
#endif

#ifndef USE_SURPRISE_SRD_LEVEL2
#define USE_SURPRISE_SRD_LEVEL2 1
#endif

#define SURPRISE_MARGIN        50
#define EARLY_MOVE_LIMIT       2
#define SURPRISE_THRESHOLD_1   200
#define SURPRISE_THRESHOLD_2   500
#define SURPRISE_MAX           1000

extern int SurpriseSRDEnabled;
extern int SurpriseSRDLevel2Enabled;
extern void printSurpriseSRDStats(const SurpriseSRDStats *stats);

//FUNCTIONS
extern void initLMRTable();
extern void SearchPosition(S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table);
extern int StaticExchangeEvaluation(S_BOARD *pos,int move,int threshold);
extern int SearchPositionThread(void *data);
extern void EnsureThreadPool(int numThreads);
extern void FreeThreadPool(void);
#endif // SEARCH_H