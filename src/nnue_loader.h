/*
 * nnue_loader.h / nnue_loader.c
 * ==============================
 * NNUE inference for GOOB — Schoenemann-0.5.0 Architecture:
 *
 *   - 768 perspective-aware input features (2 colors x 6 piece types x 64 squares).
 *     Unlike HalfKP/HalfKA, king positions are NOT part of the feature index!
 *     Moving a king is an ordinary piece move (O(1) incremental update),
 *     eliminating king-move accumulator refreshes during search.
 *   - Dual accumulators per position:
 *       acc[WHITE]: color * 384 + piece * 64 + sq
 *       acc[BLACK]: (color ^ 1) * 384 + piece * 64 + (sq ^ 56)
 *     Width: 1024 (int16_t)
 *   - Activation: Squared Clipped ReLU (SCReLU):
 *       clamp(x, 0, QA)^2 where QA = 255
 *   - Direct 8-bucket output layer:
 *       bucket = clamp((pieces - 2) / 4, 0, 7)
 *       Concatenates [us_acc | them_acc] (2048 values) and evaluates dot product
 *       into the active material bucket's weights (int16_t) + bias.
 *   - Fully quantized integer forward pass (AVX2 / AVX-512 SIMD):
 *       eval = sum(screlu * weight) / QA + bias
 *       eval = eval * scale / (QA * QB)   [scale=400, QA=255, QB=64]
 *   - Memory footprint: ~1.6 MB (803,848 int16 values = 1,607,696 bytes).
 *
 * File format and public API are unchanged from the previous version, so
 * existing quantised.bin files load as before.
 *
 * Build notes (these matter for speed):
 *   - Compile with -O3 -march=native (or at least -mavx2 -mbmi2 -mpopcnt).
 *     Without -mavx2 the AVX2 kernels are reached through a runtime check and
 *     cannot be inlined, and COUNTBIT may fall back to a software popcount.
 *   - On CPUs with AVX-512BW (Zen 4/5, Ice Lake and newer server/mobile parts),
 *     -march=native enables the 512-bit kernels below automatically.
 *   - Accumulators are accessed with unaligned loads/stores, so misalignment
 *     can no longer crash, but keep them 64-byte aligned for full speed.
 */

#ifndef NNUE_LOADER_H
#define NNUE_LOADER_H

#include "defs.h"
#include "board.h"
#include "bitboards.h"

/* ── Public API ──────────────────────────────────────────────────────────── */

/* Load weights from binary file. Returns 1 on success, 0 on failure.
 * If path is NULL or "<empty>", checks default candidate paths.
 * On failure, a previously loaded network (if any) stays active. */
int nnue_init(const char *path);

/* Full rebuild of pos->search->nnue_accumulators[ply] for both perspectives. */
void nnue_refresh_accumulator(S_BOARD *pos);

/* Compatibility hooks for move updates (search uses dirtyPieces) */
void nnue_update_add(S_BOARD *pos, int piece, int sq);
void nnue_update_remove(S_BOARD *pos, int piece, int sq);
void nnue_update_move(S_BOARD *pos, int piece, int from, int to);

/* Evaluate board position using NNUE.
 * Returns centipawns from the SIDE-TO-MOVE's perspective.
 * Works without a search context (pos->search == NULL) and beyond MAXDEPTH,
 * by computing a fresh, uncached evaluation in those cases. */
int nnue_eval(S_BOARD *pos);

/* True after successful nnue_init */
extern int nnue_loaded;

#endif /* NNUE_LOADER_H */


/* ═══════════════════════════════════════════════════════════════════════════
 * IMPLEMENTATION  –  define NNUE_IMPLEMENTATION in exactly one .c file
 *                    (nnue_loader.c defines this).
 * ═══════════════════════════════════════════════════════════════════════════ */
#ifdef NNUE_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#if defined(_WIN32)
#include <malloc.h>
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define NNUE_ARCH_X86 1
#endif

/* Build with -DNNUE_NO_SIMD to force the scalar reference path (debugging). */
#if defined(NNUE_ARCH_X86) && (defined(__GNUC__) || defined(__clang__)) && !defined(NNUE_NO_SIMD)
#include <immintrin.h>
#if defined(__AVX512BW__)
#define NNUE_USE_AVX512 1            /* compile-time selected, no dispatch   */
#else
#define NNUE_HAVE_AVX2_DISPATCH 1    /* AVX2 if compiled in or detected      */
#endif
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NNUE_UNROLL _Pragma("GCC unroll 16")
#else
#define NNUE_UNROLL
#endif

