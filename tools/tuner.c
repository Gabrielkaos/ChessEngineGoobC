/*
 * GOOB Texel tuner.
 * Optimized with batching and Adam optimizer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <omp.h>

#include "defs.h"
#include "board.h"
#include "evaluate.h"
#include "init.h"
#include "bitboards.h"
#include "hashkeys.h"
#include "attacks.h"

#define MAX_POS 6000000
#define KAPPA   0.001667
#define LAMBDA  0.0

#define CKPT_FILE     "tuner_checkpoint.bin"
#define CKPT_BEST     "tuner_best.bin"
#define CKPT_MAGIC    0x474F4F42u /* "GOOB" */
#define CKPT_VERSION_SGD 1u
#define CKPT_VERSION_ADAM 2u
#define CKPT_VERSION CKPT_VERSION_ADAM
#define CKPT_EVERY    5

typedef struct {
    char pieces[64];
    char side;   
    char castle; 
    char ep;     
    int  fifty;
    float result;
} CPOS;

static CPOS *posdata;
static int npos = 0;
static double *probs;

double *m_mg = NULL;
double *v_mg = NULL;
double *m_eg = NULL;
double *v_eg = NULL;

static int *w0_init;
static int *w0_mg;
static int *w0_eg;

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
    if (r < 0) return 0;
    return strcmp(regs[r].name, "PiecesVal") == 0 ||
           strstr(regs[r].name, "Tabless") != NULL;
}

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
        int rank = 7 - r;
        for (f = 0; f < 8;) {
            char c = *p++;
            if (c >= '1' && c <= '8') f += c - '0';
            else { cp->pieces[rank * 8 + f] = piece_code(c); f++; }
        }
        p++;
    }
}

static int load_dataset(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    posdata = malloc(sizeof(CPOS) * MAX_POS);
    if (!posdata) return -1;
    char line[512];
    while (npos < MAX_POS && fgets(line, sizeof(line), fp)) {
        char *semi = strchr(line, ';');
        if (!semi) continue;
        *semi = '\0';
        CPOS *cp = &posdata[npos];
        memset(cp->pieces, 0, 64);
        parse_pieces(cp, line);
        char *sp = strchr(line, ' ');
        if (!sp) continue;
        sp++;
        cp->side = (*sp == 'w') ? WHITE : BLACK;
        sp = strchr(sp, ' ');
        if (!sp) continue;
        sp++;
        cp->castle = 0;
        if (*sp == '-') sp++;
        else {
            while (*sp != ' ') {
                if (*sp == 'K') cp->castle |= WKCA;
                if (*sp == 'Q') cp->castle |= WQCA;
                if (*sp == 'k') cp->castle |= BKCA;
                if (*sp == 'q') cp->castle |= BQCA;
                sp++;
            }
        }
        sp++;
        cp->ep = NO_SQ;
        if (*sp != '-') {
            cp->ep = (sp[0] - 'a') + (sp[1] - '1') * 8;
            sp += 2;
        }
        sp = strchr(sp, ' ');
        if (!sp) continue;
        cp->fifty = atoi(sp + 1);
        cp->result = atof(semi + 1);
        npos++;
    }
    fclose(fp);
    printf("loaded %d positions from %s\n", npos, path);
    return 0;
}

static void setup_pos(S_BOARD *pos, const CPOS *cp) {
    ResetBoard(pos);
    for (int i = 0; i < 64; i++) pos->pieces[i] = cp->pieces[i];
    pos->side = cp->side;
    pos->castleRights = cp->castle;
    pos->enPas = cp->ep;
    pos->fiftyMove = cp->fifty;
    pos->pkHash = GeneratePKHash(pos);
    pos->npHash[WHITE] = GenerateNonPawnHash(pos, WHITE);
    pos->npHash[BLACK] = GenerateNonPawnHash(pos, BLACK);
    pos->minorHash = GenerateMinorHash(pos);
    pos->posKey = GeneratePosKey(pos);
    updateListMaterial(pos);
}

