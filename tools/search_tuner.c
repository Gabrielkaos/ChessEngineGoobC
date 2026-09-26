/*
 * GOOB search-constant tuner  --  texel / SPT over the search parameters
 * ===========================================================================
 *
 * WHAT THIS IS
 * ------------
 * The eval is a NNUE trained on Stockfish evaluations, so the static eval is
 * already as good as it is going to get without more training.  What is left
 * on the table is the *search*: the pruning margins, reduction shapes, LMR
 * counts and window sizes that decide how much of the tree gets searched for
 * a given node budget.  Those are exactly the constants that end up
 * mismatched to a new network, because they were inherited from a different
 * eval.  This tool tunes them, with the NNUE in the loop, by minimising a
 * texel loss over a fixed-node search.
 *
 * Build:   make -C src tuner      (produces src/bin/linux/search-tuner)
 * Run:     src/bin/linux/search-tuner --help
 *
 * THE LOSS
 * --------
 * Texel tuning in general fits a scoring function to a target by minimising
 * the squared error between "what the function says the outcome is" and "what
 * is known about the outcome":
 *
 *     q  = tanh(score / CP_SCALE)     in (-1, 1), score from the point of
 *                                      view of the side to move
 *     p  = (1 + q) / 2                 the implied result
 *     r  = (1 + t) / 2                 the target result
 *     L  = mean over positions of (p - r)^2
 *
 * This is the same squared-error-on-implied-result the original texel paper
 * and Stockfish's fishtest tuning use.  The one thing that decides whether
 * the tuning means anything is where `t` comes from, because `t` is the only
 * thing telling the search what "better" means.  Three sources are supported
 * and they are NOT interchangeable:
 *
 *   --mode texel   t = 2*gameResult - 1, from "FEN;result" in the dataset.
 *                 The real texel method, and the only one that measures game
 *                 outcomes.  Needs self-play data (see tools/datagen.py).
 *
 *   --mode ref     t = tanh(refScore / CP_SCALE), where refScore is a
 *                 stronger engine's score for the same position.  This is
 *                 SPT / SPO.  It is the right default when all you have is an
 *                 eval-derived dataset, because it asks a well-posed
 *                 question: "make our fixed-node search agree with a stronger
 *                 engine's search of the same positions".
 *
 *                 Produce the reference file with tools/gen_ref_scores.py,
 *                 which drives a reference engine over UCI and writes both the
 *                 int32 scores and the exact positions it scored:
 *
 *                   search-tuner --data val.bin --limit 200000 \
 *                       --dump-fens fens.txt --steps 1 --positions 1
 *                   python3 tools/gen_ref_scores.py --engine ./sf \
 *                       --in fens.txt --out ref.bin --nodes 200000
 *                   search-tuner --data ref.bin.fen --ref ref.bin --mode ref \
 *                       --nodes 20000 --positions 8192 --steps 500
 *
 *                 Note the mismatch that makes the objective non-trivial: the
 *                 reference is scored at a *larger* budget than --nodes, so
 *                 there is a real gap for the constants to close.  Pointing
 *                 both at the same budget asks the search to reproduce what it
 *                 already does and the gradient is ~0.
 *
 *   --mode label   t = tanh(stockfishCp / CP_SCALE), using the int16 eval
 *                 already stored in the 68-byte training records.
 *                 Convenient, but be careful: the network was trained on
 *                 those same labels, so the static eval already predicts them
 *                 and the gradient mostly asks the search to change as little
 *                 as possible.  Use it to smoke-test the tool, not to tune.
 *
 * THE PROTOCOL: FIXED NODES, NOT FIXED DEPTH
 * ------------------------------------------
 * Almost every constant here is a speed knob.  At a fixed depth, pruning
 * more just changes how the same node count gets spent and the accuracy
 * difference is small and noisy.  At a fixed *node budget*, pruning more only
 * pays off if the nodes it saves are spent on moves that actually matter.  So
 * each position is searched to a node budget (--nodes) and the score at the
 * last completed depth is what gets compared.  Use --depth only if you
 * specifically want to tune for equal-depth accuracy.
 *
 * HOW THE GRADIENT IS COMPUTED
 * ----------------------------
 * A fixed-node search score is an integer and piecewise constant in the
 * parameters, so there is no analytic gradient.  Two estimators:
 *
 *   --method spsa    2 loss evaluations per step, whatever the parameter
 *                    count.  Perturb every parameter by +/- step_k with a
 *                    random sign and differentiate the two losses.  Noisy per
 *                    step, cheap.  This is the default.
 *
 *   --method fd      exact central differences: 2 evaluations per parameter.
 *                    By default restricted to a random subset of --fd-batch
 *                    parameters per step, which keeps the cost around SPSA's
 *                    while cutting the variance a lot.  --method fdall does
 *                    every parameter (2*|params| evaluations per step).
 *
 * Both feed an Adam (or plain SGD) update.  Positions are re-sampled every
 * step, and the *same* sample is used for every loss evaluation inside one
 * step, so the difference being differentiated is not confounded by which
 * positions happened to been drawn.
 *
 * The update is sized in units of each parameter's own finite-difference step,
 * because that is the smallest change that reliably moves the loss and because
 * the natural scales here differ by three orders of magnitude (HistexLimit
 * ~10000, probCutDepth ~5).  The fractional part is carried between updates:
 * every parameter is an int, so a bare round-to-nearest would silently throw
 * away any movement under half a step and freeze the parameter instead of
 * moving it slowly.  See applyGradient().
 *
 * WHY POSITIONS ARE ISOLATED
 * --------------------------
 * Every position is searched with a cleared transposition table, cleared
 * history tables and fresh search-thread stacks.  Without that the loss would
 * depend on the order positions were visited in, and the finite difference
 * between two parameter settings would be dominated by that ordering rather
 * than by the parameters.  Pass --isolate 0 to let history accumulate across
 * positions (about 10x cheaper, but then you are measuring something closer
 * to continuous play and the gradient is noisier).
 *
 * A NOTE ON THE 68-BYTE RECORDS
 * -----------------------------
 * The binary training records store board, side to move, a centipawn eval and
 * a mate flag -- and nothing else.  Castling rights are therefore inferred
 * from the piece placement, and the en passant square is set to none.  Both
 * affect the search, so if you care about correctness here, feed the tuner
 * FEN text instead (tools/datagen.py output) which carries the real fields.
 *
 * OUTPUT
 * ------
 * The tuned values are written as "tune set NAME VALUE" lines, which is
 * exactly what the engine's own `tune load <file>` command consumes:
 *
 *     tune load tuned_search.txt
 *
 * HOW TO TELL WHETHER A RUN ACTUALLY HELPED
 * -----------------------------------------
 * Three loss numbers get printed and they are not interchangeable:
 *
 *   train  the batch this step's gradient was taken from.  A training loss,
 *          redrawn every step, so it is NOT comparable between steps.
 *   val    a fixed held-out split, and the number the returned checkpoint is
 *          selected on.  Because the selection is a running argmin over every
 *          checkpoint, this one is optimistically biased by the spread of the
 *          noise -- a 300-position val set will happily report a 10% "gain"
 *          that is entirely the argmin of eight noisy estimates.
 *   held   a second slice, carved out of the training positions and physically
 *          removed from the pool so no gradient step ever samples it.  Never
 *          selected on.  This is the number to believe, and the final verdict
 *          is based on it.
 *
 * A run where val falls and `held` does not has fitted the validation noise.
 * The usual causes are too few positions per step, too small a --val split, or
 * a --nodes budget so small that none of these constants change the search at
 * all -- the step line says "(NOTHING MOVED)" when that is happening.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <omp.h>

#include "defs.h"
#include "board.h"
#include "evaluate.h"
#include "init.h"
#include "hashkeys.h"
#include "attacks.h"
#include "tune.h"
#include "search.h"
#include "pvtable.h"
#include "tt_eval.h"
#include "correction.h"
#include "misc.h"
#include "nnue_loader.h"

/* The score scale the network was trained with (see
   tools/nnue_project/scripts/model.py).  It is also the natural unit for the
   tanh() below: one pawn is about 0.76 in q-space. */
