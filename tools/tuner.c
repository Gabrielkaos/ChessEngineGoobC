/*
 * GOOB Texel tuner.
 *
 * Minimizes the cross-entropy loss  L = -1/N * sum( y*ln(p) + (1-y)*ln(1-p) )
 * with p = sigmoid(kappa * eval) over a dataset of positions/results.
 *
 * Gradient:  dL/dw_k = kappa * sum_i (p_i - y_i) * (eval_i(w+e_k) - eval_i(w))
 * computed by a unit finite difference of each weight (exact for the linear
 * eval terms; the king-safety quadratic term is handled exactly too because a
 * forward difference of a quadratic is exact).
 *
 * Usage: tuner <dataset.epd> [iterations] [threads] [learning_rate]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <omp.h>

#include "defs.h"
#include "board.h"
#include "evaluate.h"
#include "init.h"
#include "bitboards.h"
#include "hashkeys.h"
#include "attacks.h"

#define MAX_POS 600000
#define KAPPA   0.001667
#define LAMBDA  0.0

typedef struct {
    char pieces[64];
    char side;   /* WHITE=0, BLACK=1 */
    char castle; /* WKCA=1 WQCA=2 BKCA=4 BQCA=8 */
    char ep;     /* -1 for none */
    int  fifty;
    float result;
} CPOS;

static CPOS *posdata;
static int npos = 0;
static double *base_evals;
static double *probs;

static int *w0_init;
static int *w0_mg;
static int *w0_eg;

/* ------------------------------------------------------------------ */
/* weight registry                                                     */
/* ------------------------------------------------------------------ */
typedef struct {
    int *base;
    int count;
    const char *name;
    const char *shape;
} REG;

#define R(arr, shape) { (int *)(arr), (int)(sizeof(arr) / sizeof((arr)[0])), #arr, shape }
#define RN(arr, count, shape) { (int *)(arr), (count), #arr, shape }

static REG regs[] = {
    R(PiecesVal, "[7]"),
    R(PawnTabless, "[64]"),
    R(KnightTabless, "[64]"),
    R(BishopTabless, "[64]"),
    R(RookTabless, "[64]"),
    R(QueenTabless, "[64]"),
    R(KingTabless, "[64]"),
    RN(PassedPawn, 32, "[2][2][8]"),
    R(PassedFriendlyDistance, "[8]"),
    R(PassedEnemyDistance, "[8]"),
    { &PassedSafePromotionPath, 1, "PassedSafePromotionPath", "" },
    { &PassedProtectedByRook, 1, "PassedProtectedByRook", "" },
    { &SafetyKnightWeight, 1, "SafetyKnightWeight", "" },
    { &SafetyBishopWeight, 1, "SafetyBishopWeight", "" },
    { &SafetyRookWeight, 1, "SafetyRookWeight", "" },
    { &SafetyQueenWeight, 1, "SafetyQueenWeight", "" },
    { &SafetyAttackValue, 1, "SafetyAttackValue", "" },
    { &SafetyWeakSquares, 1, "SafetyWeakSquares", "" },
    { &SafetyNoEnemyQueens, 1, "SafetyNoEnemyQueens", "" },
    { &SafetySafeQueenCheck, 1, "SafetySafeQueenCheck", "" },
    { &SafetySafeRookCheck, 1, "SafetySafeRookCheck", "" },
    { &SafetySafeBishopCheck, 1, "SafetySafeBishopCheck", "" },
    { &SafetySafeKnightCheck, 1, "SafetySafeKnightCheck", "" },
    { &SafetyAdjustment, 1, "SafetyAdjustment", "" },
    RN(SafetyStorm, 16, "[2][8]"),
    RN(SafetyShelter, 16, "[2][8]"),
    RN(KingStorm, 64, "[2][4][8]"),
    R(KingDefenders, "[12]"),
    RN(KingShelter, 128, "[2][8][8]"),
    R(KingPawnFileProximity, "[8]"),
    { &QueenRelativePin, 1, "QueenRelativePin", "" },
    R(QueenMobility, "[28]"),
    RN(RookFile, 2, "[2]"),
    { &RookOnSeventh, 1, "RookOnSeventh", "" },
    R(RookMobility, "[15]"),
    { &KnightBehindPawn, 1, "KnightBehindPawn", "" },
    RN(KnightOutpost, 4, "[2][2]"),
    R(KnightMobility, "[9]"),
    R(KnightInSiberia, "[4]"),
    R(BishopMobility, "[14]"),
    { &BishopRammedPawns, 1, "BishopRammedPawns", "" },
    { &bishopPair, 1, "bishopPair", "" },
    { &BishopLongDiagonal, 1, "BishopLongDiagonal", "" },
    { &BishopBehindPawn, 1, "BishopBehindPawn", "" },
    RN(BishopOutpost, 4, "[2][2]"),
    RN(PawnCandidatePasser, 16, "[2][8]"),
    R(PawnIsolated, "[8]"),
    RN(PawnStacked, 16, "[2][8]"),
    RN(PawnBackwards, 16, "[2][8]"),
    R(PawnConnected32, "[32]"),
    R(BishopTrapped, "[2]"),
    { &RookTrapped, 1, "RookTrapped", "" },
    { &tempo, 1, "tempo", "" },
};