/* AVX2 kernels get a target attribute only when the whole build is not
 * already AVX2, so that -mavx2 / -march=native builds can inline them. */
#if defined(NNUE_HAVE_AVX2_DISPATCH) && !defined(__AVX2__)
#define NNUE_AVX2_FN __attribute__((target("avx2")))
#else
#define NNUE_AVX2_FN
#endif

static inline int cpu_supports_avx2(void) {
#if defined(__AVX2__)
    return 1;
#elif defined(NNUE_HAVE_AVX2_DISPATCH)
    static int cached = -1;
    if (cached < 0) {
        __builtin_cpu_init();
        cached = __builtin_cpu_supports("avx2") ? 1 : 0;
    }
    return cached;
#else
    return 0;
#endif
}

/* ── Network Architecture Constants ─────────────────────────────────────── */
#define NNUE_INPUT_SIZE        768
#define NNUE_HIDDEN_SIZE       1024
#define NNUE_OUTPUT_BUCKETS    8
#define NNUE_SCALE             400
#define NNUE_QA                255
#define NNUE_QB                64

/* The fast SCReLU kernel computes (w * v) in int16 before the madd, with
 * v in [0, QA]. That product only fits if |w| <= 128 (128 * 255 = 32640),
 * i.e. |float weight| <= ~2.0. Nets trained without weight clipping can
 * exceed this; they are detected at load time and use the exact kernel. */
#define NNUE_FAST_OUT_W_LIMIT  128

/* Trailing bytes bullet appends to pad the file to a multiple of 64. */
#define NNUE_BULLET_PADDING    48

#define NNUE_TOTAL_SHORTS      (NNUE_INPUT_SIZE * NNUE_HIDDEN_SIZE + \
                                NNUE_HIDDEN_SIZE + \
                                NNUE_OUTPUT_BUCKETS * (NNUE_HIDDEN_SIZE * 2) + \
                                NNUE_OUTPUT_BUCKETS)

typedef struct {
    ALIGN64 int16_t ft_w[NNUE_INPUT_SIZE * NNUE_HIDDEN_SIZE];
    ALIGN64 int16_t ft_b[NNUE_HIDDEN_SIZE];
    ALIGN64 int16_t out_w[NNUE_OUTPUT_BUCKETS][NNUE_HIDDEN_SIZE * 2];
    ALIGN64 int16_t out_b[NNUE_OUTPUT_BUCKETS];
} NNUE_Weights;

static NNUE_Weights *g_weights = NULL;
static int g_out_needs_exact = 0;   /* 1 if max |out_w| > NNUE_FAST_OUT_W_LIMIT */
int nnue_loaded = 0;

/* ── Feature Index Calculation ───────────────────────────────────────────── */
static inline size_t nnue_feature_index(int us, int piece, int sq) {
    int c = COLOR_OF(piece);
    int pt = PTYPE_OF(piece);
    if (us == WHITE) {
        return (size_t)(c * 384 + pt * 64 + sq);
    } else {
        return (size_t)((c ^ 1) * 384 + pt * 64 + (sq ^ 56));
    }
}

static inline const int16_t *nnue_ft_row(int us, int piece, int sq) {
    return g_weights->ft_w + nnue_feature_index(us, piece, sq) * (size_t)NNUE_HIDDEN_SIZE;
}

/* Shared integer tail of the forward pass (identical for every kernel). */
static inline int32_t nnue_finish(int32_t sum, int bucket) {
    int32_t eval = sum / NNUE_QA;
    eval += g_weights->out_b[bucket];
    eval *= NNUE_SCALE;
    eval /= (NNUE_QA * NNUE_QB);
    return eval;
}