#define CP_SCALE 400.0

#define CKPT_MAGIC   0x474F4F42u   /* "GOOB" */
#define CKPT_VERSION 1u

/* Dataset cap. Records are held as a FEN string plus a double, ~144 bytes
   each, so 2M positions is ~290 MB. */
#define MAX_SAMPLE 2000000

#define RECORD_SIZE 68

/* ───────────────────────────── options ────────────────────────────────── */

typedef enum { MODE_TEXEL, MODE_REF, MODE_LABEL } TMode;
typedef enum { METH_SPSA, METH_FD, METH_FDALL } TMethod;
typedef enum { OPT_ADAM, OPT_SGD } TOptim;

static struct {
    const char *dataPath;
    const char *refPath;
    const char *outPath;
    const char *ckptPath;
    const char *paramsSpec;
    const char *dumpFens;
    TMode    mode;
    TMethod  method;
    TOptim   optim;
    U64      nodes;         /* node budget per position                 */
    int      depth;         /* depth cap, used only when --depth given  */
    int      useDepth;
    int      positions;     /* positions per gradient step              */
    int      limit;         /* use only the first N dataset positions    */
    int      valPositions;  /* validation positions per evaluation       */
    int      steps;
    int      threads;
    int      hashMB;        /* per-worker transposition table          */
    int      evalHashMB;
    int      fdBatch;       /* --method fd: parameters per step, 0=all */
    int      isolate;
    int      valPct;        /* validation split, percent                */
    int      evalEvery;
    int      verbose;
    int      listOnly;
    double   lr, beta1, beta2, eps;
    uint64_t seed;
} opt = {
    .dataPath = NULL, .refPath = NULL,
    .outPath  = "tuned_search.txt", .ckptPath = "search_tune_ckpt.bin",
    .paramsSpec = "all", .dumpFens = NULL,
    .mode = MODE_LABEL, .method = METH_SPSA, .optim = OPT_ADAM,
    .nodes = 20000, .depth = 0, .useDepth = 0,
    .positions = 4096, .limit = 0, .valPositions = 8192, .steps = 300, .threads = 0,
    .hashMB = 4, .evalHashMB = 4, .fdBatch = 8,
    .isolate = 1, .valPct = 10, .evalEvery = 10,
    .verbose = 0, .listOnly = 0,
    .lr = 1.0, .beta1 = 0.9, .beta2 = 0.999, .eps = 1e-8,
    .seed = 12345,
};

static const char *modeName(void) {
    return opt.mode == MODE_TEXEL ? "texel" : opt.mode == MODE_REF ? "ref" : "label";
}

/* ───────────────────────────── dataset ────────────────────────────────── */

/* One position plus its target t in (-1, 1), always from the point of view of
   the side to move. */
typedef struct {
    char   fen[128];
    double target;
} TPOS;

static TPOS *train = NULL, *val = NULL;
static int   nTrain = 0, nVal = 0;
static int   nSkipped = 0;

/* Cheap structural check, so a corrupt 20M-record file does not turn into
   20M "FEN Not Valid" lines on stdout from inside the engine's parser. */
static int fenLooksPlayable(const char *fen) {
    int rank = RANK_8, file = FILE_A, kings[2] = {0, 0};
    for (const char *p = fen; *p && *p != ' '; ++p) {
        if (*p == '/') {
            if (file != 8 || --rank < RANK_1) return 0;
            file = FILE_A;
            continue;
        }
        if (*p >= '1' && *p <= '8') { file += *p - '0'; continue; }
        if (!strchr("PNBRQKpnbrqk", *p)) return 0;
        if (file > FILE_H) return 0;
        if ((*p == 'P' || *p == 'p') && (rank == RANK_1 || rank == RANK_8)) return 0;
        if (*p == 'K') kings[WHITE]++;
        if (*p == 'k') kings[BLACK]++;
        file++;
    }
    if (rank != RANK_1 || file != 8) return 0;
    return kings[WHITE] == 1 && kings[BLACK] == 1;
}

/* Castling rights are not in the 68-byte records, so infer them from the
   placement.  Exact for any position where the king and rooks have not moved
   and the board is legal, which is what the training data is. */
static void inferCastling(const char *fen, char *out) {
    int wk = 0, bk = 0, wrook = 0, brook = 0;
    int rank = RANK_8, file = FILE_A;
    for (const char *p = fen; *p && *p != ' '; ++p) {
        if (*p == '/') { rank--; file = FILE_A; continue; }
        if (*p >= '1' && *p <= '8') { file += *p - '0'; continue; }
        int sq = rank * 8 + file;
        if (*p == 'K' && sq == E1) wk = 1;
        else if (*p == 'k' && sq == E8) bk = 1;
        else if (*p == 'R' && sq == H1) wrook |= 1;
        else if (*p == 'R' && sq == A1) wrook |= 2;
        else if (*p == 'r' && sq == H8) brook |= 1;
        else if (*p == 'r' && sq == A8) brook |= 2;
        file++;
    }
    int n = 0;
    if (wk && (wrook & 1)) out[n++] = 'K';
    if (wk && (wrook & 2)) out[n++] = 'Q';
    if (bk && (brook & 1)) out[n++] = 'k';
    if (bk && (brook & 2)) out[n++] = 'q';
    if (!n) out[n++] = '-';
    out[n] = '\0';
}

static void pushPos(const char *fen, double target) {
    if (nTrain >= MAX_SAMPLE) return;
    if (!fenLooksPlayable(fen)) { nSkipped++; return; }
    snprintf(train[nTrain].fen, sizeof(train[nTrain].fen), "%s", fen);
    train[nTrain].target = target;
    nTrain++;
}

/* ---- format A: the project's 68-byte training records --------------------
     bytes[0:64]  piece codes (0 empty, 1..6 white P..K, 7..12 black p..k)
     byte[64]      side to move (0 white, 1 black)
     bytes[65:67]  int16 LE centipawn eval, white-relative
     byte[67]      flags, bit0 = mate
   Memory-mapped, so a multi-GB file is fine. */
static int loadBinary(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "cannot open %s\n", path); return -1; }

    struct stat sb;
    if (fstat(fd, &sb) != 0) { fprintf(stderr, "stat failed on %s\n", path); close(fd); return -1; }
    long long bytes = (long long)sb.st_size;
    if (bytes % RECORD_SIZE != 0) {
        fprintf(stderr, "warning: %s is %lld bytes, not a multiple of %d; truncating %lld\n",
                path, bytes, RECORD_SIZE, bytes % RECORD_SIZE);
        bytes -= bytes % RECORD_SIZE;
    }
    size_t nrec = (size_t)(bytes / RECORD_SIZE);
    if (opt.limit > 0 && nrec > (size_t)opt.limit) {
        printf("dataset: %s -- using the first %d of %zu records\n", path, opt.limit, nrec);
        nrec = (size_t)opt.limit;
    } else {
        printf("dataset: %s -- %zu records (%.2f GB)\n", path, nrec, (double)bytes / 1e9);
    }

    const unsigned char *raw = mmap(NULL, (size_t)bytes, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (raw == MAP_FAILED) { fprintf(stderr, "mmap failed on %s\n", path); return -1; }

    static const char pcs[] = "PNBRQKpnbrqk";

    for (size_t i = 0; i < nrec; i++) {
        const unsigned char *r = raw + (size_t)i * RECORD_SIZE;

        if (r[67] & 1) { nSkipped++; continue; }   /* mate: no usable cp label */

        char fen[128];
        int  n = 0;
        for (int rk = RANK_8; rk >= RANK_1; rk--) {
            int run = 0;
            for (int f = FILE_A; f <= FILE_H; f++) {
                int code = r[rk * 8 + f];
                if (!code) { run++; continue; }
                if (run) { fen[n++] = (char)('0' + run); run = 0; }
                if (code < 1 || code > 12) { n = -1; break; }
                fen[n++] = pcs[code - 1];
            }
            if (n < 0) break;
            if (run) fen[n++] = (char)('0' + run);
            if (rk > RANK_1) fen[n++] = '/';
        }
        if (n < 0) { nSkipped++; continue; }
        fen[n] = '\0';
        n += snprintf(fen + n, sizeof(fen) - n, " %c", r[64] == 0 ? 'w' : 'b');

        char castle[8];
        inferCastling(fen, castle);
        snprintf(fen + n, sizeof(fen) - n, " %s - 0 1", castle);

        int16_t cp;
        memcpy(&cp, r + 65, 2);
        int sideRel = (r[64] == 0) ? (int)cp : -(int)cp;

        pushPos(fen, tanh((double)sideRel / CP_SCALE));
    }

    munmap((void *)raw, (size_t)bytes);
    return 0;
}

