/* Tunable search parameters -- definitions, defaults and the name table.
 *
 * See tune.h for why these are runtime values rather than header constants.
 *
 * The `def` column below is the only place a default is written down; ST is
 * filled from it by tuneSetDefaults().  That matters because the tuner reads
 * `def` when it is asked to print the search space, and a default that had
 * drifted from the real value would silently make a "reset" a no-op.
 */
#include "tune.h"
#include "string.h"
#include "stdio.h"

S_SearchTune ST;

const S_TuneEntry tuneTable[] = {
    /* name                          slot                              def   lo    hi  step */
    { "SEEPruningDepth",             &ST.seePruningDepth,                 9,    0,   24,    1 },
    { "SEEQuietMargin",              &ST.seeQuietMargin,                -64, -400,    0,    4 },
    { "SEENoisyMargin",              &ST.seeNoisyMargin,                -19, -200,  100,    2 },
    { "QSSeeMargin",                 &ST.qseeMargin,                    110,    0, 2000,   10 },
    { "DeltaMarginQ",                &ST.deltaMarginQ,                  150,    0, 2000,   10 },

    { "AlphaPruningDepth",           &ST.alphaPruningDepth,               5,    0,   16,    1 },
    { "AlphaMargin",                 &ST.alphaMargin,                  3000,  500, 6000,  100 },
    { "BetaPruningDepth",            &ST.betaPruningDepth,                8,    1,   16,    1 },
    { "BetaMargin",                  &ST.betaMargin,                     85,    0,  600,    5 },
    { "TTResearchMargin",            &ST.ttResearchMargin,              128,    0, 1000,    8 },

    { "probCutDepth",                &ST.probCutDepth,                    5,    2,   16,    1 },
    { "probCutMargin",               &ST.probCutMargin,                  80,    0, 1000,    8 },

    { "HistexLimit",                 &ST.histexLimit,                 10000,    0,40000,  500 },

    { "FutilityMargin",              &ST.futilityMargin,                 65,    0,  400,    5 },
    { "FutilityMarginNoHistory",     &ST.futilityMarginNoHistory,       210,    0,  800,   10 },
    { "FutilityPruningDepth",        &ST.futilityPruningDepth,            8,    1,   16,    1 },
    { "FutilityPruningHistoryLimit[0]",  &ST.futilityHistLimit[0],    12000, -1000,40000, 500 },
    { "FutilityPruningHistoryLimit[1]",  &ST.futilityHistLimit[1],     6000, -1000,40000, 500 },

    /* hi is a *depth threshold*, not an index into lmpCounts, so it is not
       tied to TUNE_LMP_SLOTS; the MIN() at the use site keeps the column in
       range.  It must be > 8 or the parameter would sit pinned on its own
       upper bound at the default and the tuner could only ever lower it. */
    { "LateMovePruningDepth",        &ST.lmpPruningDepth,                 8,    1,   16,    1 },
    { "LateMovePruningCounts[0][1]", &ST.lmpCounts[0][1],                 3,    0,  200,    1 },
    { "LateMovePruningCounts[0][2]", &ST.lmpCounts[0][2],                 4,    0,  200,    1 },
    { "LateMovePruningCounts[0][3]", &ST.lmpCounts[0][3],                 6,    0,  200,    1 },
    { "LateMovePruningCounts[0][4]", &ST.lmpCounts[0][4],                10,    0,  200,    1 },
    { "LateMovePruningCounts[0][5]", &ST.lmpCounts[0][5],                14,    0,  200,    1 },
    { "LateMovePruningCounts[0][6]", &ST.lmpCounts[0][6],                19,    0,  200,    1 },
    { "LateMovePruningCounts[0][7]", &ST.lmpCounts[0][7],                25,    0,  200,    1 },
    { "LateMovePruningCounts[0][8]", &ST.lmpCounts[0][8],                31,    0,  200,    1 },
    { "LateMovePruningCounts[1][1]", &ST.lmpCounts[1][1],                 5,    0,  200,    1 },
    { "LateMovePruningCounts[1][2]", &ST.lmpCounts[1][2],                 7,    0,  200,    1 },
    { "LateMovePruningCounts[1][3]", &ST.lmpCounts[1][3],                11,    0,  200,    1 },
    { "LateMovePruningCounts[1][4]", &ST.lmpCounts[1][4],                17,    0,  200,    1 },
    { "LateMovePruningCounts[1][5]", &ST.lmpCounts[1][5],                26,    0,  200,    1 },
    { "LateMovePruningCounts[1][6]", &ST.lmpCounts[1][6],                36,    0,  200,    1 },
    { "LateMovePruningCounts[1][7]", &ST.lmpCounts[1][7],                48,    0,  200,    1 },
    { "LateMovePruningCounts[1][8]", &ST.lmpCounts[1][8],                63,    0,  200,    1 },

    { "CounterMovePruningDepth[0]",  &ST.counterMovePruneDepth[0],        3,    0,   16,    1 },
    { "CounterMovePruningDepth[1]",  &ST.counterMovePruneDepth[1],        2,    0,   16,    1 },
    { "CounterMoveHistoryLimit[0]",  &ST.counterMoveHistLimit[0],         0, -8000, 4000,  100 },
    { "CounterMoveHistoryLimit[1]",  &ST.counterMoveHistLimit[1],     -1000, -8000, 4000,  100 },
    { "FollowUpMovePruningDepth[0]", &ST.followUpPruneDepth[0],           3,    0,   16,    1 },
    { "FollowUpMovePruningDepth[1]", &ST.followUpPruneDepth[1],           2,    0,   16,    1 },
    { "FollowUpMoveHistoryLimit[0]", &ST.followUpHistLimit[0],        -2000, -8000, 4000,  100 },
    { "FollowUpMoveHistoryLimit[1]", &ST.followUpHistLimit[1],        -4000, -8000, 4000,  100 },

    { "RazoringDepth",               &ST.razoringDepth,                   2,    0,    6,    1 },
    { "RazorMarginBase",             &ST.razorMarginBase,               316,    0, 1200,   16 },
    { "RazorMarginCoeff",            &ST.razorMarginCoeff,              259,    0, 1200,    8 },

    { "IIRDepth",                    &ST.iirDepth,                        6,    2,   24,    1 },

    { "defaultNullMoveDepth",        &ST.defaultNullMoveDepth,            2,    1,    6,    1 },
    { "AllNodeScale",                &ST.allNodeScale,                  276,    0, 1024,    8 },
    { "AllNodeBase",                 &ST.allNodeBase,                   268,    0, 2048,    8 },
    { "HindsightMargin",             &ST.hindsightMargin,               166,    0, 1200,    8 },

    { "NMPVerifyDepth",              &ST.nmpVerifyDepth,                 16,    2,   40,    1 },

    { "SingularQuietLimit",          &ST.singularQuietLimit,              6,    0,   32,    1 },
    { "SingularTacticalLimit",       &ST.singularTacticalLimit,           3,    0,   32,    1 },
    { "DoubleExtMargin",             &ST.doubleExtMargin,               120,    0, 1200,    8 },
    { "TTMoveHistoryMax",            &ST.ttMoveHistoryMax,             8192, 1024,32768,  256 },
    { "TTMoveHistoryScale",          &ST.ttMoveHistoryScale,              40,    1,  400,    2 },

    { "ScoreWindow",                 &ST.scoreWindow,                    10,    2,   60,    2 },
    { "WindowDepth",                 &ST.windowDepth,                     5,    1,   20,    1 },

    { "LMRBaseMilli",                &ST.lmrBaseMilli,                  750,    0, 2000,   25 },
    { "LMRDivMilli",                 &ST.lmrDivMilli,                  2250,  100, 8000,   50 },

    { "EVAL_DEFICIT_MARGIN",         &ST.evalDeficitMargin,             120,    0, 1000,   10 },
    { "EVAL_SURPLUS_MARGIN",         &ST.evalSurplusMargin,             100,    0, 1000,   10 },
    { "EVAL_MOVE_LIMIT",             &ST.evalMoveLimit,                   4,    0,   32,    1 },
};