/* ═══ Kernels ═══════════════════════════════════════════════════════════════
 * Only two accumulator kernels are needed:
 *   acc_1add_1sub : quiet moves, the hottest path.
 *   acc_apply     : dst = src + sum(adds) - sum(subs), register-tiled, so the
 *                   accumulator is loaded and stored once per tile no matter
 *                   how many rows are applied. Used for refreshes (src = bias,
 *                   adds = every piece), captures, promotions and castling.
 * dst may equal src in acc_apply.
 * ═════════════════════════════════════════════════════════════════════════ */

/* ── AVX-512BW ───────────────────────────────────────────────────────────── */
#if defined(NNUE_USE_AVX512)
#define NNUE_TILE_REGS_512 16   /* 16 zmm x 32 lanes = 512 int16 per tile */

static inline void nnue_acc_apply_avx512(int16_t *dst, const int16_t *src,
                                         const int16_t *const *adds, int n_add,
                                         const int16_t *const *subs, int n_sub) {
    for (int base = 0; base < NNUE_HIDDEN_SIZE; base += 32 * NNUE_TILE_REGS_512) {
        __m512i r[NNUE_TILE_REGS_512];
        NNUE_UNROLL
        for (int k = 0; k < NNUE_TILE_REGS_512; k++)
            r[k] = _mm512_loadu_si512((const void *)(src + base + 32 * k));
        for (int a = 0; a < n_add; a++) {
            const int16_t *row = adds[a] + base;
            NNUE_UNROLL
            for (int k = 0; k < NNUE_TILE_REGS_512; k++)
                r[k] = _mm512_add_epi16(r[k], _mm512_loadu_si512((const void *)(row + 32 * k)));
        }
        for (int s = 0; s < n_sub; s++) {
            const int16_t *row = subs[s] + base;
            NNUE_UNROLL
            for (int k = 0; k < NNUE_TILE_REGS_512; k++)
                r[k] = _mm512_sub_epi16(r[k], _mm512_loadu_si512((const void *)(row + 32 * k)));
        }
        NNUE_UNROLL
        for (int k = 0; k < NNUE_TILE_REGS_512; k++)
            _mm512_storeu_si512((void *)(dst + base + 32 * k), r[k]);
    }
}

static inline void nnue_acc_1add_1sub_avx512(int16_t *curr, const int16_t *prev,
                                             const int16_t *row_add, const int16_t *row_sub) {
    for (int i = 0; i < NNUE_HIDDEN_SIZE; i += 32) {
        __m512i vp = _mm512_loadu_si512((const void *)(prev + i));
        __m512i va = _mm512_loadu_si512((const void *)(row_add + i));
        __m512i vs = _mm512_loadu_si512((const void *)(row_sub + i));
        _mm512_storeu_si512((void *)(curr + i), _mm512_sub_epi16(_mm512_add_epi16(vp, va), vs));
    }
}

static inline int32_t nnue_forward_avx512(const int16_t *us, const int16_t *them, int bucket) {
    const __m512i zero = _mm512_setzero_si512();
    const __m512i qa = _mm512_set1_epi16(NNUE_QA);
    const int16_t *w_us = g_weights->out_w[bucket];
    const int16_t *w_them = g_weights->out_w[bucket] + NNUE_HIDDEN_SIZE;
    __m512i sum0 = zero, sum1 = zero;
    for (int i = 0; i < NNUE_HIDDEN_SIZE; i += 32) {
        __m512i u = _mm512_min_epi16(_mm512_max_epi16(_mm512_loadu_si512((const void *)(us + i)), zero), qa);
        __m512i t = _mm512_min_epi16(_mm512_max_epi16(_mm512_loadu_si512((const void *)(them + i)), zero), qa);
        __m512i wu = _mm512_loadu_si512((const void *)(w_us + i));
        __m512i wt = _mm512_loadu_si512((const void *)(w_them + i));
        sum0 = _mm512_add_epi32(sum0, _mm512_madd_epi16(_mm512_mullo_epi16(wu, u), u));
        sum1 = _mm512_add_epi32(sum1, _mm512_madd_epi16(_mm512_mullo_epi16(wt, t), t));
    }
    return nnue_finish(_mm512_reduce_add_epi32(_mm512_add_epi32(sum0, sum1)), bucket);
}

/* Exact variant for nets with |out_w| > 128: v^2 (<= 65025) is widened to
 * 32 bits and multiplied by the sign-extended weight. */