/* ---- format B: text, one "FEN;result" or bare "FEN" per line ------------ */
static int loadText(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }

    char line[512];
    long long lines = 0;
    int warnedLong = 0;
    while (fgets(line, sizeof(line), f)) lines++;
    rewind(f);
    if (lines > MAX_SAMPLE) lines = MAX_SAMPLE;
    if (opt.limit > 0 && lines > opt.limit) {
        printf("dataset: %s -- using the first %d of %lld lines\n", path, opt.limit, lines);
        lines = opt.limit;
    } else {
        printf("dataset: %s -- %lld lines\n", path, lines);
    }

    while (nTrain < lines && fgets(line, sizeof(line), f)) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) *nl = '\0';
        if (!line[0] || line[0] == '#') continue;

        char *semi = strchr(line, ';');
        if (semi) *semi = '\0';

        /* Refuse a line that cannot fit rather than truncating it.  A FEN cut
           short is not a FEN, and the failure would be silent: the tail holds
           the side to move, the halfmove clock and the fullmove number, so a
           truncated position would be scored as if the mover were white and
           the clocks were all zero. */
        size_t len = strlen(line);
        if (len >= sizeof(train[0].fen)) {
            if (!warnedLong) {
                warnedLong = 1;
                fprintf(stderr, "warning: %s has lines longer than %zu chars; "
                                "skipping them (a truncated FEN is not a FEN)\n",
                        path, sizeof(train[0].fen) - 1);
            }
            nSkipped++;
            continue;
        }

        char fen[128];
        memcpy(fen, line, len + 1);
        if (!fen[0]) continue;

        /* side to move, for flipping a white-POV result into side-to-move POV */
        int stmBlack = 0;
        for (int i = 0; fen[i]; i++) if (fen[i] == ' ') { stmBlack = (fen[i+1] == 'b'); break; }

        double t = 0.0;
        if (semi && semi[1]) {
            if (opt.mode == MODE_TEXEL) {
                double g = atof(semi + 1);              /* white's POV */
                if (g < 0.0) g = 0.0;
                if (g > 1.0) g = 1.0;
                double stmResult = stmBlack ? 1.0 - g : g;
                t = 2.0 * stmResult - 1.0;
            } else {
                t = tanh(atof(semi + 1) / CP_SCALE);
            }
        }
        /* no ';' field: t stays 0, i.e. a draw target. In texel mode that
           position contributes no gradient (see the warning in main). */
        pushPos(fen, t);
    }
    fclose(f);
    return 0;
}

/* ---- format C: reference scores, int32 per position, dataset order ------ */
static int loadRefScores(const char *path) {
    if (!path) { fprintf(stderr, "--mode ref needs --ref <file>\n"); return -1; }
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    fseek(f, 0, SEEK_END);
    long long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    fclose(f);

    if (bytes % 4 != 0) { fprintf(stderr, "%s is not a stream of int32\n", path); return -1; }
    size_t n = (size_t)(bytes / 4);
    if (n != (size_t)nTrain) {
        fprintf(stderr, "%s holds %zu scores but the dataset yielded %d positions\n",
                path, n, nTrain);
        return -1;
    }

    int32_t *sc = malloc(n * sizeof(int32_t));
    if (!sc) return -1;
    f = fopen(path, "rb");
    if (!f || fread(sc, sizeof(int32_t), n, f) != n) {
        fprintf(stderr, "short read on %s\n", path);
        if (f) fclose(f);
        free(sc);
        return -1;
    }
    fclose(f);

    /* Reference scores are side-to-move relative (same convention as the
       search score they are being compared against). */
    for (size_t i = 0; i < n; i++) train[i].target = tanh((double)sc[i] / CP_SCALE);
    free(sc);
    printf("reference scores: %zu from %s\n", n, path);
    return 0;
}

/* Write the FENs in exactly the order a --ref file has to supply scores for.
 *
 * --ref is a flat stream of int32 indexed by position, so any disagreement
 * about which positions survive loading (unparseable FENs, mate-flagged
 * records, the sample cap) would silently shift every score onto the wrong
 * position rather than fail.  Dumping the list here makes the two sides agree
 * by construction; feed it to tools/gen_ref_scores.py and the counts match. */
static int dumpFens(const char *path, int n) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return -1; }
    for (int i = 0; i < n; i++) fprintf(f, "%s\n", train[i].fen);
    fclose(f);
    printf("wrote %d FENs to %s\n", n, path);
    return 0;
}

static int loadDataset(void) {
    train = malloc(sizeof(TPOS) * (size_t)MAX_SAMPLE);
    if (!train) { fprintf(stderr, "out of memory\n"); return -1; }

    size_t len = strlen(opt.dataPath);
    int isBin = (len > 4 && !strcmp(opt.dataPath + len - 4, ".bin"));
    int rc = isBin ? loadBinary(opt.dataPath) : loadText(opt.dataPath);
    if (rc != 0) return rc;
    if (nTrain == 0) { fprintf(stderr, "no usable positions in %s\n", opt.dataPath); return -1; }

    /* Before the split, and so before the targets are replaced: the order here
       is the order --ref is indexed by. */
    if (opt.dumpFens && dumpFens(opt.dumpFens, nTrain) != 0) return -1;

    if (opt.mode == MODE_REF && loadRefScores(opt.refPath) != 0) return -1;

    int wantVal = (int)((long long)nTrain * opt.valPct / 100);
    if (wantVal > nTrain - 1) wantVal = nTrain - 1;   /* always keep >=1 train pos */
    if (wantVal > 0) {
        val = malloc(sizeof(TPOS) * (size_t)wantVal);
        if (!val) { fprintf(stderr, "out of memory\n"); return -1; }
        /* take the tail, so both halves come from the same distribution */
        for (int i = 0; i < wantVal; i++) val[i] = train[nTrain - wantVal + i];
        nVal = wantVal;
        nTrain -= wantVal;
    }
    printf("loaded %d train / %d validation positions (%d records skipped)\n",
           nTrain, nVal, nSkipped);
    return 0;
}

/* ───────────────────────── per-thread workers ────────────────────────── */

typedef struct {
    S_BOARD          board;
    S_SEARCH_THREAD *search;
    S_SHARED_TABLES *shared;
    S_PVTABLE        tt;
    S_SEARCHINFO     info;
    long long        searches;
} WORKER;

static WORKER *workers = NULL;
static int nWorkers = 0;

/* Search one position to the configured budget.  Everything that could carry
   state between positions is reset first, so the result is a pure function of
   (fen, ST). */