#define NUM_REG (sizeof(regs) / sizeof(regs[0]))

static int woffset[NUM_REG + 1];
static int nweights;

static int reg_of(int k, int *idx) {
    for (int r = 0; r < NUM_REG; r++) {
        if (k < woffset[r + 1]) {
            *idx = k - woffset[r];
            return r;
        }
    }
    return -1;
}

static int affects_psqt(int k) {
    int idx, r = reg_of(k, &idx);
    if (r < 0)
        return 0;
    return strcmp(regs[r].name, "PiecesVal") == 0 ||
           strstr(regs[r].name, "Tabless") != NULL;
}

/* ------------------------------------------------------------------ */
/* dataset loading                                                     */
/* ------------------------------------------------------------------ */
static int piece_code(char c) {
    switch (c) {
        case 'P': return wP; case 'N': return wN; case 'B': return wB;
        case 'R': return wR; case 'Q': return wQ; case 'K': return wK;
        case 'p': return bP; case 'n': return bN; case 'b': return bB;
        case 'r': return bR; case 'q': return bQ; case 'k': return bK;
        default:  return EMPTY;
    }
}

static void parse_pieces(CPOS *cp, const char *placement) {
    const char *p = placement;
    int r, f;
    for (r = 0; r < 8; r++) {
        for (f = 0; f < 8;) {
            char c = *p++;
            if (c >= '1' && c <= '8') {
                f += c - '0';
            } else {
                cp->pieces[r * 8 + f] = piece_code(c);
                f++;
            }
        }
        p++;
    }
}

static int load_dataset(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "cannot open %s\n", path);
        return -1;
    }
    posdata = malloc(sizeof(CPOS) * MAX_POS);
    if (!posdata) {
        fprintf(stderr, "oom\n");
        return -1;
    }
    char line[512];
    while (npos < MAX_POS && fgets(line, sizeof(line), fp)) {
        char *semi = strchr(line, ';');
        if (!semi)
            continue;
        *semi = '\0';
        CPOS *cp = &posdata[npos];
        memset(cp->pieces, 0, 64);
        parse_pieces(cp, line);
        char *sp = strchr(line, ' ');
        if (!sp)
            continue;
        sp++;
        cp->side = (*sp == 'w') ? WHITE : BLACK;
        sp = strchr(sp, ' ');
        if (!sp)
            continue;
        sp++;
        cp->castle = 0;
        if (*sp == '-') {
            sp++;
        } else {
            while (*sp != ' ') {
                if (*sp == 'K') cp->castle |= WKCA;
                if (*sp == 'Q') cp->castle |= WQCA;
                if (*sp == 'k') cp->castle |= BKCA;
                if (*sp == 'q') cp->castle |= BQCA;
                sp++;
            }
        }
        sp++;
        cp->ep = -1;
        if (*sp != '-') {
            cp->ep = (sp[0] - 'a') + (sp[1] - '1') * 8;
            sp += 2;
        }
        sp = strchr(sp, ' ');
        if (!sp)
            continue;
        cp->fifty = atoi(sp + 1);
        cp->result = atof(semi + 1);
        npos++;
    }
    fclose(fp);
    printf("loaded %d positions from %s\n", npos, path);
    return 0;
}

/* ------------------------------------------------------------------ */
/* board setup + eval                                                  */
/* ------------------------------------------------------------------ */
static S_BOARD *boardbuf;

static void setup_pos(S_BOARD *pos, const CPOS *cp) {
    ResetBoard(pos);
    for (int i = 0; i < 64; i++)
        pos->pieces[i] = cp->pieces[i];
    pos->side = cp->side;
    pos->castleRights = cp->castle;
    pos->enPas = cp->ep;
    pos->fiftyMove = cp->fifty;
    pos->pkHash = GeneratePKHash(pos);
    pos->posKey = GeneratePosKey(pos);
    updateListMaterial(pos);
}