static inline int32_t nnue_forward_exact_avx512(const int16_t *us, const int16_t *them, int bucket) {
    const __m512i zero = _mm512_setzero_si512();
    const __m512i qa = _mm512_set1_epi16(NNUE_QA);
    const int16_t *acc[2] = { us, them };
    __m512i sum = zero;
    for (int side = 0; side < 2; side++) {
        const int16_t *a = acc[side];
        const int16_t *w = g_weights->out_w[bucket] + side * NNUE_HIDDEN_SIZE;
        for (int i = 0; i < NNUE_HIDDEN_SIZE; i += 32) {
            __m512i c = _mm512_min_epi16(_mm512_max_epi16(_mm512_loadu_si512((const void *)(a + i)), zero), qa);
            __m512i sq = _mm512_mullo_epi16(c, c);
            __m512i wv = _mm512_loadu_si512((const void *)(w + i));
            __m512i ws = _mm512_srai_epi16(wv, 15);
            sum = _mm512_add_epi32(sum, _mm512_mullo_epi32(_mm512_unpacklo_epi16(sq, zero),
                                                           _mm512_unpacklo_epi16(wv, ws)));
            sum = _mm512_add_epi32(sum, _mm512_mullo_epi32(_mm512_unpackhi_epi16(sq, zero),
                                                           _mm512_unpackhi_epi16(wv, ws)));
        }
    }
    return nnue_finish(_mm512_reduce_add_epi32(sum), bucket);
}
#endif /* NNUE_USE_AVX512 */

/* ── AVX2 ────────────────────────────────────────────────────────────────── */
#if defined(NNUE_HAVE_AVX2_DISPATCH)
#define NNUE_TILE_REGS_256 8    /* 8 ymm x 16 lanes = 128 int16 per tile */

NNUE_AVX2_FN
static inline void nnue_acc_apply_avx2(int16_t *dst, const int16_t *src,
                                       const int16_t *const *adds, int n_add,
                                       const int16_t *const *subs, int n_sub) {
    for (int base = 0; base < NNUE_HIDDEN_SIZE; base += 16 * NNUE_TILE_REGS_256) {
        __m256i r[NNUE_TILE_REGS_256];
        NNUE_UNROLL
        for (int k = 0; k < NNUE_TILE_REGS_256; k++)
            r[k] = _mm256_loadu_si256((const __m256i *)(src + base + 16 * k));
        for (int a = 0; a < n_add; a++) {
            const int16_t *row = adds[a] + base;
            NNUE_UNROLL
            for (int k = 0; k < NNUE_TILE_REGS_256; k++)
                r[k] = _mm256_add_epi16(r[k], _mm256_loadu_si256((const __m256i *)(row + 16 * k)));
        }
        for (int s = 0; s < n_sub; s++) {
            const int16_t *row = subs[s] + base;
            NNUE_UNROLL
            for (int k = 0; k < NNUE_TILE_REGS_256; k++)
                r[k] = _mm256_sub_epi16(r[k], _mm256_loadu_si256((const __m256i *)(row + 16 * k)));
        }
        NNUE_UNROLL
        for (int k = 0; k < NNUE_TILE_REGS_256; k++)
            _mm256_storeu_si256((__m256i *)(dst + base + 16 * k), r[k]);
    }
}

NNUE_AVX2_FN
static inline void nnue_acc_1add_1sub_avx2(int16_t *curr, const int16_t *prev,
                                           const int16_t *row_add, const int16_t *row_sub) {
    for (int i = 0; i < NNUE_HIDDEN_SIZE; i += 16) {
        __m256i vp = _mm256_loadu_si256((const __m256i *)(prev + i));
        __m256i va = _mm256_loadu_si256((const __m256i *)(row_add + i));
        __m256i vs = _mm256_loadu_si256((const __m256i *)(row_sub + i));
        _mm256_storeu_si256((__m256i *)(curr + i), _mm256_sub_epi16(_mm256_add_epi16(vp, va), vs));
    }
}

NNUE_AVX2_FN
static inline int32_t nnue_hsum_epi32_avx2(__m256i v) {
    __m128i s = _mm_add_epi32(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(1, 0, 3, 2)));
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, _MM_SHUFFLE(2, 3, 0, 1)));
    return _mm_cvtsi128_si32(s);
}