static int scoreOne(WORKER *w, const char *fen) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", fen);

    if (ParseFEN(buf, &w->board) != 0) return VALUE_NONE;

    /* ParseFEN does ResetBoard, which leaves search/shared/eTable alone. */
    w->board.search = w->search;
    w->board.shared = w->shared;
    w->board.useFiftyMoveRule = TRUE;
    w->board.chess960 = FALSE;

    clearPvTable(&w->tt);
    clearEvalTable(w->board.eTable);
    if (opt.isolate) resetContinuationTable(&w->board);
    initStacks(&w->board);

    int score = SearchPositionFixed(&w->board, &w->info, &w->tt,
                                    opt.useDepth ? opt.depth : 0, opt.nodes);
    w->searches++;
    return score;
}

static int setupWorkers(void) {
    nWorkers = opt.threads > 0 ? opt.threads : omp_get_max_threads();
    workers = calloc((size_t)nWorkers, sizeof(WORKER));
    if (!workers) return -1;

    for (int i = 0; i < nWorkers; i++) {
        WORKER *w = &workers[i];
        size_t sharedSize = (sizeof(S_SHARED_TABLES) + 63) & ~(size_t)63;
        w->search = alloc_search_thread();
        w->shared = aligned_alloc(64, sharedSize);
        if (!w->search || !w->shared) {
            fprintf(stderr, "worker %d: allocation failed\n", i);
            return -1;
        }
        memset(w->shared, 0, sharedSize);

        w->board.eTable->evalTable = NULL;
        InitEvalTable(w->board.eTable, opt.evalHashMB, 0);
        InitPvTable(&w->tt, opt.hashMB, 0);

        memset(&w->info, 0, sizeof(S_SEARCHINFO));
        w->info.mateLimit = -1;   /* IterativeDeepening tests this */
        w->info.multiPV  = 1;
        w->info.quit     = FALSE;
    }
    return 0;
}

static void freeWorkers(void) {
    if (!workers) return;
    for (int i = 0; i < nWorkers; i++) {
        free(workers[i].search);
        free(workers[i].shared);
        free(workers[i].board.eTable->evalTable);
        free(workers[i].tt.pTable);
    }
    free(workers);
    workers = NULL;
}

/* ─────────────────────────── the loss ─────────────────────────────────── */

/* Mean squared error between the search's implied result and the target. */
static double evalLoss(TPOS *set, int n, const int *idx, int *usedOut) {
    double   total = 0.0;
    long long used = 0;

    #pragma omp parallel for reduction(+:total,used) schedule(dynamic, 32)
    for (int i = 0; i < n; i++) {
        WORKER *w = &workers[omp_get_thread_num()];
        int s = scoreOne(w, set[idx[i]].fen);

        /* No legal move, or a forced mate: no meaningful implied result on the
           tanh scale, so the sample is dropped rather than clamped. */
        if (s == VALUE_NONE || s <= -ISMATE || s >= ISMATE) continue;

        double p = (1.0 + tanh((double)s / CP_SCALE)) * 0.5;
        double r = (set[idx[i]].target * 0.5) + 0.5;
        double d = p - r;
        total += d * d;
        used++;
    }
    if (usedOut) *usedOut = (int)used;
    return used > 0 ? total / (double)used : 1e9;
}

static long long totalSearches(void) {
    long long s = 0;
    for (int i = 0; i < nWorkers; i++) s += workers[i].searches;
    return s;
}

static double nowSecs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ─────────────────────────── optimiser ────────────────────────────────── */

/* Defined with the rest of the parameter-subset machinery below. */
static char *paramEnabled;

static double   *gradM = NULL, *gradV = NULL;
static double   *updAcc = NULL;   /* fractional carry, in parameter units */
static long long gradStep = 0;

static int optimInit(void) {
    gradM   = calloc((size_t)tuneNumEntries, sizeof(double));
    gradV   = calloc((size_t)tuneNumEntries, sizeof(double));
    updAcc  = calloc((size_t)tuneNumEntries, sizeof(double));
    if (!gradM || !gradV || !updAcc) return -1;
    gradStep = 0;
    return 0;
}

/* One Adam (or SGD) step.
 *
 * The update is measured in units of each parameter's own `step`, because that
 * is the smallest change that reliably moves the loss: `HistexLimit` has step
 * 500 and `probCutDepth` has step 1, so a single absolute --lr cannot mean the
 * same thing for both.  Note that scaling the *gradient* would not help --
 * Adam's m/sqrt(v) is invariant under it -- the quantum has to be applied here,
 * at the update site.
 *
 * The fractional carry exists because every parameter is an int: llround() is a
 * cliff, where anything under half a step rounds away and is silently lost.
 * Without the carry, any --lr below 0.5 would freeze a parameter forever rather
 * than move it slowly.  With it, --lr is a rate: a parameter moves one step
 * every ceil(1/lr) updates. */
static void applyGradient(const double *g) {
    gradStep++;
    double bc1 = 1.0 - pow(opt.beta1, (double)gradStep);
    double bc2 = 1.0 - pow(opt.beta2, (double)gradStep);

    for (int i = 0; i < tuneNumEntries; i++) {
        if (paramEnabled && !paramEnabled[i]) continue;

        int *slot = tuneTable[i].slot;
        double m, v;
        if (opt.optim == OPT_ADAM) {
            gradM[i] = opt.beta1 * gradM[i] + (1.0 - opt.beta1) * g[i];
            gradV[i] = opt.beta2 * gradV[i] + (1.0 - opt.beta2) * g[i] * g[i];
            m = gradM[i] / bc1;
            v = gradV[i] / bc2;
        } else {
            m = g[i];
            v = g[i] * g[i];
        }

        double norm = (opt.optim == OPT_ADAM) ? m / (sqrt(v) + opt.eps) : m;
        double want = opt.lr * (double)tuneTable[i].step * norm;

        updAcc[i] -= want;                 /* gradient says lower -> negative delta */
        int whole = (int)updAcc[i];        /* toward zero; the rest carries over */
        if (whole == 0) continue;
        updAcc[i] -= whole;

        int nv = *slot + whole;
        if (nv < tuneTable[i].lo) nv = tuneTable[i].lo;
        if (nv > tuneTable[i].hi) nv = tuneTable[i].hi;
        *slot = nv;

        /* A parameter sitting on a bound must not accumulate a debt it can
         * never spend: the clamp would eat the movement and the carry would
         * keep growing, so the parameter would be launched off the bound the
         * moment it came loose.  Drop the carry instead. */
        if (nv <= tuneTable[i].lo || nv >= tuneTable[i].hi) updAcc[i] = 0.0;
    }
    tuneClampAndRebuild();
}

/* ─────────────────────── parameter subset selection ───────────────────── */

static int  *activeParams = NULL;
static int   nActive = 0;

static int buildParamSubset(const char *spec) {
    paramEnabled  = calloc((size_t)tuneNumEntries, sizeof(char));
    activeParams  = malloc(sizeof(int) * (size_t)tuneNumEntries);
    if (!paramEnabled || !activeParams) return -1;

    if (!strcmp(spec, "all")) {
        for (int i = 0; i < tuneNumEntries; i++) paramEnabled[i] = 1;
    } else if (spec[0] == '@') {
        FILE *f = fopen(spec + 1, "r");
        if (!f) { fprintf(stderr, "cannot open parameter list %s\n", spec + 1); return -1; }
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            char *nl = strpbrk(line, "\r\n");
            if (nl) *nl = '\0';
            if (line[0] == '#' || line[0] == '\0') continue;
            char name[256];
            if (sscanf(line, "%255s", name) != 1) continue;
            int i = tuneFind(name);
            if (i < 0) fprintf(stderr, "warning: unknown parameter '%s' in %s\n", name, spec + 1);
            else paramEnabled[i] = 1;
        }
        fclose(f);
    } else {
        char copy[4096];
        snprintf(copy, sizeof(copy), "%s", spec);
        for (char *tok = strtok(copy, ","); tok; tok = strtok(NULL, ",")) {
            while (*tok == ' ') tok++;
            if (!*tok) continue;
            int i = tuneFind(tok);
            if (i < 0) { fprintf(stderr, "unknown parameter '%s'\n", tok); return -1; }
            paramEnabled[i] = 1;
        }
    }

    nActive = 0;
    for (int i = 0; i < tuneNumEntries; i++)
        if (paramEnabled[i]) activeParams[nActive++] = i;

    if (nActive == 0) { fprintf(stderr, "no parameters selected\n"); return -1; }
    return 0;
}

