/*
 * nnue_loader.h / nnue_loader.c
 * ==============================
 * NNUE inference for GOOB, with an INCREMENTAL accumulator.
 *
 * Architecture (absolute/White-fixed single perspective):
 *   Input  : 768 (12 piece planes x 64 squares, always White's frame of
 *            reference — no mirroring by side to move)
 *   L1     : 256 neurons, QUANTIZED (int16 weights, int32 accumulator),
 *            clipped ReLU
 *   L2     : 32  neurons (float, clipped ReLU)
 *   L3     : 32  neurons (float, clipped ReLU)
 *   Output : 1   scalar logit (float); multiplied by cp_scale to get
 *                centipawns from WHITE's POV, then sign-flipped to
 *                side-to-move's POV to match how EvalPosition() uses it.
 *
 * WHY incremental: pos->nnue_acc (see board.h) holds fc1's output for
 * the CURRENT position at all times. Instead of rebuilding it from all
 * 64 squares on every nnue_eval() call, it is kept in sync by three
 * small hooks placed in makemove.c's ClearPiece/AddPiece/MovePiece —
 * the exact same primitives that already incrementally maintain
 * pos->psqtmat. A full rebuild (nnue_refresh_accumulator) is only
 * needed when a position is set from scratch (ParseFEN/MirrorBoard,
 * wired via updateListMaterial in board.c) or when new weights are
 * loaded (wired in uci.c's EvalFile handler).
 *
 * WHY absolute (not mirrored by side to move): mirroring flips the
 * ENTIRE input every ply, which makes incremental updates impossible
 * (there's no small delta between "White to move" features and "Black
 * to move" features — every one of the 768 inputs changes meaning).
 * Absolute features never change meaning, so a piece moving only ever
 * touches the 2 (or 3, for captures) feature columns for that piece.
 *
 * WHY only fc1 is quantized: it is the layer the accumulator touches,
 * so quantizing it shrinks its memory footprint 2x and makes the
 * incremental add/subtract simple integer ops. fc2/fc3/fc4 are tiny
 * (~9k multiply-adds total) and are recomputed fully every call either
 * way, so quantizing them too would add real complexity for very
 * little extra speed — not done here.
 *
 * Weight file layout v2 (all little-endian):
 *   char[4]  magic = "NNUE"
 *   int32    version = 2
 *   int32    input_size (768)
 *   int32    l1_size (256, MUST equal NNUE_ACC_SIZE in board.h)
 *   int32    l2_size (32)
 *   int32    l3_size (32)
 *   float32  cp_scale
 *   int32    qa_scale             (fc1 fixed-point scale, e.g. 64)
 *   int16[l1_size * input_size]   fc1.weight (quantized)
 *   int32[l1_size]                fc1.bias   (quantized)
 *   float32[l2_size * l1_size]    fc2.weight
 *   float32[l2_size]              fc2.bias
 *   float32[l3_size * l2_size]    fc3.weight
 *   float32[l3_size]              fc3.bias
 *   float32[1 * l3_size]          fc4.weight
 *   float32[1]                    fc4.bias
 */

#ifndef NNUE_LOADER_H
#define NNUE_LOADER_H

#include "defs.h"
#include "board.h"

/* ── Public API ─────────────────────────────────────────────────────────── */

/* Load weights from binary file. Returns 1 on success, 0 on failure.
 * Does NOT touch any position's accumulator — call
 * nnue_refresh_accumulator() afterward for whatever position is
 * currently set up (uci.c already does this). */
int nnue_init(const char *path);

/* Full rebuild of pos->nnue_acc from pos->pieces, from scratch. Call
 * whenever a position is set up from raw FEN/mirrored data rather than
 * via incremental makeMove/takeMove (board.c already does this from
 * updateListMaterial), or right after loading new weights. */
void nnue_refresh_accumulator(S_BOARD *pos);

