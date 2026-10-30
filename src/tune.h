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
    TP(DeltaMarginQ,                97,    40,   250,   10) \
    TP(QSSeeMargin,                108,    40,   250,   10) \
    /* transposition table */ \
    TP(TTResearchMargin,           126,    50,   300,   12) \
    /* aspiration windows */ \
    TP(ScoreWindow,                  10,     4,    30,    2) \
    TP(WindowDepth,                  5,     2,     9,    1) \
    /* window growth after a fail, in percent of the current delta */ \
    TP(AspWidening,                 50,    20,   150,    6) \
    /* hindsight depth adjustment */ \
    TP(HindsightMargin,            167,    60,   320,   12) \
    /* razoring */ \
    /* RazoringDepth 0 disables razoring */ \
    TP(RazoringDepth,                1,     0,     6,    1) \
    TP(RazorMarginBase,            229,   100,   450,   18) \
    TP(RazorMarginCoeff,           166,    60,   320,   12) \
    /* reverse futility (beta) pruning */ \
    TP(BetaPruningDepth,             8,     4,    12,    1) \
    TP(BetaMargin,                  54,    20,   150,    6) \
    /* null move pruning, R = NMPBase + depth/NMPDepthDiv + min(NMPEvalMax,(eval-beta)/NMPEvalDiv) */ \
    TP(defaultNullMoveDepth,         2,     1,     5,    1) \
    TP(NMPBase,                      6,     2,     9,    1) \
    TP(NMPDepthDiv,                  6,     3,    10,    1) \
    TP(NMPEvalDiv,                 193,   100,   400,   15) \
    TP(NMPEvalMax,                   3,     1,     6,    1) \
    TP(NMPVerifyDepth,              16,     8,    24,    1) \
    /* internal iterative reduction */ \
    TP(IIRDepth,                     5,     3,    10,    1) \
    /* probcut */ \
    TP(probCutDepth,                 6,     3,     8,    1) \
    TP(probCutMargin,               89,    30,   200,    8) \
    /* quiet futility pruning (history limits: NI = not improving, I = improving) */ \
    TP(FutilityPruningDepth,         8,     4,    12,    1) \
    TP(FutilityMargin,              67,    30,   130,    5) \
    TP(FutilityMarginNoHistory,    100,    40,   220,    9) \
    TP(FutilityHistLimitNI,      12954,  4000, 20000,  800) \
    TP(FutilityHistLimitI,        5910,     0, 14000,  700) \
    /* counter / follow-up move history pruning */ \
    TP(CounterMoveHistLimitNI,       -279, -3000,  2000,  250) \
    TP(CounterMoveHistLimitI,    -1116, -4000,  1000,  250) \
    TP(FollowUpHistLimitNI,       -424, -3500,  1500,  250) \
    TP(FollowUpHistLimitI,       -1781, -4500,   500,  250) \
    /* capture futility pruning */ \
    TP(CaptureFutilityDepth,         6,     3,    10,    1) \
    TP(CaptureFutilityBase,         91,    30,   250,   10) \
    TP(CaptureFutilityPerDepth,    122,    50,   250,   10) \
    TP(CaptureFutilityHistDiv,      16,     4,    64,    3) \
    /* SEE pruning: quiet margin * depth, noisy margin * depth^2 - capture history / SEENoisyHistDiv */ \
    TP(SEEPruningDepth,              9,     5,    13,    1) \
    TP(SEEQuietMargin,             -73,  -130,   -20,    5) \
    TP(SEENoisyMargin,             -18,   -50,    -5,    2) \
    TP(SEENoisyHistDiv,             64,    16,   256,   10) \
    /* extensions; singular beta = ttValue - depth * SingularBetaMargin / 16 */ \
    TP(SingularDepth,                10,     4,    14,    1) \
    TP(SingularBetaMargin,          16,     8,    48,    2) \
    TP(DoubleExtMargin,            128,    40,   250,   10) \
    TP(TripleExtMargin,             64,     0,   200,    8) \
    TP(MaxDoubleExtensions,          6,     2,    12,    1) \
    TP(HistexLimit,               9859,  4000, 16000,  600) \
    /* ttMoveHistory (singular double-extension margin scaling) */ \
    TP(TTMoveHistoryMax,          8812,  4096, 16384,  500) \
    TP(TTMoveHistoryScale,          42,    15,   100,    4) \
    TP(TTMoveHistBonus,            955,   400,  1600,   60) \
    TP(TTMoveHistMalus,            726,   300,  1400,   60) \
    /* late move reductions, LMRTable = LMRBase/100 + ln(d)ln(m) / (LMRDivisor/100) */ \
    TP(LMRBase,                     77,    25,   150,    6) \
    TP(LMRDivisor,                 224,   150,   350,   10) \
    TP(LMRQuietHistDiv,           4984,  2000, 10000,  400) \
    TP(LMRNoisyHistDiv,           4873,  2000, 10000,  400) \
    TP(AllNodeScale,               277,   100,   500,   20) \
    TP(AllNodeBase,                288,   100,   500,   20) \
    TP(LMRDeeperMargin,             37,    10,   100,    4) \
    TP(LMRShallowerMargin,           7,     0,    20,    1) \
    /* Surprise-SRD */ \
    TP(EVAL_DEFICIT_MARGIN,        118,    40,   250,   10) \
    TP(EVAL_SURPLUS_MARGIN,         96,    30,   250,   10) \
    TP(EVAL_MOVE_LIMIT,              4,     2,     8,    1) \
    /* quiet history update */ \
    TP(HistMoveCountThreshold,        8,     4,    16,    1) \
    TP(HistMoveCountBonusMul,      1272,   768,  2048,   60) \
    TP(HistMalusDecay,              923,   800,  1000,   10) \
    /* history bonus, stat_bonus(d) = min(HistBonusMax, HistBonusQuad*d*d + HistBonusLinear*d - 118) */ \
    TP(HistBonusQuad,                 4,     0,    12,    1) \
    TP(HistBonusLinear,             191,   120,   320,   10) \
    TP(HistBonusMax,               1708,   800,  3000,  100) \
    /* share of a quiet history update given to low-ply and pawn history (/1024) */ \
    TP(LowPlyHistUpdate,            712,   256,  1536,   60) \
    TP(PawnHistBonus,              1104,   512,  2048,   75) \
    TP(PawnHistMalus,               459,   128,  1024,   45) \
    /* quiet move ordering */ \
    TP(GoodQuietThreshold,        -3000, -12000,    0,  600) \
    TP(QuietCheckBonus,           16384,  4096, 32768, 1400) \
    TP(QuietThreatMul,               20,     5,    40,    2) \
    /* correction history table weights (pawn, minor, non-pawn, continuation) */ \
    TP(CorrPawnWeight,              128,    64,   256,   10) \
    TP(CorrMinorWeight,              88,    32,   192,    8) \
    TP(CorrNonPawnWeight,           108,    32,   192,    8) \
    TP(ContCorrWeight,               73,    16,   160,    7)

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