static int atBound(int i) {
    int v = *tuneTable[i].slot;
    return v <= tuneTable[i].lo || v >= tuneTable[i].hi;
}

/* ────────────────────────────── rng ───────────────────────────────────── */

static uint64_t rngState;

static inline uint64_t rnd64(void) {
    /* splitmix64: no warm-up, no state table -- plenty for choosing subsets
       and perturbation signs. */
    rngState += 0x9E3779B97F4A7C15ULL;
    uint64_t z = rngState;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void shuffle(int *a, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(rnd64() % (uint64_t)(i + 1));
        int t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

/* ───────────────────────── checkpointing ──────────────────────────────── */

static void writeCheckpoint(int step, double score) {
    FILE *f = fopen(opt.ckptPath, "wb");
    if (!f) return;
    uint32_t magic = CKPT_MAGIC, ver = CKPT_VERSION;
    int ok = fwrite(&magic, 4, 1, f) == 1 &&
             fwrite(&ver, 4, 1, f) == 1 &&
             fwrite(&step, sizeof(step), 1, f) == 1 &&
             fwrite(&score, sizeof(score), 1, f) == 1;
    for (int i = 0; ok && i < tuneNumEntries; i++)
        ok = fwrite(tuneTable[i].slot, sizeof(int), 1, f) == 1;
    if (ok && gradM)  ok = fwrite(gradM,  sizeof(double), (size_t)tuneNumEntries, f) == (size_t)tuneNumEntries;
    if (ok && gradV)  ok = fwrite(gradV,  sizeof(double), (size_t)tuneNumEntries, f) == (size_t)tuneNumEntries;
    if (ok && updAcc) ok = fwrite(updAcc, sizeof(double), (size_t)tuneNumEntries, f) == (size_t)tuneNumEntries;
    if (fclose(f) != 0) ok = 0;

    if (ok) printf("  checkpoint: %s (step %d, score %.8f)\n", opt.ckptPath, step, score);
    else { remove(opt.ckptPath); fprintf(stderr, "  warning: could not write checkpoint\n"); }
}

static int readCheckpoint(void) {
    FILE *f = fopen(opt.ckptPath, "rb");
    if (!f) return -1;

    uint32_t magic = 0, ver = 0;
    int step = 0;
    double score = 0.0;
    if (fread(&magic, 4, 1, f) != 1 || magic != CKPT_MAGIC ||
        fread(&ver, 4, 1, f) != 1 || ver != CKPT_VERSION ||
        fread(&step, sizeof(step), 1, f) != 1 ||
        fread(&score, sizeof(score), 1, f) != 1) {
        fprintf(stderr, "ignoring unreadable checkpoint %s\n", opt.ckptPath);
        fclose(f);
        return -1;
    }
    for (int i = 0; i < tuneNumEntries; i++) {
        int v;
        if (fread(&v, sizeof(int), 1, f) != 1) {
            fprintf(stderr, "truncated checkpoint %s\n", opt.ckptPath);
            fclose(f);
            return -1;
        }
        *tuneTable[i].slot = v;
    }
    /* Adam moments and the fractional carry are optional trailing state: a
       file written before they were recorded still restores the values and the
       step count.  A short read just leaves them zeroed, i.e. a fresh Adam. */
    if (gradM)  (void)!fread(gradM,  sizeof(double), (size_t)tuneNumEntries, f);
    if (gradV)  (void)!fread(gradV,  sizeof(double), (size_t)tuneNumEntries, f);
    if (updAcc) (void)!fread(updAcc, sizeof(double), (size_t)tuneNumEntries, f);
    gradStep = step;
    fclose(f);

    tuneClampAndRebuild();
    printf("resumed from %s: step %d, score %.8f\n", opt.ckptPath, step, score);
    return step;
}

/* ───────────────────────────── output ─────────────────────────────────── */

static void writeOutput(const char *path, double loss, double valLoss) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }

    fprintf(f, "# GOOB tuned search parameters\n");
    fprintf(f, "# mode=%s  method=%s  budget=%s  train=%d  val=%d  steps=%d\n",
            modeName(),
            opt.method == METH_SPSA ? "spsa" : opt.method == METH_FD ? "fd" : "fdall",
            opt.useDepth ? "depth" : "nodes", nTrain, nVal, opt.steps);
    fprintf(f, "# loss=%.8f  val_loss=%.8f\n", loss, valLoss);
    fprintf(f, "# apply with:  tune load %s\n", path);
    fprintf(f, "#\n# %-30s %8s %8s %8s %6s %8s\n",
            "name", "default", "lo", "hi", "step", "tuned");

    int changed = 0, pinned = 0;
    for (int i = 0; i < tuneNumEntries; i++) {
        int v = *tuneTable[i].slot;
        if (v != tuneTable[i].def) changed++;
        if (atBound(i))       pinned++;
        fprintf(f, "tune set %s %d\n", tuneTable[i].name, v);
    }
    fprintf(f, "# %d of %d parameters differ from the defaults", changed, tuneNumEntries);
    if (pinned) fprintf(f, "; %d are sitting on a bound", pinned);
    fprintf(f, "\n");
    fclose(f);

    printf("\nwrote %s: %d of %d parameters changed", path, changed, tuneNumEntries);
    if (pinned) printf(", %d on a bound", pinned);
    printf("\n");
}

static void printTunedTable(void) {
    printf("\n%-30s %8s %8s %8s %6s %8s\n", "name", "default", "lo", "hi", "step", "tuned");
    for (int i = 0; i < tuneNumEntries; i++)
        printf("%-30s %8d %8d %8d %6d %8d%s\n",
               tuneTable[i].name, tuneTable[i].def, tuneTable[i].lo, tuneTable[i].hi,
               tuneTable[i].step, *tuneTable[i].slot,
               *tuneTable[i].slot == tuneTable[i].def ? "" :
               (atBound(i) ? "  <- AT BOUND" : ""));
}

/* ───────────────────────────── usage ──────────────────────────────────── */