/* Incremental accumulator maintenance — called from makemove.c's
 * ClearPiece/AddPiece/MovePiece. No-ops if !nnue_loaded or piece==EMPTY. */
void nnue_update_add(S_BOARD *pos, int piece, int sq);
void nnue_update_remove(S_BOARD *pos, int piece, int sq);
void nnue_update_move(S_BOARD *pos, int piece, int from, int to);

/* Evaluate board position using the CURRENT accumulator (must already
 * be in sync — see above). Returns centipawns from the SIDE-TO-MOVE's
 * POV (matches how EvalPosition() in evaluate.c uses the result).
 * Requires nnue_init() to have been called first. */
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
    int qa_scale;

    int16_t *fc1_w; /* [l1_size][input_size], quantized */
    int32_t *fc1_b; /* [l1_size], quantized */
    float *fc2_w;   /* [l2_size][l1_size] */
    float *fc2_b;   /* [l2_size] */
    float *fc3_w;   /* [l3_size][l2_size] */
    float *fc3_b;   /* [l3_size] */
    float *fc4_w;   /* [1][l3_size] */
    float *fc4_b;   /* [1] */
} NNUE_WEIGHTS;

/* ── Globals ────────────────────────────────────────────────────────────── */
static NNUE_WEIGHTS *g_weights = NULL;
int nnue_loaded = 0;