static inline double sigmoid(double x) {
    if (x > 30) return 1.0;
    if (x < -30) return 0.0;
    return 1.0 / (1.0 + exp(-x));
}

static inline double white_eval(S_BOARD *pos) {
    double e = (double)EvalPosition(pos);
    return pos->side == WHITE ? e : -e;
}

/* pos->psqtmat is cached at setup time from PSQTMATTABLE, so after
   initPQSTMAT() it must be rebuilt or PSQT perturbations are invisible. */
static void refresh_batch_psqt(S_BOARD *bb, int len) {
    #pragma omp parallel for
    for (int i = 0; i < len; i++) {
        bb[i].psqtmat = 0;
        updateListMaterial(&bb[i]);
    }
}

static void dump_weights(const char *path) {
    FILE *fp = fopen(path, "w");
    if (!fp) return;
    fprintf(fp, "/* tuned weights (GOOB tuner output) */\n");
    for (int r = 0; r < NUM_REG; r++) {
        int c = regs[r].count;
        fprintf(fp, "int %s%s = {", regs[r].name, regs[r].shape);
        for (int i = 0; i < c; i++) {
            int w = regs[r].base[i];
            int mg = ScoreMG(w), eg = ScoreEG(w);
            if (i) fprintf(fp, ",");
            if (c == 1) fprintf(fp, "S(%3d,%3d)", mg, eg);
            else fprintf(fp, "%sS(%3d,%3d)", i % 8 == 0 ? "\n    " : " ", mg, eg);
        }
        fprintf(fp, "};\n");
    }
    fclose(fp);
    printf("wrote %s\n", path);
}

static void write_checkpoint(const char *path, int iter, double loss, double lr) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return;
    uint32_t magic = CKPT_MAGIC, version = CKPT_VERSION;
    int ok = fwrite(&magic, sizeof(magic), 1, fp) == 1 &&
             fwrite(&version, sizeof(version), 1, fp) == 1 &&
             fwrite(&iter, sizeof(iter), 1, fp) == 1 &&
             fwrite(&npos, sizeof(npos), 1, fp) == 1 &&
             fwrite(&nweights, sizeof(nweights), 1, fp) == 1 &&
             fwrite(&loss, sizeof(loss), 1, fp) == 1 &&
             fwrite(&lr, sizeof(lr), 1, fp) == 1;
    for (int k = 0; ok && k < nweights; k++) {
        int idx, r = reg_of(k, &idx);
        int w = regs[r].base[idx];
        ok = fwrite(&w, sizeof(w), 1, fp) == 1;
    }
    if (ok) ok = fwrite(w0_init, sizeof(int), nweights, fp) == (size_t)nweights &&
                 fwrite(w0_mg, sizeof(int), nweights, fp) == (size_t)nweights &&
                 fwrite(w0_eg, sizeof(int), nweights, fp) == (size_t)nweights;
    
    if (ok) ok = fwrite(m_mg, sizeof(double), nweights, fp) == (size_t)nweights &&
                 fwrite(v_mg, sizeof(double), nweights, fp) == (size_t)nweights &&
                 fwrite(m_eg, sizeof(double), nweights, fp) == (size_t)nweights &&
                 fwrite(v_eg, sizeof(double), nweights, fp) == (size_t)nweights;
                 
    if (fclose(fp) != 0) ok = 0;
    if (!ok) { remove(path); return; }
    printf("saved checkpoint %s (iter %d, loss %.6f)\n", path, iter, loss);
}