static void usage(void) {
    printf(
"GOOB search-constant tuner\n"
"\n"
"usage: search-tuner --data <file> [options]\n"
"\n"
"dataset\n"
"  --data <file>        .bin  68-byte training records (board,stm,cp,mate)\n"
"                       text one 'FEN' or 'FEN;result' per line\n"
"  --limit <n>          use only the first n positions   (default: all)\n"
"  --ref <file>         int32 reference scores, one per dataset position\n"
"                       (see --dump-fens for how to generate that file)\n"
"  --mode <m>           texel | ref | label       (default label)\n"
"                       texel  target = game result from 'FEN;result'\n"
"                       ref    target = a stronger engine's score (SPT)\n"
"                       label  target = the stockfish cp stored in the record\n"
"\n"
"search budget\n"
"  --nodes <n>          node budget per position       (default 20000)\n"
"  --depth <d>          depth cap instead of a node budget\n"
"\n"
"optimisation\n"
"  --method <m>         spsa | fd | fdall              (default spsa)\n"
"  --fd-batch <n>       parameters per fd step, 0=all  (default 8)\n"
"  --steps <n>          gradient steps                  (default 300)\n"
"  --positions <n>      positions per step              (default 4096)\n"
"  --lr <f>             step size, in units of each parameter's own\n"
"                       finite-difference step: 1.0 moves every parameter one\n"
"                       'step' per update, 0.25 one step every four.\n"
"                                                    (default 1.0)\n"
"  --optim <o>          adam | sgd                      (default adam)\n"
"  --params <spec>      all | name,name,... | @file     (default all)\n"
"  --seed <n>           rng seed                        (default 12345)\n"
"\n"
"resources / misc\n"
"  --threads <n>        OpenMP threads                  (default: all cores)\n"
"  --hash <mb>          per-thread TT                   (default 4)\n"
"  --evalhash <mb>      per-thread eval cache           (default 4)\n"
"  --isolate <0|1>      clear history between positions (default 1)\n"
"  --val <pct>          validation split percent        (default 10)\n"
"  --val-positions <n>  validation positions per check  (default 8192)\n"
"  --eval-every <n>     validation every n steps        (default 10)\n"
"  --out <file>         tuned parameter file (default tuned_search.txt)\n"
"  --ckpt <file>        checkpoint         (default search_tune_ckpt.bin)\n"
"  --resume             continue from the checkpoint\n"
"  --dump-fens <file>   write the loaded FENs (in --ref order) and exit; feed\n"
"                       them to tools/gen_ref_scores.py to build the --ref file\n"
"  --list               print the parameter table and exit\n"
"  --verbose            per-step parameter table\n"
"  --help\n"
"\n"
"examples\n"
"  # smoke test: is everything wired up? (fast, proves nothing -- see --mode)\n"
"  src/bin/linux/search-tuner --data tools/nnue_project/data_new/clean/val.bin \\\n"
"      --limit 5000 --mode label --steps 5 --positions 512 --nodes 8000\n"
"\n"
"  # the real thing: match a stronger engine's fixed-node search (SPT).\n"
"  # Re-loading the same --data/--limit gives the same positions in the same\n"
"  # order, so the ref file lines up with the dataset without re-dumping FENs.\n"
"  src/bin/linux/search-tuner --data tools/nnue_project/data_new/clean/val.bin \\\n"
"      --limit 200000 --dump-fens fens.txt --steps 1 --positions 1\n"
"  python3 tools/gen_ref_scores.py --engine ./sf --in fens.txt \\\n"
"      --out ref.bin --nodes 200000\n"
"  src/bin/linux/search-tuner --data tools/nnue_project/data_new/clean/val.bin \\\n"
"      --limit 200000 --ref ref.bin --mode ref \\\n"
"      --nodes 20000 --positions 8192 --steps 500\n"
"\n"
"  # gen_ref_scores.py also writes <out>.fen, the exact positions it scored.\n"
"  # Pointing --data at that file is the surest way to keep the two in step:\n"
"  src/bin/linux/search-tuner --data ref.bin.fen --ref ref.bin --mode ref ...\n");
}

/* ────────────────────────────── main ──────────────────────────────────── */