NNUE_AVX2_FN
static inline int32_t nnue_forward_avx2(const int16_t *us, const int16_t *them, int bucket) {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i qa = _mm256_set1_epi16(NNUE_QA);
    const int16_t *w_us = g_weights->out_w[bucket];
    const int16_t *w_them = g_weights->out_w[bucket] + NNUE_HIDDEN_SIZE;
    __m256i sum0 = zero, sum1 = zero;   /* two chains: shorter dependency */
    for (int i = 0; i < NNUE_HIDDEN_SIZE; i += 16) {
        __m256i u = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256((const __m256i *)(us + i)), zero), qa);
        __m256i t = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256((const __m256i *)(them + i)), zero), qa);
        __m256i wu = _mm256_loadu_si256((const __m256i *)(w_us + i));
        __m256i wt = _mm256_loadu_si256((const __m256i *)(w_them + i));
        sum0 = _mm256_add_epi32(sum0, _mm256_madd_epi16(_mm256_mullo_epi16(wu, u), u));
        sum1 = _mm256_add_epi32(sum1, _mm256_madd_epi16(_mm256_mullo_epi16(wt, t), t));
    }
    return nnue_finish(nnue_hsum_epi32_avx2(_mm256_add_epi32(sum0, sum1)), bucket);
}

NNUE_AVX2_FN
static inline int32_t nnue_forward_exact_avx2(const int16_t *us, const int16_t *them, int bucket) {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i qa = _mm256_set1_epi16(NNUE_QA);
    const int16_t *acc[2] = { us, them };
    __m256i sum = zero;
    for (int side = 0; side < 2; side++) {
        const int16_t *a = acc[side];
        const int16_t *w = g_weights->out_w[bucket] + side * NNUE_HIDDEN_SIZE;
        for (int i = 0; i < NNUE_HIDDEN_SIZE; i += 16) {
            __m256i c = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256((const __m256i *)(a + i)), zero), qa);
            __m256i sq = _mm256_mullo_epi16(c, c);          /* v^2 as unsigned 16-bit */
            __m256i wv = _mm256_loadu_si256((const __m256i *)(w + i));
            __m256i ws = _mm256_srai_epi16(wv, 15);         /* sign words for widening */
            sum = _mm256_add_epi32(sum, _mm256_mullo_epi32(_mm256_unpacklo_epi16(sq, zero),
                                                           _mm256_unpacklo_epi16(wv, ws)));
            sum = _mm256_add_epi32(sum, _mm256_mullo_epi32(_mm256_unpackhi_epi16(sq, zero),
                                                           _mm256_unpackhi_epi16(wv, ws)));
        }
    }
    return nnue_finish(nnue_hsum_epi32_avx2(sum), bucket);
}
#endif /* NNUE_HAVE_AVX2_DISPATCH */

/* ── Scalar (exact reference; compilers auto-vectorize these) ────────────── */
static inline void nnue_acc_apply_scalar(int16_t *dst, const int16_t *src,
                                         const int16_t *const *adds, int n_add,
                                         const int16_t *const *subs, int n_sub) {
    if (dst != src) memcpy(dst, src, NNUE_HIDDEN_SIZE * sizeof(int16_t));
    for (int a = 0; a < n_add; a++)
        for (int i = 0; i < NNUE_HIDDEN_SIZE; i++) dst[i] = (int16_t)(dst[i] + adds[a][i]);
    for (int s = 0; s < n_sub; s++)
        for (int i = 0; i < NNUE_HIDDEN_SIZE; i++) dst[i] = (int16_t)(dst[i] - subs[s][i]);
}

static inline void nnue_acc_1add_1sub_scalar(int16_t *curr, const int16_t *prev,
                                             const int16_t *row_add, const int16_t *row_sub) {
    for (int i = 0; i < NNUE_HIDDEN_SIZE; i++)
        curr[i] = (int16_t)(prev[i] + row_add[i] - row_sub[i]);
}

static inline int32_t screlu(int16_t v) {
    int32_t c = v;
    if (c < 0) c = 0;
    else if (c > NNUE_QA) c = NNUE_QA;
    return c * c;
}