static int load_checkpoint(const char *path, double *loss, double *lr) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "cannot open checkpoint %s\n", path);
        return -1;
    }
    uint32_t magic = 0, version = 0;
    int iter, ckpt_npos, ckpt_nweights;
    if (fread(&magic, sizeof(magic), 1, fp) != 1 || magic != CKPT_MAGIC ||
        fread(&version, sizeof(version), 1, fp) != 1 ||
        fread(&iter, sizeof(iter), 1, fp) != 1 ||
        fread(&ckpt_npos, sizeof(ckpt_npos), 1, fp) != 1 ||
        fread(&ckpt_nweights, sizeof(ckpt_nweights), 1, fp) != 1 ||
        fread(loss, sizeof(*loss), 1, fp) != 1 ||
        fread(lr, sizeof(*lr), 1, fp) != 1) {
        fprintf(stderr, "bad checkpoint header: %s\n", path);
        fclose(fp);
        return -1;
    }
    if (version != CKPT_VERSION_SGD && version != CKPT_VERSION_ADAM) {
        fprintf(stderr, "bad checkpoint version: %u\n", version);
        fclose(fp);
        return -1;
    }
    if (ckpt_nweights != nweights) {
        fprintf(stderr, "checkpoint mismatch: %d weights, tuning %d weights\n", ckpt_nweights, nweights);
        fclose(fp);
        return -1;
    }
    int *wbuf = malloc(sizeof(int) * nweights);
    if (!wbuf || fread(wbuf, sizeof(int), nweights, fp) != (size_t)nweights ||
        fread(w0_init, sizeof(int), nweights, fp) != (size_t)nweights ||
        fread(w0_mg, sizeof(int), nweights, fp) != (size_t)nweights ||
        fread(w0_eg, sizeof(int), nweights, fp) != (size_t)nweights) {
        fprintf(stderr, "bad checkpoint body: %s\n", path);
        free(wbuf);
        fclose(fp);
        return -1;
    }

    if (version == CKPT_VERSION_ADAM) {
        if (fread(m_mg, sizeof(double), nweights, fp) != (size_t)nweights ||
            fread(v_mg, sizeof(double), nweights, fp) != (size_t)nweights ||
            fread(m_eg, sizeof(double), nweights, fp) != (size_t)nweights ||
            fread(v_eg, sizeof(double), nweights, fp) != (size_t)nweights) {
            fprintf(stderr, "bad checkpoint adam state: %s\n", path);
        }
    } else {
        printf("Loaded SGD checkpoint (v1). Adam momentum will start from 0.\n");
        memset(m_mg, 0, sizeof(double) * nweights);
        memset(v_mg, 0, sizeof(double) * nweights);
        memset(m_eg, 0, sizeof(double) * nweights);
        memset(v_eg, 0, sizeof(double) * nweights);
    }
    fclose(fp);

    for (int k = 0; k < nweights; k++) {
        int idx, r = reg_of(k, &idx);
        regs[r].base[idx] = wbuf[k];
    }
    free(wbuf);
    initPQSTMAT();
    return iter;
}

static const char *fmt_time(double secs, char *buf, int bufsz) {
    int s = (int)secs;
    if (s < 60) snprintf(buf, bufsz, "%ds", s);
    else if (s < 3600) snprintf(buf, bufsz, "%dm %02ds", s / 60, s % 60);
    else snprintf(buf, bufsz, "%dh %02dm %02ds", s / 3600, (s % 3600) / 60, s % 60);
    return buf;
}

