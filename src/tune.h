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
    TP(DeltaMarginQ,                95,    40,   250,   10) \
    TP(QSSeeMargin,                 99,    40,   250,   10) \
    /* transposition table */ \
    /* aspiration windows */ \
    TP(ScoreWindow,                  11,     4,    30,    2) \
    TP(WindowDepth,                  5,     2,     9,    1) \
    /* window growth after a fail, in percent of the current delta */ \
    TP(AspWidening,                 45,    20,   150,    6) \
    /* hindsight depth adjustment */ \
    TP(HindsightMargin,            174,    60,   320,   12) \
    /* razoring */ \
    /* RazoringDepth 0 disables razoring */ \
    TP(RazoringDepth,                0,     0,     6,    1) \
    TP(RazorMarginBase,            232,   100,   450,   18) \
    TP(RazorMarginCoeff,           172,    60,   320,   12) \
    /* reverse futility (beta) pruning */ \
    TP(BetaPruningDepth,             9,     4,    12,    1) \
    TP(BetaMargin,                  52,    20,   150,    6) \
    /* null move pruning, R = NMPBase + depth/NMPDepthDiv + min(NMPEvalMax,(eval-beta)/NMPEvalDiv) */ \
    TP(defaultNullMoveDepth,         3,     1,     5,    1) \
    TP(NMPBase,                      5,     2,     9,    1) \
    TP(NMPDepthDiv,                  6,     3,    10,    1) \
    TP(NMPEvalDiv,                 193,   100,   400,   15) \
    TP(NMPEvalMax,                   3,     1,     6,    1) \
    TP(NMPVerifyDepth,              16,     8,    24,    1) \
    /* internal iterative reduction */ \
    TP(IIRDepth,                     5,     3,    10,    1) \
    /* probcut */ \
    TP(probCutDepth,                 7,     3,     8,    1) \
    TP(probCutMargin,               95,    30,   200,    8) \
    /* quiet futility pruning (history limits: NI = not improving, I = improving) */ \
    TP(FutilityPruningDepth,         8,     4,    12,    1) \
    TP(FutilityMargin,              65,    30,   130,    5) \
    TP(FutilityMarginNoHistory,     92,    40,   220,    9) \
    TP(FutilityHistLimitNI,      12966,  4000, 20000,  800) \
    TP(FutilityHistLimitI,        5998,     0, 14000,  700) \
    /* counter / follow-up move history pruning */ \
    TP(CounterMoveHistLimitNI,       -451, -3000,  2000,  250) \
    TP(CounterMoveHistLimitI,    -1027, -4000,  1000,  250) \
    TP(FollowUpHistLimitNI,       -539, -3500,  1500,  250) \
    TP(FollowUpHistLimitI,       -1735, -4500,   500,  250) \
    /* capture futility pruning */ \
    /* SEE pruning: quiet margin * depth, noisy margin * depth^2 - capture history / SEENoisyHistDiv */ \
    TP(SEEPruningDepth,              9,     5,    13,    1) \
    TP(SEEQuietMargin,             -75,  -130,   -20,    5) \
    TP(SEENoisyMargin,             -19,   -50,    -5,    2) \
    TP(SEENoisyHistDiv,             66,    16,   256,   10) \
    /* extensions; singular beta = ttValue - depth * SingularBetaMargin / 16 */ \
    TP(SingularDepth,                 8,     4,    14,    1) \
    TP(SingularBetaMargin,          17,     8,    48,    2) \
    TP(DoubleExtMargin,            121,    40,   250,   10) \
    TP(TripleExtMargin,             60,     0,   200,    8) \
    TP(MaxDoubleExtensions,          6,     2,    12,    1) \
    /* ttMoveHistory (singular double-extension margin scaling) */ \
    TP(TTMoveHistoryMax,          8700,  4096, 16384,  500) \
    TP(TTMoveHistoryScale,          40,    15,   100,    4) \
    TP(TTMoveHistBonus,            934,   400,  1600,   60) \
    TP(TTMoveHistMalus,            780,   300,  1400,   60) \
    /* late move reductions, LMRTable = LMRBase/100 + ln(d)ln(m) / (LMRDivisor/100) */ \
    TP(LMRBase,                     79,    25,   150,    6) \
    TP(LMRDivisor,                 226,   150,   350,   10) \
    TP(LMRQuietHistDiv,           4994,  2000, 10000,  400) \
    TP(LMRNoisyHistDiv,           4718,  2000, 10000,  400) \
    TP(AllNodeScale,               274,   100,   500,   20) \
    TP(AllNodeBase,                297,   100,   500,   20) \
    TP(LMRDeeperMargin,             34,    10,   100,    4) \
    TP(LMRShallowerMargin,           7,     0,    20,    1) \
    /* quiet history update */ \
    TP(HistMoveCountThreshold,        7,     4,    16,    1) \
    TP(HistMoveCountBonusMul,      1292,   768,  2048,   60) \
    TP(HistMalusDecay,              931,   800,  1000,   10) \
    /* history bonus, stat_bonus(d) = min(HistBonusMax, HistBonusQuad*d*d + HistBonusLinear*d - 118) */ \
    TP(HistBonusQuad,                 4,     0,    12,    1) \
    TP(HistBonusLinear,             203,   120,   320,   10) \
    TP(HistBonusMax,               1740,   800,  3000,  100) \
    /* share of a quiet history update given to low-ply and pawn history (/1024) */ \
    TP(LowPlyHistUpdate,            706,   256,  1536,   60) \
    TP(PawnHistBonus,              1110,   512,  2048,   75) \
    TP(PawnHistMalus,               471,   128,  1024,   45) \
    /* quiet move ordering */ \
    TP(GoodQuietThreshold,        -2886, -12000,    0,  600) \
    TP(QuietCheckBonus,           16672,  4096, 32768, 1400) \
    TP(QuietThreatMul,               21,     5,    40,    2) \
    /* correction history table weights (pawn, minor, non-pawn, continuation) */ \
    TP(CorrPawnWeight,              132,    64,   256,   10) \
    TP(CorrMinorWeight,              90,    32,   192,    8) \
    TP(CorrNonPawnWeight,           109,    32,   192,    8) \
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
