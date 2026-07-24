/*
 * nnue_loader.h / nnue_loader.c
 * ==============================
 * Lightweight NNUE inference for GOOB.
 * Reads the float32 binary exported by export_weights.py and evaluates
 * positions.
 *
 * Architecture (single perspective, side-to-move relative):
 *   Input  : 768 (12 piece planes x 64 squares)
 *   L1     : 256 neurons (clipped ReLU)
 *   L2     : 32  neurons (clipped ReLU)
 *   L3     : 32  neurons (clipped ReLU)
 *   Output : 1   scalar logit; multiplied by cp_scale to get centipawns,
 *                from the side-to-move's POV
 *
 * Every position is re-expressed as "features from the side-to-move's
 * point of view": if Black is to move, the board is mirrored vertically
 * (square ^ 56) and piece colours are swapped, so the net only ever sees
 * one consistent orientation. This MUST match nnue_dataset.py exactly.
 *
 * Weight file layout (all little-endian):
 *   char[4]  magic = "NNUE"
 *   int32    version
 *   int32    input_size (768)
 *   int32    l1_size (256)
 *   int32    l2_size (32)
 *   int32    l3_size (32)
 *   float32  cp_scale
 *   float32[l1_size * input_size]  fc1.weight
 *   float32[l1_size]               fc1.bias
 *   float32[l2_size * l1_size]     fc2.weight
 *   float32[l2_size]               fc2.bias
 *   float32[l3_size * l2_size]     fc3.weight
 *   float32[l3_size]               fc3.bias
 *   float32[1 * l3_size]           fc4.weight
 *   float32[1]                     fc4.bias
 */

#ifndef NNUE_LOADER_H
#define NNUE_LOADER_H

#include "defs.h"
#include "board.h"

/* ── Public API ─────────────────────────────────────────────────────────── */

/* Load weights from binary file. Returns 1 on success, 0 on failure. */
int nnue_init(const char *path);

/* Evaluate board position. Returns centipawns from the SIDE-TO-MOVE's
   POV (matches how EvalPosition() in evaluate.c uses the result).
   Requires nnue_init() to have been called first. */
int nnue_eval(const S_BOARD *pos);

/* True after successful nnue_init */
extern int nnue_loaded;

#endif /* NNUE_LOADER_H */


/* ═══════════════════════════════════════════════════════════════════════════
 * IMPLEMENTATION  –  define NNUE_IMPLEMENTATION in exactly one .c file
 *                    (nnue_loader.c already does this).
 * ═══════════════════════════════════════════════════════════════════════════ */
#ifdef NNUE_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    int input_size;
    int l1_size;
    int l2_size;
    int l3_size;
    float cp_scale;

    float *fc1_w; /* [l1_size][input_size] */
    float *fc1_b; /* [l1_size] */
    float *fc2_w; /* [l2_size][l1_size] */
    float *fc2_b; /* [l2_size] */
    float *fc3_w; /* [l3_size][l2_size] */
    float *fc3_b; /* [l3_size] */
    float *fc4_w; /* [1][l3_size] */
    float *fc4_b; /* [1] */
} NNUE_WEIGHTS;

/* ── Globals ────────────────────────────────────────────────────────────── */
static NNUE_WEIGHTS *g_weights = NULL;
int nnue_loaded = 0;