static inline double sigmoid(double x) {
    if (x > 30) return 1.0;
    if (x < -30) return 0.0;
    return 1.0 / (1.0 + exp(-x));
}

/* ------------------------------------------------------------------ */
/* sweeps                                                              */
/* ------------------------------------------------------------------ */
static void compute_base_evals(void) {
    #pragma omp parallel
    {
        S_BOARD *pos = &boardbuf[omp_get_thread_num()];
        #pragma omp for
        for (int i = 0; i < npos; i++) {
            setup_pos(pos, &posdata[i]);
            base_evals[i] = (double)EvalPosition(pos);
            probs[i] = sigmoid(KAPPA * base_evals[i]);
        }
    }
}

static double compute_loss(void) {
    double loss = 0;
    #pragma omp parallel for reduction(+ : loss)
    for (int i = 0; i < npos; i++) {
        double p = probs[i];
        double y = posdata[i].result;
        double clip = 1e-6;
        p = p < clip ? clip : (p > 1 - clip ? 1 - clip : p);
        loss += y * log(p) + (1 - y) * log(1 - p);
    }
    return -loss / npos;
}

/* gradient of the loss wrt the MG and EG halves of weight k:
   g_mg from a +1 perturb (moves only the MG half), g_eg from a +65536
   perturb (moves only the EG half). */
static void weight_grad(int k, double *g_mg, double *g_eg) {
    int idx, r = reg_of(k, &idx);
    double gm = 0, ge = 0;

    regs[r].base[idx] += 1;
    if (affects_psqt(k))
        initPQSTMAT();
    #pragma omp parallel
    {
        S_BOARD *pos = &boardbuf[omp_get_thread_num()];
        #pragma omp for reduction(+ : gm)
        for (int i = 0; i < npos; i++) {
            setup_pos(pos, &posdata[i]);
            double ev = (double)EvalPosition(pos);
            gm += (probs[i] - posdata[i].result) * KAPPA * (ev - base_evals[i]) / npos;
        }
    }
    regs[r].base[idx] -= 1;

    regs[r].base[idx] += 65536;
    if (affects_psqt(k))
        initPQSTMAT();
    #pragma omp parallel
    {
        S_BOARD *pos = &boardbuf[omp_get_thread_num()];
        #pragma omp for reduction(+ : ge)
        for (int i = 0; i < npos; i++) {
            setup_pos(pos, &posdata[i]);
            double ev = (double)EvalPosition(pos);
            ge += (probs[i] - posdata[i].result) * KAPPA * (ev - base_evals[i]) / npos;
        }
    }
    regs[r].base[idx] -= 65536;
    if (affects_psqt(k))
        initPQSTMAT();

    /* L2 regularization toward the initial weight values */
    int w0 = regs[r].base[idx];
    int mg0 = ScoreMG(w0), eg0 = ScoreEG(w0);
    if (strcmp(regs[r].name, "tempo") == 0) {
        gm += LAMBDA * (double)(w0 - w0_init[k]) / npos;
        ge = 0;
    } else {
        gm += LAMBDA * (double)(mg0 - w0_mg[k]) / npos;
        ge += LAMBDA * (double)(eg0 - w0_eg[k]) / npos;
    }

    *g_mg = gm;
    *g_eg = ge;
}

