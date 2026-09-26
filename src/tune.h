#ifndef TUNE_H
#define TUNE_H

#include "defs.h"
#include <stdio.h>

/* Every search constant lives in one runtime-tunable struct instead of being
 * a `static const int` in a header.  Two reasons:
 *
 *   1. tools/search_tuner.c has to be able to perturb a parameter, search a
 *      batch of positions, measure the loss and put the value back -- all at
 *      runtime, thousands of times, without recompiling.
 *   2. A header `static const` is copied into every translation unit that
 *      includes it, so there is no single definition to point a tuner at.
 *
 * The defaults in tuneTable[] are the values the engine shipped with, so a
 * freshly built binary plays exactly the same as before this file existed.
 * Sizing a parameter here is a *scale* choice, not a value choice: the
 * gradient of a fixed-node search is a step function, so `step` must be big
 * enough that a perturbation actually changes some node counts.
 *
 * All of these are consumed through `ST.` (e.g. ST.futilityMargin); the only
 * places that touch ST directly are tune.c, the `tune` UCI command in
 * uci.c, and the tuner itself.
 */

/* LateMovePruningCounts is indexed [improving][depth].  Slots 1..8 are
 * exposed to the tuner; lmpPruningDepth is clamped to TUNE_LMP_SLOTS-1 so the
 * index can never run off the end.  (An out-of-range count of 0 would make
 * `quietsSeen >= count` always true and prune every quiet move, so the clamp
 * is load bearing, not cosmetic.) */
#define TUNE_LMP_SLOTS 9

typedef struct {
    /* ---- SEE and qsearch ---- */
    int seePruningDepth;   /* max depth at which SEE may reject a move      */
    int seeQuietMargin;    /* quiet SEE threshold, scaled linearly by depth  */
    int seeNoisyMargin;    /* capture SEE threshold, scaled by depth^2      */
    int qseeMargin;        /* slack for the qsearch stand-pat                */
    int deltaMarginQ;      /* stand-pat is pointless below best-case + this  */

    /* ---- alpha / beta pruning ---- */
    int alphaPruningDepth; /* max depth for the "hopelessly lost" cutoff     */
    int alphaMargin;
    int betaPruningDepth;  /* max depth for the "hopelessly won" cutoff      */
    int betaMargin;
    int ttResearchMargin;  /* do not re-search a TT entry closer than this   */

    /* ---- probcut ---- */
    int probCutDepth;
    int probCutMargin;

    /* ---- history-based extensions ---- */
    int histexLimit;       /* counter/follow-up history needed to extend     */

    /* ---- futility pruning ---- */
    int futilityMargin;             /* per ply                            */
    int futilityMarginNoHistory;    /* extra slack when history is empty   */
    int futilityPruningDepth;
    int futilityHistLimit[2];       /* [improving]                        */

    /* ---- late move pruning ---- */
    int lmpPruningDepth;
    int lmpCounts[2][TUNE_LMP_SLOTS];  /* [improving][depth]              */

    /* ---- counter / follow-up move pruning ---- */
    int counterMovePruneDepth[2];
    int counterMoveHistLimit[2];
    int followUpPruneDepth[2];
    int followUpHistLimit[2];

    /* ---- razoring ---- */
    int razoringDepth;
    int razorMarginBase;
    int razorMarginCoeff;

    /* ---- internal iterative reduction ---- */
    int iirDepth;

    /* ---- late move reduction ---- */
    int defaultNullMoveDepth;
    int allNodeScale;      /* R += R*scale/(256*depth+base)                 */
    int allNodeBase;
    int hindsightMargin;   /* parent+child eval sum that undoes a reduction */

    /* ---- null move verification ---- */
    int nmpVerifyDepth;

    /* ---- singular extension ---- */
    int singularQuietLimit;
    int singularTacticalLimit;
    int doubleExtMargin;
    int ttMoveHistoryMax;   /* saturation of pos->shared->ttMoveHistory    */
    int ttMoveHistoryScale; /* divisor applied to it in the double-ext test*/

    /* ---- root / aspiration window ---- */
    int scoreWindow;
    int windowDepth;        /* first depth at which aspiration kicks in     */

    /* ---- LMR shape ----
     * initLMRTable computes `base + log(i)*log(j)/div`.  Kept as integers
     * scaled by 1000 so the whole table stays in integer land and the tuner
     * never has to reason about float steps.  Defaults: 750 -> 0.75,
     * 2250 -> 2.25, which reproduce the original table exactly. */
    int lmrBaseMilli;
    int lmrDivMilli;

    /* ---- Surprise-SRD (sibling-surprise / eval-expectation LMR) ---- */
    int evalDeficitMargin;
    int evalSurplusMargin;
    int evalMoveLimit;
} S_SearchTune;

/* The one live copy of the search parameters. */
extern S_SearchTune ST;

typedef struct {
    const char *name;  /* the identifier the parameter had before, or a
                        * Short_Name/Index form for array elements, so the
                        * tuner's output is greppable against the history   */
    int *slot;
    int  def;          /* shipping default -- the single source of truth    */
    int  lo, hi;       /* hard bounds; the tuner clamps to these           */
    int  step;         /* finite-difference / SPSA perturbation size       */
} S_TuneEntry;

extern const S_TuneEntry tuneTable[];
extern const int tuneNumEntries;

/* Set every parameter to its default.  Idempotent; called from AllInit(). */
void tuneSetDefaults(void);

/* Clamp every parameter into [lo,hi] and rebuild anything derived from them
 * (currently only the LMR table).  Call after any bulk parameter change. */
void tuneClampAndRebuild(void);

/* Index of `name` in tuneTable[], or -1. */
int  tuneFind(const char *name);

/* Set / get one parameter by name.  Both clamp to [lo,hi] and return 0 on
 * success, -1 if the name is unknown. */
int  tuneSetByName(const char *name, int value);
int  tuneGetByName(const char *name);

/* Print "name def lo hi step current" for every parameter. */
void tunePrintAll(FILE *out);

/* Fills LMRTable from ST.lmrBaseMilli / ST.lmrDivMilli. */
void initLMRTable(void);

#endif // TUNE_H
