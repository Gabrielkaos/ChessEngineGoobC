/*
 * nnue_loader.h / nnue_loader.c
 * ==============================
 * Lightweight NNUE inference for GOOB.
 * Reads the float32 binary exported by train.py and evaluates positions.
 *
 * Architecture:
 *   Input  : 2 x HalfKP (41024 features each)
 *   L1     : 2 x Accumulator -> 256 neurons (clipped ReLU)
 *   L2     : 32  neurons (clipped ReLU)
 *   L3     : 32  neurons (clipped ReLU)
 *   Output : 1   scalar (centipawns, White's POV)
 *
 * Integration with GOOB:
 *   1. Add nnue_loader.c to your Makefile's SRCS list.
 *   2. Call nnue_init("path/to/nnue.bin") once at startup (e.g. in main.c).
 *   3. In evaluate.c, replace / augment EvalPosition with nnue_eval(pos).
 *
 * Weight layout in binary file (all float32, little-endian):
 *   acc_white.weight [256 x 41024]
 *   acc_white.bias   [256]
 *   acc_black.weight [256 x 41024]
 *   acc_black.bias   [256]
 *   fc1.weight       [32 x 512]
 *   fc1.bias         [32]
 *   fc2.weight       [32 x 32]
 *   fc2.bias         [32]
 *   fc3.weight       [1  x 32]
 *   fc3.bias         [1]
 */

#ifndef NNUE_LOADER_H
#define NNUE_LOADER_H

#include "defs.h"
#include "board.h"

/* ── Dimensions (must match nnue_model.py) ──────────────────────────────── */
#define NNUE_HALFKP   41024
#define NNUE_L1       256
#define NNUE_L2       32
#define NNUE_L3       32
#define NNUE_SCALE    400
#define NNUE_NUM_PT   10    /* piece types (no kings): wP wN wB wR wQ bP bN bB bR bQ */

/* ── Weight storage ─────────────────────────────────────────────────────── */
typedef struct {
    float acc_w_weight[NNUE_L1][NNUE_HALFKP];
    float acc_w_bias  [NNUE_L1];
    float acc_b_weight[NNUE_L1][NNUE_HALFKP];
    float acc_b_bias  [NNUE_L1];
    float fc1_weight  [NNUE_L2][NNUE_L1 * 2];
    float fc1_bias    [NNUE_L2];
    float fc2_weight  [NNUE_L3][NNUE_L2];
    float fc2_bias    [NNUE_L3];
    float fc3_weight  [1][NNUE_L3];
    float fc3_bias    [1];
} NNUE_WEIGHTS;

/* ── Public API ─────────────────────────────────────────────────────────── */

/* Load weights from binary file.  Returns 1 on success, 0 on failure. */
int  nnue_init(const char *path);

/* Evaluate board position.  Returns centipawns from White's POV.
   Requires nnue_init() to have been called first. */
int  nnue_eval(const S_BOARD *pos);

/* True after successful nnue_init */
extern int nnue_loaded;

#endif /* NNUE_LOADER_H */


/* ═══════════════════════════════════════════════════════════════════════════
 * IMPLEMENTATION  –  define NNUE_IMPLEMENTATION in exactly one .c file
 *                    (or just compile nnue_loader.c separately).
 * ═══════════════════════════════════════════════════════════════════════════ */
#ifdef NNUE_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── GOOB piece -> HalfKP piece-type index ──────────────────────────────── */
/* GOOB enum: EMPTY=0 wP=1 wN=2 wB=3 wR=4 wQ=5 wK=6 bP=7 bN=8 bB=9 bR=10 bQ=11 bK=12 */
static const int PIECE_TO_PT[13] = {
    -1,   /* EMPTY */
     0,   /* wP */
     1,   /* wN */
     2,   /* wB */
     3,   /* wR */
     4,   /* wQ */
    -1,   /* wK  – not a HalfKP feature */
     5,   /* bP */
     6,   /* bN */
     7,   /* bB */
     8,   /* bR */
     9,   /* bQ */
    -1,   /* bK  – not a HalfKP feature */
};

/* ── Globals ────────────────────────────────────────────────────────────── */
static NNUE_WEIGHTS *g_weights = NULL;
int nnue_loaded = 0;

/* ── Helpers ────────────────────────────────────────────────────────────── */