/* ------------------------------------------------------------------ */
/* output                                                              */
/* ------------------------------------------------------------------ */
static void dump_weights(const char *path) {
    FILE *fp = fopen(path, "w");
    if (!fp)
        return;
    fprintf(fp, "/* tuned weights (GOOB tuner output) */\n");
    for (int r = 0; r < NUM_REG; r++) {
        int c = regs[r].count;
        fprintf(fp, "int %s%s = {", regs[r].name, regs[r].shape);
        for (int i = 0; i < c; i++) {
            int w = regs[r].base[i];
            int mg = ScoreMG(w), eg = ScoreEG(w);
            if (i)
                fprintf(fp, ",");
            if (c == 1)
                fprintf(fp, "S(%3d,%3d)", mg, eg);
            else
                fprintf(fp, "%sS(%3d,%3d)", i % 8 == 0 ? "\n    " : " ", mg, eg);
        }
        fprintf(fp, "};\n");
    }
    fclose(fp);
    printf("wrote %s\n", path);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv) {
    const char *dataset = argc > 1 ? argv[1] : "dataset.epd";
    int iterations = argc > 2 ? atoi(argv[2]) : 200;
    int nthreads = argc > 3 ? atoi(argv[3]) : omp_get_max_threads();
    double lr = argc > 4 ? atof(argv[4]) : 2000.0;

    if (load_dataset(dataset) < 0)
        return 1;

    AllInit();
    initPQSTMAT();
    tuneMode = 1;

    omp_set_num_threads(nthreads);
    int maxthr = omp_get_max_threads();
    boardbuf = calloc(maxthr, sizeof(S_BOARD));
    for (int t = 0; t < maxthr; t++) {
        boardbuf[t].useNNUE = 0;
        boardbuf[t].usePKNet = 0;
        boardbuf[t].useFiftyMoveRule = 1;
        boardbuf[t].contempt = 0;
        boardbuf[t].contemptDrawPenalty = 0;
        boardbuf[t].contemptComplexity = 0;
        boardbuf[t].chess960 = 0;
    }

    woffset[0] = 0;
    for (int r = 0; r < NUM_REG; r++)
        woffset[r + 1] = woffset[r] + regs[r].count;
    nweights = woffset[NUM_REG];
    printf("tuning %d weights over %d positions, %d threads, lr=%.2f\n",
           nweights, npos, nthreads, lr);

    base_evals = malloc(sizeof(double) * npos);
    probs = malloc(sizeof(double) * npos);

    compute_base_evals();
    printf("initial loss: %.6f\n", compute_loss());
    fflush(stdout);

    double t0 = omp_get_wtime();
    double *grad_mg = malloc(sizeof(double) * nweights);
    double *grad_eg = malloc(sizeof(double) * nweights);
    w0_init = malloc(sizeof(int) * nweights);
    w0_mg = malloc(sizeof(int) * nweights);
    w0_eg = malloc(sizeof(int) * nweights);
    for (int k = 0; k < nweights; k++) {
        int idx, r = reg_of(k, &idx);
        w0_init[k] = regs[r].base[idx];
        w0_mg[k] = ScoreMG(regs[r].base[idx]);
        w0_eg[k] = ScoreEG(regs[r].base[idx]);
    }
    for (int iter = 1; iter <= iterations; iter++) {
        double it0 = omp_get_wtime();
        for (int k = 0; k < nweights; k++)
            weight_grad(k, &grad_mg[k], &grad_eg[k]);
        double gmax = 0, gsum = 0, gnon = 0;
        for (int k = 0; k < nweights; k++) {
            double a = fabs(grad_mg[k]) + fabs(grad_eg[k]);
            if (a > gmax) gmax = a;
            gsum += a;
            if (a > 1e-9) gnon++;
        }
        printf("  grad: max %.5f mean %.6f nonzero %d/%d\n",
               gmax, gsum / nweights, (int)gnon, nweights);
        for (int k = 0; k < nweights; k++) {
            int idx, r = reg_of(k, &idx);
            int w = regs[r].base[idx];
            if (strcmp(regs[r].name, "tempo") == 0) {
                int neww = (int)llround(w - lr * grad_mg[k]);
                if (neww < 0) neww = 0;
                if (neww > 32767) neww = 32767;
                regs[r].base[idx] = neww;
            } else {
                int mg = ScoreMG(w), eg = ScoreEG(w);
                int nmg = (int)llround(mg - lr * grad_mg[k]);
                int neg = (int)llround(eg - lr * grad_eg[k]);
                if (nmg < -32767) nmg = -32767; if (nmg > 32767) nmg = 32767;
                if (neg < -32767) neg = -32767; if (neg > 32767) neg = 32767;
                regs[r].base[idx] = S(nmg, neg);
            }
        }
        initPQSTMAT();
        printf("  after update: PiecesVal[1] = %d (S(%d,%d)), tempo = %d\n",
               PiecesVal[1], ScoreMG(PiecesVal[1]), ScoreEG(PiecesVal[1]), tempo);
        compute_base_evals();
        double loss = compute_loss();
        double dt = omp_get_wtime() - it0;
        printf("iter %4d loss %.6f  %.1fs\n", iter, loss, dt);
        fflush(stdout);
        if (iter % 20 == 0 || iter == iterations) {
            char fname[128];
            snprintf(fname, sizeof(fname), "weights_%04d.c", iter);
            dump_weights(fname);
        }
    }
    free(grad_mg);
    free(grad_eg);
    printf("total %.0fs\n", omp_get_wtime() - t0);
    dump_weights("weights_final.c");
    return 0;
}
