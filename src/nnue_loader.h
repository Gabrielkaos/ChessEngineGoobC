/*
 * nnue_loader.h / nnue_loader.c
 * ==============================
 * NNUE inference for GOOB, with an INCREMENTAL accumulator and a fully
 * quantized forward pass (fc1..fc4 all integer, one float multiply at
 * the very end).
 *
 * Architecture (absolute/White-fixed single perspective):
 *   Input  : 768 (12 piece planes x 64 squares, always White's frame of
 *            reference — no mirroring by side to move)
 *   L1     : QUANTIZED (int16 weights, int32 accumulator), clipped ReLU,
 *            requantized to int8 [0,127] for the fc2 dot product
 *   L2     : QUANTIZED (int8 weights, int32 dot product), clipped ReLU,
 *            requantized to int8 [0,127] for the fc3 dot product
 *   L3     : QUANTIZED (int8 weights, int32 dot product), clipped ReLU,
 *            requantized to int8 [0,127] for the fc4 dot product
 *   Output : QUANTIZED (int8 weights, int32 dot product) raw logit;
 *            converted to a float ONCE (not per-neuron) via a single
 *            combined scale, multiplied by cp_scale to get centipawns
 *            from WHITE's POV, then sign-flipped to side-to-move's POV
 *            to match how EvalPosition() uses it.
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
 * WHY every layer is quantized (as of v4): fc1 is the layer the
 * accumulator touches, and fc2/fc3/fc4 are matmuls recomputed every
 * single nnue_eval() call. int8 x int8 -> int32 dot products vectorize
 * (see the AVX2 kernel below) and are cheaper than the equivalent
 * float32 matmul; quantizing fc3/fc4 too means the ONLY float op left
 * in the whole forward pass is a single multiply at the very end to
 * convert fc4's int32 accumulator to a centipawn value — everything
 * upstream of that stays in integer arithmetic, same as Stockfish.
 *
 * Fixed-point interface between layers: dequantizing/requantizing to
 * int8 [0,127] is done with one float divide/round per NEURON (not per
 * weight) at each layer boundary — negligible next to the O(in*out)
 * matmul it feeds — so there's no need for power-of-two scales or
 * bit-shift tricks; any qa/qb/qc/qd scale that avoids int16/int8
 * overflow works. fc4 is the one exception: it has no activation after
 * it (it produces the raw logit, not a [0,1] activation), so its int32
 * accumulator is converted straight to centipawns with a single
 * combined-scale multiply instead of a per-neuron requantize.
 *
 * PERFORMANCE NOTE (fc1 accumulator layout): fc1.weight is stored on
 * disk neuron-major ([l1_size][input_size], see export_weights.py) but
 * is transposed to feature-major ([input_size][l1_size]) in memory
 * right after loading. nnue_update_add/remove/move touch this array on
 * every single make/unmake move, and feature-major layout makes each
 * of those calls walk one contiguous row of l1_size int16s instead of
 * striding input_size*sizeof(int16_t) bytes apart per element. This is
 * purely an in-memory representation change — the on-disk format and
 * export_weights.py's fc1 encoding are untouched. fc2/fc3/fc4 don't
 * need this: their forward pass loops per OUTPUT neuron over a
 * contiguous input row, which is already the disk layout.
 *
 * PERFORMANCE NOTE (SIMD): the int8 x int8 -> int32 dot product shared
 * by fc2/fc3/fc4 has an AVX2 kernel (nnue_dot_i8_avx2, 32 lanes/iter
 * via _mm256_maddubs_epi16 + _mm256_madd_epi16) chosen at runtime via
 * __builtin_cpu_supports, falling back to a portable scalar loop on
 * older x86 CPUs and non-x86 platforms. Compiled with
 * __attribute__((target("avx2"))) so it's safe to build into a
 * normal (non -mavx2) translation unit.
 *
 * Weight file layout v4 (all little-endian, ON DISK — see nnue_init()
 * for the in-memory transpose applied to fc1.weight after reading it):
 *   char[4]  magic = "NNUE"
 *   int32    version = 4
 *   int32    input_size (768)
 *   int32    l1_size (MUST equal NNUE_ACC_SIZE in board.h)
 *   int32    l2_size
 *   int32    l3_size
 *   float32  cp_scale
 *   int32    qa_scale             (fc1 fixed-point scale)
 *   int32    qb_scale             (fc2 fixed-point scale)
 *   int32    qc_scale             (fc3 fixed-point scale)
 *   int32    qd_scale             (fc4 fixed-point scale)
 *   int16[l1_size * input_size]   fc1.weight (quantized)
 *   int32[l1_size]                fc1.bias   (quantized)
 *   int8 [l2_size * l1_size]      fc2.weight (quantized)
 *   int32[l2_size]                fc2.bias   (quantized, scale = 127*qb_scale)
 *   int8 [l3_size * l2_size]      fc3.weight (quantized)
 *   int32[l3_size]                fc3.bias   (quantized, scale = 127*qc_scale)
 *   int8 [1 * l3_size]            fc4.weight (quantized)
 *   int32[1]                      fc4.bias   (quantized, scale = 127*qd_scale)
 *
 * v3 files (float32 fc3/fc4) are no longer accepted — re-export with
 * the current export_weights.py to get a v4 file.
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

/* ── SIMD platform detection for the int8 dot product ─────────────────────
 * fc2/fc3/fc4 all share the same int8 x int8 -> int32 dot product, and
 * together they're the matmuls recomputed every single nnue_eval() call,
 * so this is the highest-value target for vectorization. We use
 * GCC/Clang function-multiversioning-style dispatch: the AVX2 kernel is
 * compiled with __attribute__((target("avx2"))) so it can live in a
 * normally-compiled translation unit (no -mavx2 needed for the whole
 * file/project), and we pick it at runtime via __builtin_cpu_supports so
 * the binary still runs correctly on older/non-AVX2 x86 CPUs and on
 * non-x86 platforms (ARM, etc.) — those just fall back to the portable
 * scalar path below. */
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define NNUE_ARCH_X86 1
#endif

