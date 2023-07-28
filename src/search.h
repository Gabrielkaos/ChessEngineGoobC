#ifndef SEARCH
#define SEARCH

//NOTE
/*
    Some variables shamelessly copied from Ethereal
*/

static const int SEEPieceValues[] = {
     0, 100, 450, 450, 675, 1300, 0, 100, 450, 450, 675, 1300, 0
};
static const int SEEPruningDepth = 9;
static const int SEEQuietMargin  = -64;
static const int SEENoisyMargin  = -19;
static const int DeltaMarginQ    = 150;
static const int QSSeeMargin     = 110;

static const int ScoreWindow = 50;

static const int probCutDepth = 5;
static const int probCutMargin = 85;

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

static const int SingularQuietLimit = 6;
static const int SingularTacticalLimit = 3;


//FUNCTIONS
extern void initLMRTable();
extern void SearchPosition(S_BOARD *pos,S_SEARCHINFO *info);
extern int StaticExchangeEvaluation(S_BOARD *pos,int move,int threshold);

#endif // SEARCH
