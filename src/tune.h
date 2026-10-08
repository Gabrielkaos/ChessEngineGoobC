#ifndef TUNE_H
#define TUNE_H

//Tunable search parameters.
//
//Every entry is  TP(name, default, min, max, c_end)  where c_end is the final
//SPSA perturbation size (OpenBench convention, roughly (max-min)/20).
//
//Normal builds turn each entry into a compile-time constant (an enum), so the
//generated code is the same as with the old `static const int`s.
//Building with -DTUNE (`make tune`) turns them into globals that are exposed
//as UCI spin options, and adds the `spsa` command that prints the table in
//OpenBench SPSA format for tools/spsa/spsa.py.
//
//To change a default, edit it here (tools/spsa/spsa.py apply does that for a
//finished tuning run).

#define SEARCH_PARAMS(TP) \
    /* qsearch */ \
    TP(DeltaMarginQ,               103,    40,   250,   10) \
    TP(QSSeeMargin,                110,    40,   250,   10) \
    /* transposition table */ \
    TP(TTResearchMargin,           126,    50,   300,   12) \
    /* aspiration windows */ \
    TP(ScoreWindow,                  9,     4,    30,    2) \
    TP(WindowDepth,                  6,     2,     9,    1) \
    /* hindsight depth adjustment */ \
    TP(HindsightMargin,            164,    60,   320,   12) \
    /* razoring */ \
    TP(RazoringDepth,                2,     1,     5,    1) \
    TP(RazorMarginBase,            236,   100,   450,   18) \
    TP(RazorMarginCoeff,           158,    60,   320,   12) \
    /* reverse futility (beta) pruning */ \
    TP(BetaPruningDepth,             8,     4,    12,    1) \
    TP(BetaMargin,                  72,    35,   150,    6) \
    /* null move pruning, R = NMPBase + depth/NMPDepthDiv + min(3,(eval-beta)/NMPEvalDiv) */ \
    TP(defaultNullMoveDepth,         2,     1,     5,    1) \
    TP(NMPBase,                      5,     2,     6,    1) \
    TP(NMPDepthDiv,                  6,     3,    10,    1) \
    TP(NMPEvalDiv,                 197,   100,   400,   15) \
    TP(NMPVerifyDepth,              16,     8,    24,    1) \
    /* internal iterative reduction */ \
    TP(IIRDepth,                     6,     3,    10,    1) \
    /* probcut */ \
    TP(probCutDepth,                 6,     3,     8,    1) \
    TP(probCutMargin,               79,    30,   200,    8) \
    /* quiet futility pruning (history limits: NI = not improving, I = improving) */ \
    TP(FutilityPruningDepth,         8,     4,    12,    1) \
    TP(FutilityMargin,              67,    30,   130,    5) \
    TP(FutilityMarginNoHistory,    110,    40,   220,    9) \
    TP(FutilityHistLimitNI,      12045,  4000, 20000,  800) \
    TP(FutilityHistLimitI,        5962,     0, 14000,  700) \
    /* counter / follow-up move history pruning */ \
    TP(CounterMoveHistLimitNI,       -205, -3000,  2000,  250) \
    TP(CounterMoveHistLimitI,    -1096, -4000,  1000,  250) \
    TP(FollowUpHistLimitNI,       -484, -3500,  1500,  250) \
    TP(FollowUpHistLimitI,       -1584, -4500,   500,  250) \
    /* capture futility pruning */ \
    TP(CaptureFutilityDepth,         6,     3,    10,    1) \
    TP(CaptureFutilityBase,        105,    30,   250,   10) \
    TP(CaptureFutilityPerDepth,    116,    50,   250,   10) \
    /* SEE pruning: quiet margin * depth, noisy margin * depth^2 */ \
    TP(SEEPruningDepth,              9,     5,    13,    1) \
    TP(SEEQuietMargin,             -65,  -130,   -20,    5) \
    TP(SEENoisyMargin,             -19,   -50,    -5,    2) \
    /* extensions */ \
    TP(SingularDepth,                8,     4,    10,    1) \
    TP(DoubleExtMargin,            123,    40,   250,   10) \
    TP(HistexLimit,               9810,  4000, 16000,  600) \
    /* ttMoveHistory (singular double-extension margin scaling) */ \
    TP(TTMoveHistoryMax,          8196,  4096, 16384,  500) \
    TP(TTMoveHistoryScale,          42,    15,   100,    4) \
    TP(TTMoveHistBonus,            887,   400,  1600,   60) \
    TP(TTMoveHistMalus,            726,   300,  1400,   60) \
    /* late move reductions, LMRTable = LMRBase/100 + ln(d)ln(m) / (LMRDivisor/100) */ \
    TP(LMRBase,                     76,    25,   150,    6) \
    TP(LMRDivisor,                 225,   150,   350,   10) \
    TP(LMRQuietHistDiv,           5005,  2000, 10000,  400) \
    TP(LMRNoisyHistDiv,           5039,  2000, 10000,  400) \
    TP(AllNodeScale,               279,   100,   500,   20) \
    TP(AllNodeBase,                274,   100,   500,   20) \
    TP(LMRDeeperMargin,             40,    10,   100,    4) \
    TP(LMRShallowerMargin,           6,     0,    20,    1) \
    /* Surprise-SRD */ \
    TP(EVAL_DEFICIT_MARGIN,        119,    40,   250,   10) \
    TP(EVAL_SURPLUS_MARGIN,         94,    30,   250,   10) \
    TP(EVAL_MOVE_LIMIT,              4,     2,     8,    1)

#ifdef TUNE

#define TP_DECLARE(name, def, lo, hi, c) extern int name;
SEARCH_PARAMS(TP_DECLARE)
#undef TP_DECLARE

//print the parameters as UCI spin options (called from uciPrint)
extern void tunePrintUciOptions(void);
//handle "setoption name <param> value <n>"; returns 1 if <param> is tunable
extern int  tuneSetOption(const char *line);
//print the parameters in OpenBench SPSA format: name, int, value, min, max, c_end, r_end
extern void tunePrintSpsa(void);

#else

#define TP_CONSTANT(name, def, lo, hi, c) name = (def),
enum { SEARCH_PARAMS(TP_CONSTANT) };
#undef TP_CONSTANT

#endif

#endif // TUNE_H