#if defined(NNUE_ARCH_X86) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define NNUE_HAVE_AVX2_DISPATCH 1
#endif

typedef struct {
    int input_size;
    int l1_size;
    int l2_size;
    int l3_size;
    float cp_scale;
    int qa_scale;
    int qb_scale;
    int qc_scale;
    int qd_scale;

    int16_t *fc1_w; /* [input_size][l1_size], quantized — TRANSPOSED in memory
                        from the on-disk [l1_size][input_size] layout, so
                        row = fc1_w + feature*l1_size is contiguous. This is
                        what nnue_update_add/remove/move walk on every
                        make/unmake move, so contiguous access here matters
                        far more than in the eval-time matmuls below. */
    int32_t *fc1_b; /* [l1_size], quantized */
    int8_t  *fc2_w; /* [l2_size][l1_size], quantized */
    int32_t *fc2_b; /* [l2_size], quantized (scale = 127*qb_scale) */
    int8_t  *fc3_w; /* [l3_size][l2_size], quantized */
    int32_t *fc3_b; /* [l3_size], quantized (scale = 127*qc_scale) */
    int8_t  *fc4_w; /* [1][l3_size], quantized (single output neuron) */
    int32_t  fc4_b;  /* scalar, quantized (scale = 127*qd_scale) */

    /* Precomputed once at load time: cp_scale / (127 * qd_scale).
     * fc4's raw int32 accumulator times this single float gives
     * centipawns directly — the only float op in the whole eval. */
    float fc4_combined_scale;
} NNUE_WEIGHTS;

/* ── Globals ────────────────────────────────────────────────────────────── */
static NNUE_WEIGHTS *g_weights = NULL;
int nnue_loaded = 0;

/* ── Helpers ────────────────────────────────────────────────────────────── */

/* Undo a layer's (127*q_scale) combined fixed-point scale and clip to
 * [0,127] in one step: given acc = round(true_value * 127 * q_scale),
 * returns round(clamp01(true_value) * 127) as an int8. Shared by every
 * quantized layer boundary (fc1's accumulator -> h1_i8, fc2 -> h2_i8,
 * fc3 -> h3_i8) since they all use the same bias/output scale
 * convention (see export_weights.py). One float divide per neuron —
 * negligible next to the O(in*out) matmul it feeds. */