const int tuneNumEntries = (int)(sizeof(tuneTable) / sizeof(tuneTable[0]));

int tuneFind(const char *name) {
    for (int i = 0; i < tuneNumEntries; i++)
        if (!strcmp(tuneTable[i].name, name)) return i;
    return -1;
}

void tuneSetDefaults(void) {
    for (int i = 0; i < tuneNumEntries; i++)
        *tuneTable[i].slot = tuneTable[i].def;
}

void tuneClampAndRebuild(void) {
    for (int i = 0; i < tuneNumEntries; i++) {
        int *slot = tuneTable[i].slot;
        if (*slot < tuneTable[i].lo) *slot = tuneTable[i].lo;
        if (*slot > tuneTable[i].hi) *slot = tuneTable[i].hi;
    }
    // The LMR table is the only thing derived from ST rather than read from
    // it, so it has to be rebuilt whenever a parameter may have moved.
    // lmpCounts[] is indexed by depth with TUNE_LMP_SLOTS columns; the clamp
    // on lmpPruningDepth caps the index, and search.c clamps again at the use
    // site, so no out-of-range column is ever read.
    initLMRTable();
}

int tuneSetByName(const char *name, int value) {
    int i = tuneFind(name);
    if (i < 0) return -1;
    if (value < tuneTable[i].lo) value = tuneTable[i].lo;
    if (value > tuneTable[i].hi) value = tuneTable[i].hi;
    *tuneTable[i].slot = value;
    return 0;
}

int tuneGetByName(const char *name) {
    int i = tuneFind(name);
    return i < 0 ? 0 : *tuneTable[i].slot;
}

void tunePrintAll(FILE *out) {
    fprintf(out, "%-30s %8s %8s %8s %6s %8s\n",
            "name", "default", "lo", "hi", "step", "current");
    for (int i = 0; i < tuneNumEntries; i++)
        fprintf(out, "%-30s %8d %8d %8d %6d %8d\n",
                tuneTable[i].name, tuneTable[i].def, tuneTable[i].lo,
                tuneTable[i].hi, tuneTable[i].step, *tuneTable[i].slot);
}