static inline int32_t nnue_forward_scalar(const int16_t *us, const int16_t *them, int bucket) {
    int32_t sum = 0;
    const int16_t *w_us = g_weights->out_w[bucket];
    const int16_t *w_them = g_weights->out_w[bucket] + NNUE_HIDDEN_SIZE;
    for (int i = 0; i < NNUE_HIDDEN_SIZE; i++) {
        sum += screlu(us[i]) * (int32_t)w_us[i] +
               screlu(them[i]) * (int32_t)w_them[i];
    }
    return nnue_finish(sum, bucket);
}

/* ── Dispatchers ─────────────────────────────────────────────────────────── */
static inline void nnue_acc_apply(int16_t *dst, const int16_t *src,
                                  const int16_t *const *adds, int n_add,
                                  const int16_t *const *subs, int n_sub) {
#if defined(NNUE_USE_AVX512)
    nnue_acc_apply_avx512(dst, src, adds, n_add, subs, n_sub);
#else
#if defined(NNUE_HAVE_AVX2_DISPATCH)
    if (cpu_supports_avx2()) { nnue_acc_apply_avx2(dst, src, adds, n_add, subs, n_sub); return; }
#endif
    nnue_acc_apply_scalar(dst, src, adds, n_add, subs, n_sub);
#endif
}

static inline void nnue_acc_1add_1sub(int16_t *curr, const int16_t *prev,
                                      const int16_t *row_add, const int16_t *row_sub) {
#if defined(NNUE_USE_AVX512)
    nnue_acc_1add_1sub_avx512(curr, prev, row_add, row_sub);
#else
#if defined(NNUE_HAVE_AVX2_DISPATCH)
    if (cpu_supports_avx2()) { nnue_acc_1add_1sub_avx2(curr, prev, row_add, row_sub); return; }
#endif
    nnue_acc_1add_1sub_scalar(curr, prev, row_add, row_sub);
#endif
}

static inline int32_t nnue_forward(const int16_t *us, const int16_t *them, int bucket) {
#if defined(NNUE_USE_AVX512)
    return g_out_needs_exact ? nnue_forward_exact_avx512(us, them, bucket)
                             : nnue_forward_avx512(us, them, bucket);
#else
#if defined(NNUE_HAVE_AVX2_DISPATCH)
    if (cpu_supports_avx2())
        return g_out_needs_exact ? nnue_forward_exact_avx2(us, them, bucket)
                                 : nnue_forward_avx2(us, them, bucket);
#endif
    return nnue_forward_scalar(us, them, bucket);
#endif
}

/* ── Full Perspective Refresh ────────────────────────────────────────────── */
static void nnue_refresh_perspective(const S_BOARD *pos, int us, int16_t *acc) {
    const int16_t *rows[64];
    int n = 0;
    for (int sq = 0; sq < 64; sq++) {
        int p = pos->pieces[sq];
        if (p != EMPTY) rows[n++] = nnue_ft_row(us, p, sq);
    }
    /* One tiled pass: bias + all pieces, accumulator stored once per tile. */
    nnue_acc_apply(acc, g_weights->ft_b, rows, n, NULL, 0);
}

void nnue_refresh_accumulator(S_BOARD *pos) {
    if (!nnue_loaded || !pos->search) return;
    int ply = pos->ply;
    /* Plies outside the stack are evaluated fresh by nnue_eval; writing them
     * into the last slot (as before) could leave a wrong accumulator marked
     * as computed for the position that really lives at MAXDEPTH - 1. */
    if (ply < 0 || ply >= MAXDEPTH) return;
    nnue_refresh_perspective(pos, WHITE, pos->search->nnue_accumulators[ply].accumulation[WHITE]);
    nnue_refresh_perspective(pos, BLACK, pos->search->nnue_accumulators[ply].accumulation[BLACK]);
    pos->search->nnue_accumulators[ply].computed[WHITE] = 1;
    pos->search->nnue_accumulators[ply].computed[BLACK] = 1;
}