static inline float clamp01(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static inline int halfkp_index(int king_sq, int piece_sq, int pt) {
    return king_sq * 64 * NNUE_NUM_PT + piece_sq * NNUE_NUM_PT + pt;
}

/* ── Init ───────────────────────────────────────────────────────────────── */
int nnue_init(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[NNUE] Cannot open weight file: %s\n", path);
        return 0;
    }

    if (!g_weights) {
        g_weights = (NNUE_WEIGHTS *)malloc(sizeof(NNUE_WEIGHTS));
        if (!g_weights) { fclose(f); return 0; }
    }

    /* Read in same order as export_weights_bin() in nnue_model.py */
    size_t ok = 1;
    ok &= fread(g_weights->acc_w_weight, sizeof(float), NNUE_L1 * NNUE_HALFKP,    f) == (size_t)(NNUE_L1 * NNUE_HALFKP);
    ok &= fread(g_weights->acc_w_bias,   sizeof(float), NNUE_L1,                  f) == (size_t)NNUE_L1;
    ok &= fread(g_weights->acc_b_weight, sizeof(float), NNUE_L1 * NNUE_HALFKP,    f) == (size_t)(NNUE_L1 * NNUE_HALFKP);
    ok &= fread(g_weights->acc_b_bias,   sizeof(float), NNUE_L1,                  f) == (size_t)NNUE_L1;
    ok &= fread(g_weights->fc1_weight,   sizeof(float), NNUE_L2 * NNUE_L1 * 2,   f) == (size_t)(NNUE_L2 * NNUE_L1 * 2);
    ok &= fread(g_weights->fc1_bias,     sizeof(float), NNUE_L2,                  f) == (size_t)NNUE_L2;
    ok &= fread(g_weights->fc2_weight,   sizeof(float), NNUE_L3 * NNUE_L2,        f) == (size_t)(NNUE_L3 * NNUE_L2);
    ok &= fread(g_weights->fc2_bias,     sizeof(float), NNUE_L3,                  f) == (size_t)NNUE_L3;
    ok &= fread(g_weights->fc3_weight,   sizeof(float), NNUE_L3,                  f) == (size_t)NNUE_L3;
    ok &= fread(g_weights->fc3_bias,     sizeof(float), 1,                        f) == (size_t)1;

    fclose(f);

    if (!ok) {
        fprintf(stderr, "[NNUE] Weight file truncated or corrupt: %s\n", path);
        free(g_weights); g_weights = NULL;
        return 0;
    }

    nnue_loaded = 1;
    printf("[NNUE] Weights loaded from %s\n", path);
    return 1;
}

/* ── Eval ───────────────────────────────────────────────────────────────── */
int nnue_eval(const S_BOARD *pos) {
    if (!nnue_loaded) return 0;

    /* Find king squares */
    int wksq = -1, bksq = -1;
    for (int sq = 0; sq < 64; sq++) {
        int p = pos->pieces[sq];   /* GOOB uses pos->pieces[64] */
        if (p == 6)  wksq = sq;
        if (p == 12) bksq = sq;
    }
    if (wksq < 0 || bksq < 0) return 0;

    int mirror_bksq = bksq ^ 56;

    /* ── Build accumulators ─────────────────────────────────────────────── */
    float acc_w[NNUE_L1], acc_b[NNUE_L1];

    /* Start from bias */
    for (int i = 0; i < NNUE_L1; i++) {
        acc_w[i] = g_weights->acc_w_bias[i];
        acc_b[i] = g_weights->acc_b_bias[i];
    }

    /* Add active features */
    for (int sq = 0; sq < 64; sq++) {
        int p = pos->pieces[sq];
        int pt = PIECE_TO_PT[p];
        if (pt < 0) continue;   /* empty, king */

        /* White perspective */
        int wi = halfkp_index(wksq, sq, pt);
        for (int n = 0; n < NNUE_L1; n++)
            acc_w[n] += g_weights->acc_w_weight[n][wi];

        /* Black perspective (mirror square + flip piece colour) */
        int flipped_pt = pt < 5 ? pt + 5 : pt - 5;
        int bi = halfkp_index(mirror_bksq, sq ^ 56, flipped_pt);
        for (int n = 0; n < NNUE_L1; n++)
            acc_b[n] += g_weights->acc_b_weight[n][bi];
    }

    /* Clipped ReLU */
    for (int i = 0; i < NNUE_L1; i++) {
        acc_w[i] = clamp01(acc_w[i]);
        acc_b[i] = clamp01(acc_b[i]);
    }

    /* Concatenate: side-to-move first */
    float concat[NNUE_L1 * 2];
    if (pos->side == WHITE) {
        memcpy(concat,           acc_w, NNUE_L1 * sizeof(float));
        memcpy(concat + NNUE_L1, acc_b, NNUE_L1 * sizeof(float));
    } else {
        memcpy(concat,           acc_b, NNUE_L1 * sizeof(float));
        memcpy(concat + NNUE_L1, acc_w, NNUE_L1 * sizeof(float));
    }

    /* ── FC1 ────────────────────────────────────────────────────────────── */
    float l1[NNUE_L2];
    for (int o = 0; o < NNUE_L2; o++) {
        float s = g_weights->fc1_bias[o];
        for (int i = 0; i < NNUE_L1 * 2; i++)
            s += g_weights->fc1_weight[o][i] * concat[i];
        l1[o] = clamp01(s);
    }

    /* ── FC2 ────────────────────────────────────────────────────────────── */
    float l2[NNUE_L3];
    for (int o = 0; o < NNUE_L3; o++) {
        float s = g_weights->fc2_bias[o];
        for (int i = 0; i < NNUE_L2; i++)
            s += g_weights->fc2_weight[o][i] * l1[i];
        l2[o] = clamp01(s);
    }

    /* ── FC3 (output) ───────────────────────────────────────────────────── */
    float score = g_weights->fc3_bias[0];
    for (int i = 0; i < NNUE_L3; i++)
        score += g_weights->fc3_weight[0][i] * l2[i];

    score *= NNUE_SCALE;

    /* Return from side-to-move's POV (GOOB convention) */
    int cp = (int)score;
    return pos->side == WHITE ? cp : -cp;
}

#endif /* NNUE_IMPLEMENTATION */