int main(int argc, char **argv) {
    const char *dataset = argc > 1 ? argv[1] : "dataset.epd";
    int iterations = argc > 2 ? atoi(argv[2]) : 200;
    int nthreads = argc > 3 ? atoi(argv[3]) : omp_get_max_threads();
    double lr = argc > 4 ? atof(argv[4]) : 2.0; /* Adam uses much smaller lr */
    if (lr > 100.0) {
        printf("WARNING: You provided lr=%.1f, but Adam typically uses lr between 1.0 and 5.0!\n", lr);
    }
    int resume = (argc > 5 && strcmp(argv[5], "resume") == 0);

    if (load_dataset(dataset) < 0) return 1;

    AllInit();
    initPQSTMAT();
    tuneMode = 1;

    omp_set_num_threads(nthreads);

    woffset[0] = 0;
    for (int r = 0; r < NUM_REG; r++) woffset[r + 1] = woffset[r] + regs[r].count;
    nweights = woffset[NUM_REG];
    printf("tuning %d weights over %d positions, %d threads, lr=%.2f\n", nweights, npos, nthreads, lr);

    double *grad_mg = calloc(nweights, sizeof(double));
    double *grad_eg = calloc(nweights, sizeof(double));
    w0_init = calloc(nweights, sizeof(int));
    w0_mg = calloc(nweights, sizeof(int));
    w0_eg = calloc(nweights, sizeof(int));
    
    m_mg = calloc(nweights, sizeof(double));
    v_mg = calloc(nweights, sizeof(double));
    m_eg = calloc(nweights, sizeof(double));
    v_eg = calloc(nweights, sizeof(double));

    int start_iter = 1;
    double best_loss = 1e30;
    if (resume) {
        double ckpt_lr = 0.0;
        int it = load_checkpoint(CKPT_FILE, &best_loss, &ckpt_lr);
        if (it < 0) {
            fprintf(stderr, "resume failed, starting fresh\n");
            best_loss = 1e30;
        } else {
            start_iter = it + 1;
            /* If the user explicitly typed an LR, keep it. 
               If not, use the checkpoint LR ONLY if it's safe for Adam (< 100). */
            if (argc <= 4 && ckpt_lr < 100.0) {
                lr = ckpt_lr;
            }
            printf("resuming from iteration %d (loss %.6f, using lr %.2f)\n", it, best_loss, lr);
        }
    }
    if (start_iter == 1) {
        for (int k = 0; k < nweights; k++) {
            int idx, r = reg_of(k, &idx);
            w0_init[k] = regs[r].base[idx];
            w0_mg[k] = ScoreMG(regs[r].base[idx]);
            w0_eg[k] = ScoreEG(regs[r].base[idx]);
        }
    }

    int batch_size = 16384;
    S_BOARD *board_batch = calloc(batch_size, sizeof(S_BOARD));
    double *batch_base_evals = malloc(batch_size * sizeof(double));
    double *batch_probs = malloc(batch_size * sizeof(double));

    double t0 = omp_get_wtime();
    double adam_b1 = 0.9, adam_b2 = 0.999, adam_eps = 1e-8;
    int adam_t = start_iter - 1;

    for (int iter = start_iter; iter <= iterations; iter++) {
        adam_t++;
        double it0 = omp_get_wtime();
        printf("\n══════════════════════════════════════════════════════════════\n");
        printf("  ITERATION %d / %d  (overall %.1f%%)\n", iter, iterations, 100.0 * (iter - 1) / iterations);
        printf("══════════════════════════════════════════════════════════════\n");
        fflush(stdout);

        double epoch_loss = 0;
        memset(grad_mg, 0, sizeof(double) * nweights);
        memset(grad_eg, 0, sizeof(double) * nweights);

        int num_batches = (npos + batch_size - 1) / batch_size;

        for (int b = 0; b < num_batches; b++) {
            int b_start = b * batch_size;
            int b_len = (b_start + batch_size > npos) ? (npos - b_start) : batch_size;

            #pragma omp parallel for
            for (int i = 0; i < b_len; i++) {
                // Initialize board settings so they match what's needed for eval
                board_batch[i].useNNUE = 0;
                board_batch[i].usePKNet = 0;
                board_batch[i].useFiftyMoveRule = 0;
                board_batch[i].contempt = 0;
                board_batch[i].contemptDrawPenalty = 0;
                board_batch[i].contemptComplexity = 0;
                board_batch[i].chess960 = 0;
                
                setup_pos(&board_batch[i], &posdata[b_start + i]);
                batch_base_evals[i] = white_eval(&board_batch[i]);
                batch_probs[i] = sigmoid(KAPPA * batch_base_evals[i]);
            }

            double b_loss = 0;
            #pragma omp parallel for reduction(+ : b_loss)
            for (int i = 0; i < b_len; i++) {
                double p = batch_probs[i];
                double y = posdata[b_start + i].result;
                double clip = 1e-6;
                p = p < clip ? clip : (p > 1 - clip ? 1 - clip : p);
                b_loss += y * log(p) + (1 - y) * log(1 - p);
            }
            epoch_loss += b_loss;

            for (int k = 0; k < nweights; k++) {
                int idx, r = reg_of(k, &idx);
                double gm = 0, ge = 0;

                regs[r].base[idx] += 1;
                if (affects_psqt(k)) { initPQSTMAT(); refresh_batch_psqt(board_batch, b_len); }
                #pragma omp parallel for reduction(+ : gm)
                for (int i = 0; i < b_len; i++) {
                    double ev = white_eval(&board_batch[i]);
                    gm += (batch_probs[i] - posdata[b_start + i].result) * KAPPA * (ev - batch_base_evals[i]);
                }
                regs[r].base[idx] -= 1;

                int is_tempo = (strcmp(regs[r].name, "tempo") == 0);
                if (!is_tempo) {
                    regs[r].base[idx] += 65536;
                    if (affects_psqt(k)) { initPQSTMAT(); refresh_batch_psqt(board_batch, b_len); }
                    #pragma omp parallel for reduction(+ : ge)
                    for (int i = 0; i < b_len; i++) {
                        double ev = white_eval(&board_batch[i]);
                        ge += (batch_probs[i] - posdata[b_start + i].result) * KAPPA * (ev - batch_base_evals[i]);
                    }
                    regs[r].base[idx] -= 65536;
                }
                if (affects_psqt(k)) initPQSTMAT();

                grad_mg[k] += gm / npos;
                grad_eg[k] += ge / npos;
            }

            double elapsed = omp_get_wtime() - it0;
            double frac = (double)(b + 1) / num_batches;
            double eta = (frac > 0.001) ? elapsed / frac * (1.0 - frac) : 0;
            char ebuf[32], rbuf[32];
            printf("  [Batch %4d / %4d]  %5.1f%%  │  elapsed %s  │  ETA %s\r",
                   b + 1, num_batches, 100.0 * frac,
                   fmt_time(elapsed, ebuf, sizeof(ebuf)),
                   fmt_time(eta, rbuf, sizeof(rbuf)));
            fflush(stdout);
        }
        printf("\n");
        
        epoch_loss = -epoch_loss / npos;

        double gmax = 0, gsum = 0, gnon = 0;
        for (int k = 0; k < nweights; k++) {
            double a = fabs(grad_mg[k]) + fabs(grad_eg[k]);
            if (a > gmax) gmax = a;
            gsum += a;
            if (a > 1e-9) gnon++;
        }
        printf("  grad: max %.5f mean %.6f nonzero %d/%d\n", gmax, gsum / nweights, (int)gnon, nweights);

        for (int k = 0; k < nweights; k++) {
            int idx, r = reg_of(k, &idx);
            int w0 = regs[r].base[idx];
            int mg0 = ScoreMG(w0), eg0 = ScoreEG(w0);

            if (strcmp(regs[r].name, "tempo") == 0) {
                grad_mg[k] += LAMBDA * (double)(w0 - w0_init[k]) / npos;
            } else {
                grad_mg[k] += LAMBDA * (double)(mg0 - w0_mg[k]) / npos;
                grad_eg[k] += LAMBDA * (double)(eg0 - w0_eg[k]) / npos;
            }

            m_mg[k] = adam_b1 * m_mg[k] + (1 - adam_b1) * grad_mg[k];
            v_mg[k] = adam_b2 * v_mg[k] + (1 - adam_b2) * grad_mg[k] * grad_mg[k];
            double m_hat_mg = m_mg[k] / (1 - pow(adam_b1, adam_t));
            double v_hat_mg = v_mg[k] / (1 - pow(adam_b2, adam_t));
            double update_mg = lr * m_hat_mg / (sqrt(v_hat_mg) + adam_eps);

            double update_eg = 0;
            if (strcmp(regs[r].name, "tempo") != 0) {
                m_eg[k] = adam_b1 * m_eg[k] + (1 - adam_b1) * grad_eg[k];
                v_eg[k] = adam_b2 * v_eg[k] + (1 - adam_b2) * grad_eg[k] * grad_eg[k];
                double m_hat_eg = m_eg[k] / (1 - pow(adam_b1, adam_t));
                double v_hat_eg = v_eg[k] / (1 - pow(adam_b2, adam_t));
                update_eg = lr * m_hat_eg / (sqrt(v_hat_eg) + adam_eps);
            }

            if (strcmp(regs[r].name, "tempo") == 0) {
                int neww = (int)llround(w0 - update_mg);
                if (neww < 0) neww = 0; if (neww > 32767) neww = 32767;
                regs[r].base[idx] = neww;
            } else {
                int nmg = (int)llround(mg0 - update_mg);
                int neg = (int)llround(eg0 - update_eg);
                if (nmg < -32767) nmg = -32767; if (nmg > 32767) nmg = 32767;
                if (neg < -32767) neg = -32767; if (neg > 32767) neg = 32767;
                regs[r].base[idx] = S(nmg, neg);
            }
        }
        initPQSTMAT();
        printf("  after update: PiecesVal[1] = %d (S(%d,%d)), tempo = %d\n", PiecesVal[1], ScoreMG(PiecesVal[1]), ScoreEG(PiecesVal[1]), tempo);

        double dt = omp_get_wtime() - it0;
        double total_elapsed = omp_get_wtime() - t0;
        int iters_done = iter - start_iter + 1;
        int iters_left = iterations - iter;
        double avg_iter = total_elapsed / iters_done;
        double eta_total = avg_iter * iters_left;
        char dtbuf[32], tebuf[32], etbuf[32], aibuf[32];
        printf("──────────────────────────────────────────────────────────────\n");
        printf("  iter %4d │ loss %.6f │ iter time %s │ avg/iter %s\n", iter, epoch_loss, fmt_time(dt, dtbuf, sizeof(dtbuf)), fmt_time(avg_iter, aibuf, sizeof(aibuf)));
        printf("  progress  │ %d / %d iters (%.1f%%) │ elapsed %s │ ETA %s\n", iters_done, iterations - start_iter + 1, 100.0 * iter / iterations, fmt_time(total_elapsed, tebuf, sizeof(tebuf)), fmt_time(eta_total, etbuf, sizeof(etbuf)));
        printf("──────────────────────────────────────────────────────────────\n");
        fflush(stdout);

        if (iter % CKPT_EVERY == 0 || iter == iterations) write_checkpoint(CKPT_FILE, iter, epoch_loss, lr);
        if (epoch_loss < best_loss) {
            best_loss = epoch_loss;
            write_checkpoint(CKPT_BEST, iter, epoch_loss, lr);
            dump_weights("weights_best.c");
        }
        if (iter % 20 == 0 || iter == iterations) {
            char fname[128];
            snprintf(fname, sizeof(fname), "weights_%04d.c", iter);
            dump_weights(fname);
        }
    }
    
    free(board_batch);
    free(batch_base_evals);
    free(batch_probs);
    free(grad_mg); free(grad_eg); free(w0_init); free(w0_mg); free(w0_eg);
    free(m_mg); free(v_mg); free(m_eg); free(v_eg);

    printf("total %.0fs\n", omp_get_wtime() - t0);
    dump_weights("weights_final.c");
    return 0;
}