/* ── Helpers ────────────────────────────────────────────────────────────── */
static inline float clamp01(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static float *read_floats(FILE *f, int count, int *ok) {
    float *buf = (float *)malloc(sizeof(float) * (size_t)count);
    if (!buf) { *ok = 0; return NULL; }
    if (fread(buf, sizeof(float), (size_t)count, f) != (size_t)count) {
        *ok = 0;
        free(buf);
        return NULL;
    }
    return buf;
}

static void nnue_free_weights(void) {
    if (!g_weights) return;
    free(g_weights->fc1_w); free(g_weights->fc1_b);
    free(g_weights->fc2_w); free(g_weights->fc2_b);
    free(g_weights->fc3_w); free(g_weights->fc3_b);
    free(g_weights->fc4_w); free(g_weights->fc4_b);
    free(g_weights);
    g_weights = NULL;
}

/* ── Init ───────────────────────────────────────────────────────────────── */
int nnue_init(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[NNUE] Cannot open weight file: %s\n", path);
        return 0;
    }

    char magic[4];
    int32_t version, input_size, l1, l2, l3;
    float cp_scale;

    int ok = 1;
    ok &= fread(magic, 1, 4, f) == 4 && memcmp(magic, "NNUE", 4) == 0;
    ok &= fread(&version, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&input_size, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&l1, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&l2, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&l3, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&cp_scale, sizeof(float), 1, f) == 1;

    if (!ok) {
        fprintf(stderr, "[NNUE] Bad header in weight file: %s\n", path);
        fclose(f);
        return 0;
    }

    nnue_free_weights(); /* drop any previously loaded net */
    g_weights = (NNUE_WEIGHTS *)calloc(1, sizeof(NNUE_WEIGHTS));
    if (!g_weights) { fclose(f); return 0; }

    g_weights->input_size = input_size;
    g_weights->l1_size = l1;
    g_weights->l2_size = l2;
    g_weights->l3_size = l3;
    g_weights->cp_scale = cp_scale;

    g_weights->fc1_w = read_floats(f, l1 * input_size, &ok);
    g_weights->fc1_b = read_floats(f, l1, &ok);
    g_weights->fc2_w = read_floats(f, l2 * l1, &ok);
    g_weights->fc2_b = read_floats(f, l2, &ok);
    g_weights->fc3_w = read_floats(f, l3 * l2, &ok);
    g_weights->fc3_b = read_floats(f, l3, &ok);
    g_weights->fc4_w = read_floats(f, 1 * l3, &ok);
    g_weights->fc4_b = read_floats(f, 1, &ok);

    fclose(f);

    if (!ok) {
        fprintf(stderr, "[NNUE] Weight file truncated or corrupt: %s\n", path);
        nnue_free_weights();
        nnue_loaded = 0;
        return 0;
    }

    nnue_loaded = 1;
    printf("[NNUE] Weights loaded from %s (in=%d l1=%d l2=%d l3=%d scale=%.1f)\n",
           path, input_size, l1, l2, l3, cp_scale);
    return 1;
}

/* out[i] = clipped_relu( bias[i] + sum_j in[j] * w[i*in_size + j] ) */
static void linear_clipped(const float *in, int in_size,
                            const float *w, const float *b, int out_size,
                            float *out) {
    for (int i = 0; i < out_size; i++) {
        float acc = b[i];
        const float *row = w + (size_t)i * in_size;
        for (int j = 0; j < in_size; j++) {
            acc += in[j] * row[j];
        }
        out[i] = clamp01(acc);
    }
}

/* ── Eval ───────────────────────────────────────────────────────────────── */
int nnue_eval(const S_BOARD *pos) {
    if (!nnue_loaded) return 0;

    /* GOOB piece codes (defs.h): EMPTY=0, wP..wK=1..6, bP..bK=7..12 */
    /* GOOB squares: A1=0 ... H8=63, same layout as our feature encoding */
    /* GOOB side: WHITE=0, BLACK=1 */

    float features[768];
    memset(features, 0, sizeof(features));

    int side_to_move = pos->side; /* 0 = white, 1 = black */

    for (int sq = 0; sq < 64; sq++) {
        int piece;
        int feat_sq = sq;

        if (side_to_move == BLACK) {
            int mirrored_sq = sq ^ 56;
            int p = pos->pieces[mirrored_sq];
            if (p == EMPTY) continue;
            /* swap colour: white piece (1..6) -> black slot (+6), and
               vice versa, exactly like nnue_dataset.py's mirroring */
            piece = (p <= 6) ? (p + 6) : (p - 6);
        } else {
            int p = pos->pieces[sq];
            if (p == EMPTY) continue;
            piece = p;
        }

        int plane = piece - 1; /* 0..11 */
        features[plane * 64 + feat_sq] = 1.0f;
    }

    if (g_weights->l1_size > 256 || g_weights->l2_size > 64 || g_weights->l3_size > 64) {
        fprintf(stderr, "[NNUE] Net dimensions too large for fixed buffers, skipping eval\n");
        return 0;
    }

    float h1[256], h2[64], h3[64];
    linear_clipped(features, g_weights->input_size, g_weights->fc1_w, g_weights->fc1_b, g_weights->l1_size, h1);
    linear_clipped(h1, g_weights->l1_size, g_weights->fc2_w, g_weights->fc2_b, g_weights->l2_size, h2);
    linear_clipped(h2, g_weights->l2_size, g_weights->fc3_w, g_weights->fc3_b, g_weights->l3_size, h3);

    float out = g_weights->fc4_b[0];
    for (int j = 0; j < g_weights->l3_size; j++) {
        out += h3[j] * g_weights->fc4_w[j];
    }

    /* Already side-to-move relative (we mirrored the board above), so no
       extra sign flip is needed here — matches how EvalPosition() uses it. */
    return (int)(out * g_weights->cp_scale);
}

#endif /* NNUE_IMPLEMENTATION */