int main(int argc, char **argv) {
    int resume = 0;

    #define NEXT(_n)                                                    \
        do {                                                            \
            if (i + 1 >= argc) { fprintf(stderr, "%s needs a value\n", _n); \
                                 return 1; }                            \
            argv[++i];                                                  \
        } while (0)

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else if (!strcmp(a, "--data"))       { NEXT("--data");   opt.dataPath  = argv[i]; }
        else if (!strcmp(a, "--ref"))        { NEXT("--ref");    opt.refPath   = argv[i]; }
        else if (!strcmp(a, "--out"))        { NEXT("--out");    opt.outPath   = argv[i]; }
        else if (!strcmp(a, "--ckpt"))       { NEXT("--ckpt");   opt.ckptPath  = argv[i]; }
        else if (!strcmp(a, "--params"))     { NEXT("--params"); opt.paramsSpec = argv[i]; }
        else if (!strcmp(a, "--dump-fens"))  { NEXT("--dump-fens"); opt.dumpFens = argv[i]; }
        else if (!strcmp(a, "--positions"))  { NEXT("--positions"); opt.positions = atoi(argv[i]); }
        else if (!strcmp(a, "--limit"))      { NEXT("--limit"); opt.limit = atoi(argv[i]); }
        else if (!strcmp(a, "--val-positions")) { NEXT("--val-positions"); opt.valPositions = atoi(argv[i]); }
        else if (!strcmp(a, "--steps"))      { NEXT("--steps");   opt.steps     = atoi(argv[i]); }
        else if (!strcmp(a, "--threads"))    { NEXT("--threads"); opt.threads   = atoi(argv[i]); }
        else if (!strcmp(a, "--hash"))       { NEXT("--hash");    opt.hashMB    = atoi(argv[i]); }
        else if (!strcmp(a, "--evalhash"))   { NEXT("--evalhash");opt.evalHashMB= atoi(argv[i]); }
        else if (!strcmp(a, "--fd-batch"))   { NEXT("--fd-batch");opt.fdBatch  = atoi(argv[i]); }
        else if (!strcmp(a, "--isolate"))    { NEXT("--isolate"); opt.isolate   = atoi(argv[i]); }
        else if (!strcmp(a, "--val"))        { NEXT("--val");     opt.valPct    = atoi(argv[i]); }
        else if (!strcmp(a, "--eval-every")) { NEXT("--eval-every"); opt.evalEvery = atoi(argv[i]); }
        else if (!strcmp(a, "--mode")) {
            NEXT("--mode");
            if      (!strcmp(argv[i], "texel")) opt.mode = MODE_TEXEL;
            else if (!strcmp(argv[i], "ref"))   opt.mode = MODE_REF;
            else if (!strcmp(argv[i], "label")) opt.mode = MODE_LABEL;
            else { fprintf(stderr, "--mode must be texel, ref or label\n"); return 1; }
        }
        else if (!strcmp(a, "--method")) {
            NEXT("--method");
            if      (!strcmp(argv[i], "spsa"))  opt.method = METH_SPSA;
            else if (!strcmp(argv[i], "fd"))    opt.method = METH_FD;
            else if (!strcmp(argv[i], "fdall")) opt.method = METH_FDALL;
            else { fprintf(stderr, "--method must be spsa, fd or fdall\n"); return 1; }
        }
        else if (!strcmp(a, "--optim")) {
            NEXT("--optim");
            if      (!strcmp(argv[i], "adam")) opt.optim = OPT_ADAM;
            else if (!strcmp(argv[i], "sgd"))  opt.optim = OPT_SGD;
            else { fprintf(stderr, "--optim must be adam or sgd\n"); return 1; }
        }
        else if (!strcmp(a, "--nodes"))  { NEXT("--nodes");  opt.nodes = strtoull(argv[i], NULL, 10); }
        else if (!strcmp(a, "--depth"))  { NEXT("--depth");  opt.depth = atoi(argv[i]); opt.useDepth = 1; }
        else if (!strcmp(a, "--lr"))     { NEXT("--lr");     opt.lr = atof(argv[i]); }
        else if (!strcmp(a, "--seed"))   { NEXT("--seed");   opt.seed = strtoull(argv[i], NULL, 10); }
        else if (!strcmp(a, "--verbose")) opt.verbose = 1;
        else if (!strcmp(a, "--resume"))  resume = 1;
        else if (!strcmp(a, "--list"))    opt.listOnly = 1;
        else { fprintf(stderr, "unknown option: %s\n", a); usage(); return 1; }
    }
    #undef NEXT

    if (opt.listOnly) {
        /* ST is a zeroed global until something fills it (AllInit does it in a
           normal engine run), so without this the "current" column would just
           be a column of zeroes. */
        tuneSetDefaults();
        tunePrintAll(stdout);
        return 0;
    }
    if (!opt.dataPath) { fprintf(stderr, "--data is required\n\n"); usage(); return 1; }

    if (opt.isolate   < 0) opt.isolate   = 0;
    if (opt.isolate   > 1) opt.isolate   = 1;
    if (opt.evalEvery  < 1) opt.evalEvery  = 1;
    if (opt.hashMB    < 1) opt.hashMB    = 1;
    if (opt.evalHashMB< 1) opt.evalHashMB= 1;
    if (opt.valPct    < 0) opt.valPct    = 0;
    if (opt.valPct   > 50) opt.valPct   = 50;
    if (opt.positions < 1) opt.positions = 1;
    if (opt.valPositions < 1) opt.valPositions = 1;
    if (opt.steps     < 1) opt.steps     = 1;
    rngState = opt.seed ? opt.seed : 1;

    /* Engine init. tuneSetDefaults() lives inside AllInit() and runs before
       initLMRTable(), so ST is populated and the LMR table matches it. */
    AllInit();
    if (!nnue_init(NULL)) {
        fprintf(stderr, "FATAL: could not load the network (weights/quantised.bin).\n"
                        "       Build from src/ so the embedded net resolves.\n");
        return 1;
    }
    tuneQuiet = 1;   /* constant for the whole run, safe to read from threads */

    if (opt.mode == MODE_LABEL)
        printf("note: --mode label fits the search to the same labels the network was\n"
               "      trained on, so the gradient is weak by construction.\n"
               "      Use --mode ref (or texel) for a real tuning run.\n");
    if (opt.mode == MODE_TEXEL)
        printf("note: --mode texel needs real game results ('FEN;result'). Positions with\n"
               "      no result get a draw target and contribute no gradient.\n");

    if (loadDataset() != 0) return 1;
    if (opt.positions > nTrain) opt.positions = nTrain;

    if (buildParamSubset(opt.paramsSpec) != 0) return 1;
    if (optimInit() != 0) { fprintf(stderr, "out of memory\n"); return 1; }
    if (setupWorkers() != 0) return 1;
    omp_set_num_threads(nWorkers);

    if (opt.mode == MODE_TEXEL) {
        int withResult = 0;
        for (int i = 0; i < nTrain; i++) if (train[i].target != 0.0) withResult++;
        if (withResult * 2 < nTrain)
            printf("warning: only %d of %d training positions carry a result; the rest are\n"
                   "         pinned to a draw and will flatten the gradient.\n",
                   withResult, nTrain);
    }

    printf("tuning %d of %d parameters | %d steps x %d positions | budget %s | %d threads | isolate %d\n",
           nActive, tuneNumEntries, opt.steps, opt.positions,
           opt.useDepth ? "depth" : "nodes", nWorkers, opt.isolate);
    if (opt.useDepth) printf("  depth cap %d\n", opt.depth);
    else              printf("  node budget %llu\n", (unsigned long long)opt.nodes);

    int startStep = resume ? readCheckpoint() : 0;
    if (startStep < 0) startStep = 0;

    int *pool   = malloc(sizeof(int) * (size_t)nTrain);
    int *sample = malloc(sizeof(int) * (size_t)opt.positions);
    int *valIdx = malloc(sizeof(int) * (size_t)(nVal > 0 ? nVal : 1));
    double *g       = calloc((size_t)tuneNumEntries, sizeof(double));
    int   *delta    = calloc((size_t)tuneNumEntries, sizeof(int));
    int   *subset   = malloc(sizeof(int) * (size_t)tuneNumEntries);
    int   *saved    = malloc(sizeof(int) * (size_t)tuneNumEntries);
    int   *probe    = malloc(sizeof(int) * (size_t)tuneNumEntries);
    int   *evalIdx  = malloc(sizeof(int) * (size_t)opt.positions);
    if (!pool || !sample || !valIdx || !g || !delta || !subset || !saved ||
        !probe || !evalIdx) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    for (int i = 0; i < nTrain; i++) pool[i] = i;
    for (int i = 0; i < nVal; i++)   valIdx[i] = i;
    shuffle(pool, nTrain);
    shuffle(valIdx, nVal);

    /* The split is a percentage of a multi-million-position dataset, but every
       validation pass costs a full batch of searches -- a 10% split of 1.8M
       positions is 200k searches, which is more than the entire optimisation
       step it is supposed to be judging.  Shuffle then take a fixed prefix:
       the subset is random but identical on every evaluation, so two numbers
       are comparable, and a bigger --val just buys a less noisy estimate. */
    int nValUse = nVal < opt.valPositions ? nVal : opt.valPositions;
    if (nVal > 0 && nValUse < nVal)
        printf("validation: using %d of %d held-out positions (raise --val-positions for less noise)\n",
               nValUse, nVal);

    int used = 0;

    /* Hold out a fixed slice of the shuffled pool for the baseline and final
       numbers.  Two things are wrong without it: the two losses are measured
       over different position sets, so "final loss was X, baseline was Y"
       compares two samples of the sampling noise; and the positions used are
       ones the gradient also trains on, so an improvement is partly just
       overfitting.  The slice is taken from the tail of the pool and the
       gradient only ever draws from the head, so the two sets are disjoint and
       both are fixed for the whole run. */
    int nEval = opt.positions;
    if (nEval > nTrain) nEval = nTrain;
    if (nEval < 1) nEval = nTrain < 1 ? 0 : 1;
    for (int i = 0; i < nEval; i++) evalIdx[i] = pool[nTrain - nEval + i];

    /* Actually hold them out.  Merely slicing the pool is not enough: the
       gradient reshuffles `pool` every step, so unless the held-out indices
       are physically removed from it, a later shuffle can hand them to the
       gradient and the "held-out" number becomes a training number. */
    unsigned char *isEval = calloc((size_t)nTrain, 1);
    if (!isEval) { fprintf(stderr, "out of memory\n"); return 1; }
    for (int i = 0; i < nEval; i++) isEval[evalIdx[i]] = 1;
    int kept = 0;
    for (int i = 0; i < nTrain; i++)
        if (!isEval[pool[i]]) pool[kept++] = pool[i];
    free(isEval);
    if (kept < 1) { fprintf(stderr, "dataset too small to split\n"); return 1; }
    if (opt.positions > kept) opt.positions = kept;

    double t0 = nowSecs();
    double baseLoss = evalLoss(train, nEval, evalIdx, &used);
    printf("\nbaseline: loss %.8f over %d held-out positions (%.1fs)\n",
           baseLoss, used, nowSecs() - t0);

    /* Baseline validation too.  `best` below is a validation score, so without
       this number there is nothing to compare the best checkpoint against and
       no way to tell a run that helped from one that merely wandered. */
    double baseVal = 1e9;
    if (nVal > 0) {
        baseVal = evalLoss(val, nValUse, valIdx, &used);
        printf("baseline: val  %.8f over %d validation positions\n", baseVal, nValUse);
    }

    for (int i = 0; i < tuneNumEntries; i++) saved[i] = *tuneTable[i].slot;

    double best = (nVal > 0) ? 1e9 : baseLoss;
    int warnedNoMove = 0;

    for (int step = startStep; step < opt.steps; step++) {
        /* Draw this step's sample once and reuse it for every loss evaluation
           in the step, so the difference being differentiated reflects the
           parameters and not which positions were drawn. */
        shuffle(pool, nTrain);
        memcpy(sample, pool, sizeof(int) * (size_t)opt.positions);

        for (int i = 0; i < tuneNumEntries; i++) { g[i] = 0.0; saved[i] = *tuneTable[i].slot; }

        if (opt.method == METH_SPSA) {
            /* Pick a perturbation direction that keeps BOTH probes a full step
               away from the current value.  tuneSetByName() clamps to [lo,hi],
               so a probe that would leave the box lands back on the
               unperturbed point -- the difference stops being central and the
               gradient comes out with roughly half the magnitude, every step,
               for any parameter sitting on a bound.  A parameter whose box
               cannot hold both probes is skipped instead: one noisy
               contribution is worse than none, since Adam normalises the
               magnitude away and would still act on it.
               The sign stays random.  It has to be: the estimate
               (L(x+d) - L(x-d)) * d_k is only an unbiased estimate of g_k when
               the signs of the *other* parameters average out, which is the
               whole reason SPSA works in 2 evaluations instead of 2*|params|.
               Biasing every interior parameter to +1 would make the cross
               terms add up instead of cancel, and the search would walk
               straight into a corner of the box.  So: draw a sign, and flip
               it only when the box does not allow that direction. */
            int nProbe = 0;
            for (int n = 0; n < nActive; n++) {
                int i = activeParams[n];
                int v = saved[i], h = tuneTable[i].step < 1 ? 1 : tuneTable[i].step;
                if (v + h > tuneTable[i].hi && v - h < tuneTable[i].lo) continue;

                delta[i] = (rnd64() & 1ULL) ? 1 : -1;
                if (v + delta[i] * h > tuneTable[i].hi) delta[i] = -1;
                if (v + delta[i] * h < tuneTable[i].lo) delta[i] =  1;
                probe[nProbe++] = i;
            }

            for (int k = 0; k < nProbe; k++) {
                int i = probe[k], h = tuneTable[i].step < 1 ? 1 : tuneTable[i].step;
                *tuneTable[i].slot = saved[i] + delta[i] * h;
            }
            tuneClampAndRebuild();
            double lPlus = evalLoss(train, opt.positions, sample, &used);

            for (int k = 0; k < nProbe; k++) {
                int i = probe[k], h = tuneTable[i].step < 1 ? 1 : tuneTable[i].step;
                *tuneTable[i].slot = saved[i] - delta[i] * h;
            }
            tuneClampAndRebuild();
            double lMinus = evalLoss(train, opt.positions, sample, &used);

            for (int k = 0; k < nProbe; k++) {
                int i = probe[k];
                double h = (double)(tuneTable[i].step < 1 ? 1 : tuneTable[i].step);
                g[i] = (lPlus - lMinus) * (double)delta[i] / (2.0 * h);
            }

            /* back to the unperturbed point before stepping */
            for (int i = 0; i < tuneNumEntries; i++) *tuneTable[i].slot = saved[i];
            tuneClampAndRebuild();

        } else {
            int nTake = (opt.method == METH_FDALL || opt.fdBatch <= 0) ? nActive : opt.fdBatch;
            if (nTake > nActive) nTake = nActive;
            for (int i = 0; i < nActive; i++) subset[i] = activeParams[i];
            for (int i = 0; i < nTake; i++) {          /* partial Fisher-Yates */
                int j = i + (int)(rnd64() % (uint64_t)(nActive - i));
                int t = subset[i]; subset[i] = subset[j]; subset[j] = t;
            }

            for (int s = 0; s < nTake; s++) {
                int i = subset[s];
                int h = tuneTable[i].step < 1 ? 1 : tuneTable[i].step;
                int v = saved[i];

                /* Both probes are placed relative to the *unperturbed* value.
                   Deriving the second one from the already-moved value (the
                   obvious `*slot - 2*h`) re-centres the difference whenever the
                   first probe was clamped, which biases the gradient by half a
                   step in the direction that was already clamped. */
                if (v + h > tuneTable[i].hi || v - h < tuneTable[i].lo) continue;

                *tuneTable[i].slot = v + h;
                tuneClampAndRebuild();
                double lPlus  = evalLoss(train, opt.positions, sample, &used);

                *tuneTable[i].slot = v - h;
                tuneClampAndRebuild();
                double lMinus = evalLoss(train, opt.positions, sample, &used);

                *tuneTable[i].slot = v;
                tuneClampAndRebuild();

                g[i] = (lPlus - lMinus) / (2.0 * (double)h);
            }
        }

        applyGradient(g);
        double loss = evalLoss(train, opt.positions, sample, &used);

        if ((step + 1) % opt.evalEvery == 0 || step + 1 == opt.steps) {
            double v = (nVal > 0) ? evalLoss(val, nValUse, valIdx, &used) : 1e9;
            /* The held-out slice is reported alongside val on purpose.  `best`
               below is a running argmin over val, so the val number is
               optimistically biased by however many checkpoints it has seen --
               a val that falls while `held` sits still or rises is the
               optimiser fitting the validation noise, not the parameters. */
            double h = evalLoss(train, nEval, evalIdx, &used);
            char vbuf[32], hbuf[32];
            if (nVal > 0) snprintf(vbuf, sizeof(vbuf), "%.8f", v);
            else          snprintf(vbuf, sizeof(vbuf), "%8s", "n/a");
            snprintf(hbuf, sizeof(hbuf), "%.8f", h);

            int pinned = 0, moved = 0;
            for (int n = 0; n < nActive; n++) {
                int i = activeParams[n];
                if (atBound(i)) pinned++;
                if (*tuneTable[i].slot != tuneTable[i].def) moved++;
            }

            long long searched = totalSearches();
            /* `train` is the loss on the sample this step's gradient was taken
               from, so it is a training loss and is NOT comparable between
               steps (each step draws fresh positions).  `val` is the only
               column that means anything across steps. */
            printf("step %4d  train %.8f  val %s  held %s  lr %.4f  %6.0fs  %lld.%03lldM searches%s%s\n",
                   step + 1, loss, vbuf, hbuf, opt.lr, nowSecs() - t0,
                   searched / 1000000, (searched % 1000000) / 1000,
                   moved ? "" : "  (NOTHING MOVED)",
                   pinned ? "  (params on a bound)" : "");

            if (!moved && !warnedNoMove) {
                warnedNoMove = 1;
                printf("warning: after %d steps not one parameter has left its default.\n"
                       "         The update is lr * (that parameter's own step) per step, so\n"
                       "         this is either an --lr far below 0.5 or a --nodes budget too\n"
                       "         small for any of these constants to change the search.\n",
                       step + 1);
            }

            double score = (nVal > 0) ? v : loss;
            if (score < best) { best = score; writeCheckpoint(step + 1, score); }

            if (opt.verbose) printTunedTable();
        }
    }

    /* Prefer the best-scoring checkpoint over wherever the last step landed.
       Without a validation split the "best" tracked during the run is itself a
       training loss, so the checkpoint is the only thing worth keeping; with
       one, it is the whole point of having it. */
    if (access(opt.ckptPath, F_OK) == 0) readCheckpoint();

    long long searched = totalSearches();
    printf("\ndone in %.0fs, %lld.%03lldM searches\n", nowSecs() - t0,
           searched / 1000000, (searched % 1000000) / 1000);

    double finalLoss = evalLoss(train, nEval, evalIdx, &used);
    double finalVal  = (nVal > 0) ? evalLoss(val, nValUse, valIdx, &used) : 1e9;
    if (nVal > 0) {
        printf("final:   held %.8f (%+.2f%% vs baseline %.8f)\n",
               finalLoss, 100.0 * (finalLoss - baseLoss) / baseLoss, baseLoss);
        printf("         val  %.8f (%+.2f%% vs baseline %.8f, selected on)\n",
               finalVal, 100.0 * (finalVal - baseVal) / baseVal, baseVal);
        /* The verdict is deliberately based on `held`, not `val`.  The returned
           parameters are the best-val checkpoint, and the best of N checkpoints
           is the maximum of N noisy estimates, so val flatters the result by
           roughly the spread of the noise; held was never selected on and is
           the number to believe. */
        if (finalLoss >= baseLoss)
            printf("         held-out is no better than the defaults, so this run did not\n"
                   "         find a genuine improvement -- the val gain is the checkpoint\n"
                   "         selection fitting noise.  More --steps / --positions, a larger\n"
                   "         --nodes budget so the constants bite, or --mode texel.\n");
    } else {
        printf("final:   held %.8f (%+.2f%% vs baseline %.8f)\n",
               finalLoss, 100.0 * (finalLoss - baseLoss) / baseLoss, baseLoss);
    }

    printTunedTable();
    writeOutput(opt.outPath, finalLoss, finalVal);

    free(pool); free(sample); free(valIdx);
    free(g); free(delta); free(subset); free(saved); free(probe); free(evalIdx);
    free(gradM); free(gradV); free(updAcc);
    free(paramEnabled); free(activeParams);
    freeWorkers();
    free(train); free(val);
    return 0;
}