static inline int8_t requantize_clipped_i8(int32_t acc, int q_scale) {
    float v = (float)acc / (float)q_scale;
    if (v < 0.0f) v = 0.0f;
    if (v > 127.0f) v = 127.0f;
    return (int8_t)(v + 0.5f);
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
    free(g_weights->fc4_w);
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
    int32_t version, input_size, l1, l2, l3, qa_scale, qb_scale, qc_scale, qd_scale;
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
    ok &= fread(&qb_scale, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&qc_scale, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&qd_scale, sizeof(int32_t), 1, f) == 1;

    if (!ok || version != 4) {
        fprintf(stderr, "[NNUE] Bad header or unsupported version in: %s "
                         "(expected version 4 — retrain/re-export with the "
                         "current export_weights.py; v3 files with float32 "
                         "fc3/fc4 are no longer accepted)\n", path);
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
    g_weights->qb_scale = qb_scale;
    g_weights->qc_scale = qc_scale;
    g_weights->qd_scale = qd_scale;

    /* fc1.weight is stored on disk as [l1_size][input_size] (neuron-major),
     * matching export_weights.py — no need to touch the exporter. We read
     * it into a scratch buffer, then transpose it into [input_size][l1_size]
     * (feature-major) for the actual runtime copy. See the big header
     * comment for why this matters (incremental update hot path). */
    int16_t *fc1_w_diskorder = (int16_t *)read_bytes(f, sizeof(int16_t) * (size_t)l1 * input_size, &ok);
    g_weights->fc1_b = (int32_t *)read_bytes(f, sizeof(int32_t) * (size_t)l1, &ok);
    g_weights->fc2_w = (int8_t *)read_bytes(f, sizeof(int8_t) * (size_t)l2 * l1, &ok);
    g_weights->fc2_b = (int32_t *)read_bytes(f, sizeof(int32_t) * (size_t)l2, &ok);
    g_weights->fc3_w = (int8_t *)read_bytes(f, sizeof(int8_t) * (size_t)l3 * l2, &ok);
    g_weights->fc3_b = (int32_t *)read_bytes(f, sizeof(int32_t) * (size_t)l3, &ok);
    g_weights->fc4_w = (int8_t *)read_bytes(f, sizeof(int8_t) * (size_t)l3, &ok);
    ok &= fread(&g_weights->fc4_b, sizeof(int32_t), 1, f) == 1;

    fclose(f);

    if (ok && fc1_w_diskorder) {
        g_weights->fc1_w = (int16_t *)malloc(sizeof(int16_t) * (size_t)l1 * input_size);
        if (!g_weights->fc1_w) {
            ok = 0;
        } else {
            for (int i = 0; i < l1; i++) {
                const int16_t *src_row = fc1_w_diskorder + (size_t)i * input_size;
                for (int col = 0; col < input_size; col++) {
                    /* dst[col][i] = src[i][col] */
                    g_weights->fc1_w[(size_t)col * l1 + i] = src_row[col];
                }
            }
        }
    } else {
        ok = 0;
    }
    free(fc1_w_diskorder);

    if (!ok) {
        fprintf(stderr, "[NNUE] Weight file truncated or corrupt: %s\n", path);
        nnue_free_weights();
        nnue_loaded = 0;
        return 0;
    }

    g_weights->fc4_combined_scale = cp_scale / (127.0f * (float)qd_scale);

    nnue_loaded = 1;
    printf("[NNUE] v4 weights loaded from %s (in=%d l1=%d l2=%d l3=%d "
           "scale=%.1f qa=%d qb=%d qc=%d qd=%d)\n", path, input_size, l1, l2, l3,
           cp_scale, qa_scale, qb_scale, qc_scale, qd_scale);
    return 1;
}

/* ── Incremental accumulator maintenance ───────────────────────────────── */

void nnue_update_add(S_BOARD *pos, int piece, int sq) {
    if (!nnue_loaded || piece == EMPTY) return;
    int col = (piece - 1) * 64 + sq;
    int l1_size = g_weights->l1_size;
    /* fc1_w is feature-major: this row is l1_size contiguous int16s. */
    const int16_t *row = g_weights->fc1_w + (size_t)col * l1_size;
    for (int i = 0; i < l1_size; i++) {
        pos->nnue_acc[i] += (int32_t)row[i];
    }
}

void nnue_update_remove(S_BOARD *pos, int piece, int sq) {
    if (!nnue_loaded || piece == EMPTY) return;
    int col = (piece - 1) * 64 + sq;
    int l1_size = g_weights->l1_size;
    const int16_t *row = g_weights->fc1_w + (size_t)col * l1_size;
    for (int i = 0; i < l1_size; i++) {
        pos->nnue_acc[i] -= (int32_t)row[i];
    }
}

void nnue_update_move(S_BOARD *pos, int piece, int from, int to) {
    if (!nnue_loaded || piece == EMPTY) return;
    int l1_size = g_weights->l1_size;
    int col_from = (piece - 1) * 64 + from;
    int col_to   = (piece - 1) * 64 + to;
    const int16_t *row_from = g_weights->fc1_w + (size_t)col_from * l1_size;
    const int16_t *row_to   = g_weights->fc1_w + (size_t)col_to   * l1_size;
    for (int i = 0; i < l1_size; i++) {
        pos->nnue_acc[i] += (int32_t)row_to[i] - (int32_t)row_from[i];
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

/* ── int8 dot product kernels, shared by fc2/fc3/fc4 ──────────────────────
 * a[] (the incoming activation) is always in [0,127] (a clipped-ReLU
 * activation requantized to int8 — see requantize_clipped_i8), so it's
 * safe to reinterpret as unsigned bytes. b[] is the signed int8 weight
 * row for one output neuron. */

static int32_t nnue_dot_i8_scalar(const int8_t *a, const int8_t *b, int n) {
    int32_t acc = 0;
    for (int j = 0; j < n; j++) {
        acc += (int32_t)(unsigned char)a[j] * (int32_t)b[j];
    }
    return acc;
}

#ifdef NNUE_HAVE_AVX2_DISPATCH
/* Processes 32 int8 lanes per iteration:
 *   _mm256_maddubs_epi16(u8, s8) -> 16 int16 partial sums (adjacent pairs)
 *   _mm256_madd_epi16(., ones)   -> 8 int32 partial sums (adjacent pairs)
 * accumulated across the loop, then horizontally reduced once at the end.
 * No overflow risk: int8 x int8 products are at most ±127*128, and the
 * maddubs pairwise sum stays comfortably inside the int16 range. */
__attribute__((target("avx2")))
static int32_t nnue_dot_i8_avx2(const int8_t *a, const int8_t *b, int n) {
    __m256i acc = _mm256_setzero_si256();
    const __m256i ones = _mm256_set1_epi16(1);
    int i = 0;
    for (; i + 32 <= n; i += 32) {
        __m256i va = _mm256_loadu_si256((const __m256i *)(a + i));
        __m256i vb = _mm256_loadu_si256((const __m256i *)(b + i));
        __m256i prod16 = _mm256_maddubs_epi16(va, vb);
        __m256i prod32 = _mm256_madd_epi16(prod16, ones);
        acc = _mm256_add_epi32(acc, prod32);
    }
    __m128i lo = _mm256_castsi256_si128(acc);
    __m128i hi = _mm256_extracti128_si256(acc, 1);
    __m128i sum128 = _mm_add_epi32(lo, hi);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    int32_t sum = _mm_cvtsi128_si32(sum128);
    /* tail: n isn't guaranteed to be a multiple of 32 (l1/l2/l3 sizes
     * are architecture choices, not tuned to vector width) */
    sum += nnue_dot_i8_scalar(a + i, b + i, n - i);
    return sum;
}

/* Detected once, lazily, and cached. __builtin_cpu_init() is called first
 * since older GCC versions require it before __builtin_cpu_supports is
 * reliable. */
static int cpu_supports_avx2(void) {
    static int cached = -1;
    if (cached < 0) {
        __builtin_cpu_init();
        cached = __builtin_cpu_supports("avx2") ? 1 : 0;
    }
    return cached;
}
#endif /* NNUE_HAVE_AVX2_DISPATCH */

/* Dispatches to AVX2 when available (x86 + runtime support), else the
 * portable scalar loop. Used by every quantized layer's forward pass. */
static inline int32_t nnue_dot_i8(const int8_t *a, const int8_t *b, int n) {
#ifdef NNUE_HAVE_AVX2_DISPATCH
    if (cpu_supports_avx2()) return nnue_dot_i8_avx2(a, b, n);
#endif
    return nnue_dot_i8_scalar(a, b, n);
}

/* ── Quantized layer forward passes ────────────────────────────────────── */

/* out_i8[i] = requantize_clipped_i8( bias[i] + dot(in_i8, w_row_i), q_scale )
 * for i in [0,out_size). w is [out_size][in_size], row-major — already
 * contiguous per output neuron on disk, no transpose needed (unlike
 * fc1's accumulator-update layout). Shared by fc2 and fc3, which are
 * structurally identical (int8-in, int8-out, clipped-ReLU). */
static void int8_layer_forward(const int8_t *in_i8, int in_size,
                                const int8_t *w, const int32_t *b,
                                int q_scale, int out_size,
                                int8_t *out_i8) {
    for (int i = 0; i < out_size; i++) {
        const int8_t *row = w + (size_t)i * in_size;
        int32_t acc = b[i] + nnue_dot_i8(in_i8, row, in_size);
        out_i8[i] = requantize_clipped_i8(acc, q_scale);
    }
}

/* fc4 differs from fc2/fc3: single output neuron, and no activation
 * after it (raw logit, not a [0,1] activation) — so it just returns the
 * int32 accumulator for the caller to convert to centipawns once. */
static int32_t fc4_forward(const int8_t *in_i8, int in_size,
                            const int8_t *w, int32_t b) {
    return b + nnue_dot_i8(in_i8, w, in_size);
}

/* ── Eval ───────────────────────────────────────────────────────────────── */
int nnue_eval(const S_BOARD *pos) {
    if (!nnue_loaded) return 0;

    if (g_weights->l2_size > 64 || g_weights->l3_size > 64) {
        fprintf(stderr, "[NNUE] Net dimensions too large for fixed buffers, skipping eval\n");
        return 0;
    }

    /* fc1 activation: dequantize the int32 accumulator and requantize to
     * int8 [0,127] for the fc2 dot product (see requantize_clipped_i8). */
    int8_t h1_i8[NNUE_ACC_SIZE];
    for (int i = 0; i < g_weights->l1_size; i++) {
        h1_i8[i] = requantize_clipped_i8(pos->nnue_acc[i], g_weights->qa_scale);
    }

    int8_t h2_i8[64], h3_i8[64];
    int8_layer_forward(h1_i8, g_weights->l1_size, g_weights->fc2_w, g_weights->fc2_b,
                        g_weights->qb_scale, g_weights->l2_size, h2_i8);
    int8_layer_forward(h2_i8, g_weights->l2_size, g_weights->fc3_w, g_weights->fc3_b,
                        g_weights->qc_scale, g_weights->l3_size, h3_i8);
    int32_t acc4 = fc4_forward(h3_i8, g_weights->l3_size, g_weights->fc4_w, g_weights->fc4_b);

    /* acc4 is fc4's int32 accumulator, in units of (127*qd_scale) — the
     * ONLY float conversion in the whole forward pass happens here,
     * once, not per-neuron. out is a White-relative logit (features are
     * absolute/White-fixed, see header comment); flip sign for Black to
     * move, matching how EvalPosition() uses the result. */
    int cp_white = (int)((float)acc4 * g_weights->fc4_combined_scale);
    return pos->side == WHITE ? cp_white : -cp_white;
}

#endif /* NNUE_IMPLEMENTATION */