/* ── Incremental Move Step ───────────────────────────────────────────────── */
static void nnue_update_accumulator_step(int us,
                                         const int16_t *prev_acc,
                                         int16_t *curr_acc,
                                         const DirtyPiece *dp) {
    if (dp->remove_count == 1 && dp->add_count == 1) {
        nnue_acc_1add_1sub(curr_acc, prev_acc,
                           nnue_ft_row(us, dp->piece_add[0], dp->to[0]),
                           nnue_ft_row(us, dp->piece_remove[0], dp->from[0]));
        return;
    }
    if (dp->remove_count == 0 && dp->add_count == 0) {
        memcpy(curr_acc, prev_acc, NNUE_HIDDEN_SIZE * sizeof(int16_t));
        return;
    }

    /* Captures (2 sub, 1 add), capture-promotions, castling (2/2), and any
     * other count: one tiled pass per group of up to 4 adds / 4 subs. */
    const int16_t *adds[4], *subs[4];
    const int16_t *src = prev_acc;
    int a = 0, r = 0;
    do {
        int na = 0, nr = 0;
        while (a < dp->add_count && na < 4) {
            adds[na++] = nnue_ft_row(us, dp->piece_add[a], dp->to[a]);
            a++;
        }
        while (r < dp->remove_count && nr < 4) {
            subs[nr++] = nnue_ft_row(us, dp->piece_remove[r], dp->from[r]);
            r++;
        }
        nnue_acc_apply(curr_acc, src, adds, na, subs, nr);
        src = curr_acc;
    } while (a < dp->add_count || r < dp->remove_count);
}

/* ── Lazy Accumulator Multi-Ply Traversal ────────────────────────────────── */
static void nnue_update_perspective_to_ply(S_BOARD *pos, int us, int target_ply) {
    if (pos->search->nnue_accumulators[target_ply].computed[us]) return;

    int ancestor = -1;
    for (int p = target_ply - 1; p >= 0; p--) {
        if (pos->search->nnue_accumulators[p].computed[us]) {
            ancestor = p;
            break;
        }
    }

    // With 768 features, king moves never force a full refresh!
    // Refresh only if we don't have a recent ancestor in search tree.
    if (ancestor < 0 || (target_ply - ancestor > 4)) {
        nnue_refresh_perspective(pos, us, pos->search->nnue_accumulators[target_ply].accumulation[us]);
        pos->search->nnue_accumulators[target_ply].computed[us] = 1;
        return;
    }

    for (int step = ancestor + 1; step <= target_ply; step++) {
        if (!pos->search->nnue_accumulators[step].computed[us]) {
            nnue_update_accumulator_step(us,
                                         pos->search->nnue_accumulators[step - 1].accumulation[us],
                                         pos->search->nnue_accumulators[step].accumulation[us],
                                         &pos->search->dirtyPieces[step]);
            pos->search->nnue_accumulators[step].computed[us] = 1;
        }
    }
}

void nnue_update_add(S_BOARD *pos, int piece, int sq) { (void)pos; (void)piece; (void)sq; }
void nnue_update_remove(S_BOARD *pos, int piece, int sq) { (void)pos; (void)piece; (void)sq; }
void nnue_update_move(S_BOARD *pos, int piece, int from, int to) { (void)pos; (void)piece; (void)from; (void)to; }

/* ── Evaluation ──────────────────────────────────────────────────────────── */
static inline int nnue_output_bucket(const S_BOARD *pos) {
    int pieces = COUNTBIT(pos->byTypeBB[ALL_PIECES]);
    int bucket = (pieces - 2) / 4;
    if (bucket < 0) bucket = 0;
    else if (bucket > 7) bucket = 7;
    return bucket;
}

int nnue_eval(S_BOARD *pos) {
    if (!nnue_loaded || !g_weights) return 0;

    int stm = pos->side;
    int other = stm ^ 1;
    int bucket = nnue_output_bucket(pos);
    int ply = pos->ply;

    /* No search stack (e.g. a UCI "eval" command) or beyond it: evaluate
     * from scratch without touching the cache. */
    if (!pos->search || ply < 0 || ply >= MAXDEPTH) {
        ALIGN64 int16_t acc[2][NNUE_HIDDEN_SIZE];
        nnue_refresh_perspective(pos, WHITE, acc[WHITE]);
        nnue_refresh_perspective(pos, BLACK, acc[BLACK]);
        return nnue_forward(acc[stm], acc[other], bucket);
    }

    nnue_update_perspective_to_ply(pos, WHITE, ply);
    nnue_update_perspective_to_ply(pos, BLACK, ply);

    const int16_t *acc_stm = pos->search->nnue_accumulators[ply].accumulation[stm];
    const int16_t *acc_other = pos->search->nnue_accumulators[ply].accumulation[other];
    return nnue_forward(acc_stm, acc_other, bucket);
}