/* ── Helpers ────────────────────────────────────────────────────────────── */
static inline float clamp01f(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

static void *read_bytes(FILE *f, size_t nbytes, int *ok) {
    void *buf = malloc(nbytes);
    if (!buf) { *ok = 0; return NULL; }
    if (fread(buf, 1, nbytes, f) != nbytes) {
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
    int32_t version, input_size, l1, l2, l3, qa_scale;
    float cp_scale;

    int ok = 1;
    ok &= fread(magic, 1, 4, f) == 4 && memcmp(magic, "NNUE", 4) == 0;
    ok &= fread(&version, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&input_size, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&l1, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&l2, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&l3, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&cp_scale, sizeof(float), 1, f) == 1;
    ok &= fread(&qa_scale, sizeof(int32_t), 1, f) == 1;

    if (!ok || version != 2) {
        fprintf(stderr, "[NNUE] Bad header or unsupported version in: %s "
                         "(expected version 2 — retrain/re-export with the "
                         "current export_weights.py)\n", path);
        fclose(f);
        return 0;
    }

    if (l1 != NNUE_ACC_SIZE) {
        fprintf(stderr, "[NNUE] l1_size (%d) does not match NNUE_ACC_SIZE (%d) "
                         "in board.h — update board.h and rebuild.\n",
                l1, NNUE_ACC_SIZE);
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
    g_weights->qa_scale = qa_scale;

    g_weights->fc1_w = (int16_t *)read_bytes(f, sizeof(int16_t) * (size_t)l1 * input_size, &ok);
    g_weights->fc1_b = (int32_t *)read_bytes(f, sizeof(int32_t) * (size_t)l1, &ok);
    g_weights->fc2_w = (float *)read_bytes(f, sizeof(float) * (size_t)l2 * l1, &ok);
    g_weights->fc2_b = (float *)read_bytes(f, sizeof(float) * (size_t)l2, &ok);
    g_weights->fc3_w = (float *)read_bytes(f, sizeof(float) * (size_t)l3 * l2, &ok);
    g_weights->fc3_b = (float *)read_bytes(f, sizeof(float) * (size_t)l3, &ok);
    g_weights->fc4_w = (float *)read_bytes(f, sizeof(float) * (size_t)l3, &ok);
    g_weights->fc4_b = (float *)read_bytes(f, sizeof(float), &ok);

    fclose(f);

    if (!ok) {
        fprintf(stderr, "[NNUE] Weight file truncated or corrupt: %s\n", path);
        nnue_free_weights();
        nnue_loaded = 0;
        return 0;
    }

    nnue_loaded = 1;
    printf("[NNUE] v2 weights loaded from %s (in=%d l1=%d l2=%d l3=%d "
           "scale=%.1f qa=%d)\n", path, input_size, l1, l2, l3, cp_scale, qa_scale);
    return 1;
}

/* ── Incremental accumulator maintenance ───────────────────────────────── */

void nnue_update_add(S_BOARD *pos, int piece, int sq) {
    if (!nnue_loaded || piece == EMPTY) return;
    int col = (piece - 1) * 64 + sq;
    const int16_t *w = g_weights->fc1_w;
    int in_size = g_weights->input_size;
    for (int i = 0; i < g_weights->l1_size; i++) {
        pos->nnue_acc[i] += (int32_t)w[(size_t)i * in_size + col];
    }
}

void nnue_update_remove(S_BOARD *pos, int piece, int sq) {
    if (!nnue_loaded || piece == EMPTY) return;
    int col = (piece - 1) * 64 + sq;
    const int16_t *w = g_weights->fc1_w;
    int in_size = g_weights->input_size;
    for (int i = 0; i < g_weights->l1_size; i++) {
        pos->nnue_acc[i] -= (int32_t)w[(size_t)i * in_size + col];
    }
}

void nnue_update_move(S_BOARD *pos, int piece, int from, int to) {
    if (!nnue_loaded || piece == EMPTY) return;
    int col_from = (piece - 1) * 64 + from;
    int col_to   = (piece - 1) * 64 + to;
    const int16_t *w = g_weights->fc1_w;
    int in_size = g_weights->input_size;
    for (int i = 0; i < g_weights->l1_size; i++) {
        pos->nnue_acc[i] += (int32_t)w[(size_t)i * in_size + col_to]
                           - (int32_t)w[(size_t)i * in_size + col_from];
    }
}

void nnue_refresh_accumulator(S_BOARD *pos) {
    if (!nnue_loaded) return;
    for (int i = 0; i < g_weights->l1_size; i++) {
        pos->nnue_acc[i] = g_weights->fc1_b[i];
    }
    for (int sq = 0; sq < 64; sq++) {
        int p = pos->pieces[sq];
        if (p == EMPTY) continue;
        nnue_update_add(pos, p, sq);
    }
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
        out[i] = clamp01f(acc);
    }
}

/* ── Eval ───────────────────────────────────────────────────────────────── */
int nnue_eval(const S_BOARD *pos) {
    if (!nnue_loaded) return 0;

    if (g_weights->l2_size > 64 || g_weights->l3_size > 64) {
        fprintf(stderr, "[NNUE] Net dimensions too large for fixed buffers, skipping eval\n");
        return 0;
    }

    /* fc1 activation: dequantize the int32 accumulator back to a float
       in [0,1] via dividing by qa_scale, then clip. This is the ONLY
       place quantization scale matters — everything after this point
       is identical to the float model. */
    float h1[512];
    float inv_qa = 1.0f / (float)g_weights->qa_scale;
    for (int i = 0; i < g_weights->l1_size; i++) {
        h1[i] = clamp01f((float)pos->nnue_acc[i] * inv_qa);
    }

    float h2[64], h3[64];
    linear_clipped(h1, g_weights->l1_size, g_weights->fc2_w, g_weights->fc2_b, g_weights->l2_size, h2);
    linear_clipped(h2, g_weights->l2_size, g_weights->fc3_w, g_weights->fc3_b, g_weights->l3_size, h3);

    float out = g_weights->fc4_b[0];
    for (int j = 0; j < g_weights->l3_size; j++) {
        out += h3[j] * g_weights->fc4_w[j];
    }

    /* out is a White-relative logit (features are absolute/White-fixed,
       see header comment); flip sign for Black to move, matching how
       EvalPosition() uses the result. */
    int cp_white = (int)(out * g_weights->cp_scale);
    return pos->side == WHITE ? cp_white : -cp_white;
}

#endif /* NNUE_IMPLEMENTATION */