/* ── Weight Loading & Initialization ─────────────────────────────────────── */
static void *nnue_aligned_alloc(size_t align, size_t size) {
#if defined(_WIN32)
    return _aligned_malloc(size, align);
#else
    size = (size + align - 1) & ~(align - 1);   /* C11 requires a multiple */
    return aligned_alloc(align, size);
#endif
}

static void nnue_aligned_free(void *p) {
#if defined(_WIN32)
    _aligned_free(p);
#else
    free(p);
#endif
}

int nnue_init(const char *path) {
    FILE *f = NULL;
    const char *opened = NULL;

    if (path && path[0] && strcmp(path, "<empty>") != 0) {
        f = fopen(path, "rb");
        if (f) {
            opened = path;
        } else {
            printf("info string NNUE: cannot open '%s', trying default locations\n", path);
            fflush(stdout);
        }
    }
    if (!f) {
        const char *candidates[] = {
            "quantised.bin",
            "weights/quantised.bin",
            "src/weights/quantised.bin",
            "Schoenemann-0.5.0/src/quantised.bin",
            "../Schoenemann-0.5.0/src/quantised.bin",
            NULL
        };
        for (int i = 0; candidates[i] != NULL; i++) {
            f = fopen(candidates[i], "rb");
            if (f) { opened = candidates[i]; break; }
        }
    }
    if (!f) {
        printf("info string NNUE: no network file found\n");
        fflush(stdout);
        return 0;
    }

    /* Load into a fresh buffer so a failed reload cannot corrupt the
     * network that is currently in use. */
    NNUE_Weights *w = (NNUE_Weights *)nnue_aligned_alloc(64, sizeof(NNUE_Weights));
    if (!w) {
        fclose(f);
        return 0;
    }

    size_t read = 0;
    read += fread(w->ft_w, sizeof(int16_t), NNUE_INPUT_SIZE * NNUE_HIDDEN_SIZE, f);
    read += fread(w->ft_b, sizeof(int16_t), NNUE_HIDDEN_SIZE, f);
    read += fread(w->out_w, sizeof(int16_t), NNUE_OUTPUT_BUCKETS * NNUE_HIDDEN_SIZE * 2, f);
    read += fread(w->out_b, sizeof(int16_t), NNUE_OUTPUT_BUCKETS, f);

    long extra = 0;
    {
        long here = ftell(f);
        if (here >= 0 && fseek(f, 0, SEEK_END) == 0) {
            long end = ftell(f);
            if (end > here) extra = end - here;
        }
    }
    fclose(f);

    if (read < (size_t)NNUE_TOTAL_SHORTS) {
        printf("info string NNUE load failed: expected %zu shorts, read %zu\n",
               (size_t)NNUE_TOTAL_SHORTS, read);
        fflush(stdout);
        nnue_aligned_free(w);
        return 0;
    }
    if (extra != 0 && extra != NNUE_BULLET_PADDING) {
        printf("info string NNUE warning: '%s' has %ld unexpected trailing bytes "
               "(different architecture or layout?)\n", opened, extra);
    }

    int max_abs_out = 0;
    for (int b = 0; b < NNUE_OUTPUT_BUCKETS; b++) {
        for (int i = 0; i < 2 * NNUE_HIDDEN_SIZE; i++) {
            int v = w->out_w[b][i];
            if (v < 0) v = -v;
            if (v > max_abs_out) max_abs_out = v;
        }
    }

    NNUE_Weights *old = g_weights;
    g_weights = w;
    g_out_needs_exact = max_abs_out > NNUE_FAST_OUT_W_LIMIT;
    nnue_loaded = 1;
    if (old) nnue_aligned_free(old);

    if (g_out_needs_exact) {
        printf("info string NNUE: output weights reach %d (fast kernel needs <= %d); "
               "using the exact kernel. Retrain with weight clipping for full speed.\n",
               max_abs_out, NNUE_FAST_OUT_W_LIMIT);
    }
    fflush(stdout);
    return 1;
}

#endif /* NNUE_IMPLEMENTATION */