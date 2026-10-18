/*
 * nnue_loader.h / nnue_loader.c
 * ==============================
 * NNUE inference for GOOB.
 *
 * Architecture and SIMD inference adapted from:
 *   Schoenemann (https://github.com/Jochengehtab/Schoenemann)
 *   Copyright (c) Jochen Gehtab
 *   Licensed under GNU Affero General Public License v3.0 (AGPL-3.0).
 *
 * Adaptations for GOOB:
 *   - Adapted data structures, integration hooks, and accumulator management
 *     for GOOB's board representation and search by Gabriel Montes.
 *   - Added perspective horizontal king mirroring (NNUE_MIRROR), 4 factorized
 *     perspective king buckets (NNUE_KING_BUCKETS), per-thread Finny table
 *     accumulator caching (pos->search->nnue_cache), and GOOBNNUE v2 format
 *     trailer verification.
 *   - Network weights and training data independently generated and trained
 *     by Gabriel Montes.
 *
 * NNUE Details — Schoenemann-0.5.0 Architecture + King Mirroring + 4 King Buckets:
 *
 *   - 768 * NNUE_KING_BUCKETS perspective-aware input features
 *     (4 king buckets x 2 colors x 6 piece types x 64 squares = 3,072 features).
 *   - Perspective Horizontal King Mirroring (NNUE_MIRROR, default 1):
 *     When a perspective's own king is on files e..h ((king_sq & 7) >= 4), all
 *     piece squares for that perspective are horizontally mirrored (flip = 7,
 *     sq ^= flip), folding king files into a..d (so e.g. g1 and c1 both map to c1).
 *   - Perspective King Buckets (NNUE_KING_BUCKETS, default 4):
 *     Each perspective selects a bucket in 0..3 from its own king's relative
 *     square (NNUE_KingBucket[rel_ksq]: 0=castled corner a1,b1,g1,h1;
 *     1=back-rank center c1..f1; 2=2nd rank a2..h2; 3=ranks 3..8).
 *     King moves that stay within the same (flip, bucket) pair are ordinary O(1)
 *     1-add/1-sub incremental updates. Moves where a king crosses a mirror or
 *     bucket boundary set dp->refresh[side] and trigger a single-perspective
 *     Finny-table differential refresh against pos->search->nnue_cache.
 *   - Dual accumulators per position:
 *       acc[WHITE]: bucket_w * 768 + color * 384 + piece * 64 + (sq ^ flip_w)
 *       acc[BLACK]: bucket_b * 768 + (color ^ 1) * 384 + piece * 64 + ((sq ^ 56) ^ flip_b)
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
 *   - Memory footprint (NNUE_KING_BUCKETS=4): ~6.03 MB (3,163,144 int16 values =
 *     6,326,288 bytes + 48-byte GOOBNNUE v2 architecture trailer = 6,326,336 bytes;
 *     1,607,744 bytes when NNUE_KING_BUCKETS=1).
 *   - Architecture trailer validation: nnue_init() verifies the 48-byte trailer
 *     (mirror flag, king_buckets, hidden_size, output_buckets) against compiled
 *     constants and refuses mismatched networks.
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

/* Load weights from binary file or embedded network. Returns 1 on success, 0 on failure.
 * If path is NULL, "<empty>", or "default", loads the embedded network.
 * If path is a valid file, loads the custom network from that file.
 * On failure, a previously loaded network (if any) stays active. */
int nnue_init(const char *path);

/* Full rebuild of pos->search->nnue_accumulators[ply] for both perspectives. */
void nnue_refresh_accumulator(S_BOARD *pos);

/* Invalidate all per-thread Finny accumulator caches (called on ucinewgame and EvalFile). */
void nnue_clear_caches(void);

/* Evaluate board position using NNUE.
 * Returns centipawns from the SIDE-TO-MOVE's perspective.
 * Works without a search context (pos->search == NULL) and beyond MAXDEPTH,
 * by computing a fresh, uncached evaluation in those cases. */
int nnue_eval(S_BOARD *pos);

/* True after successful nnue_init */
extern int nnue_loaded;

static const uint8_t NNUE_KingBucket[64] = {
    0, 0, 1, 1, 1, 1, 0, 0,
    2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
};

static inline uint8_t nnue_sq_flip(int sq) {
#if NNUE_MIRROR
    return ((sq & 7) >= 4) ? 7 : 0;
#else
    (void)sq;
    return 0;
#endif
}

static inline uint8_t nnue_sq_bucket(int us, int sq) {
#if NNUE_KING_BUCKETS > 1
    int rel = (us == WHITE) ? sq : (sq ^ 56);
    return NNUE_KingBucket[rel];
#else
    (void)us;
    (void)sq;
    return 0;
#endif
}

static inline int nnue_king_needs_refresh(int side, int from, int to) {
    return (nnue_sq_flip(from) != nnue_sq_flip(to)) ||
           (nnue_sq_bucket(side, from) != nnue_sq_bucket(side, to));
}

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
#include "cpu.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define NNUE_ARCH_X86 1
#endif

/* Build with -DNNUE_NO_SIMD to force the scalar reference path (debugging). */
#if defined(NNUE_ARCH_X86) && (defined(__GNUC__) || defined(__clang__)) && !defined(NNUE_NO_SIMD)
#include <immintrin.h>
#if defined(__AVX512BW__)
#define NNUE_USE_AVX512 1            /* compile-time selected, no dispatch   */
#elif defined(UNIVERSAL_BUILD)
#define NNUE_USE_AVX512 1            /* compile AVX-512 kernels via target attr */
#define NNUE_HAVE_AVX2_DISPATCH 1    /* compile AVX2 kernels via target attr    */
#define NNUE_UNIVERSAL 1
#else
#define NNUE_HAVE_AVX2_DISPATCH 1    /* AVX2 if compiled in or detected      */
#endif
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NNUE_UNROLL _Pragma("GCC unroll 16")
#else
#define NNUE_UNROLL
#endif

#if defined(NNUE_USE_AVX512)
#if defined(__AVX512BW__)
#define NNUE_AVX512_FN
#else
#define NNUE_AVX512_FN __attribute__((target("avx512bw,avx512f,avx512vl,avx512dq")))
#endif
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
#define NNUE_REFRESH_THRESHOLD 32

#ifndef NNUE_MIRROR
#define NNUE_MIRROR            1
#endif
#ifndef NNUE_KING_BUCKETS
#define NNUE_KING_BUCKETS      4
#endif

#define NNUE_FT_SIZE           (NNUE_KING_BUCKETS * NNUE_INPUT_SIZE)

/* The fast SCReLU kernel computes (w * v) in int16 before the madd, with
 * v in [0, QA]. That product only fits if |w| <= 128 (128 * 255 = 32640),
 * i.e. |float weight| <= ~2.0. Nets trained without weight clipping can
 * exceed this; they are detected at load time and use the exact kernel. */
#define NNUE_FAST_OUT_W_LIMIT  128

/* Trailing bytes bullet / GOOBNNUE v2 appends to pad the file to a multiple of 64. */
#define NNUE_BULLET_PADDING    48

#define NNUE_TOTAL_SHORTS      (NNUE_FT_SIZE * NNUE_HIDDEN_SIZE + \
                                NNUE_HIDDEN_SIZE + \
                                NNUE_OUTPUT_BUCKETS * (NNUE_HIDDEN_SIZE * 2) + \
                                NNUE_OUTPUT_BUCKETS)

typedef struct {
    ALIGN64 int16_t ft_w[NNUE_FT_SIZE * NNUE_HIDDEN_SIZE];
    ALIGN64 int16_t ft_b[NNUE_HIDDEN_SIZE];
    ALIGN64 int16_t out_w[NNUE_OUTPUT_BUCKETS][NNUE_HIDDEN_SIZE * 2];
    ALIGN64 int16_t out_b[NNUE_OUTPUT_BUCKETS];
} NNUE_Weights;

static NNUE_Weights *g_weights = NULL;
static int g_out_needs_exact = 0;   /* 1 if max |out_w| > NNUE_FAST_OUT_W_LIMIT */
static uint32_t g_nnue_cache_gen = 1;
int nnue_loaded = 0;

void nnue_clear_caches(void) {
    __atomic_add_fetch(&g_nnue_cache_gen, 1u, __ATOMIC_SEQ_CST);
}

extern const unsigned char gEmbeddedNetData[];
extern const unsigned int gEmbeddedNetSize;

static uint16_t s_piece_offset[COLOR_NB][16] = {
    [WHITE] = {
        [wP] = 0,   [wN] = 64,  [wB] = 128, [wR] = 192, [wQ] = 256, [wK] = 320,
        [bP] = 384, [bN] = 448, [bB] = 512, [bR] = 576, [bQ] = 640, [bK] = 704
    },
    [BLACK] = {
        [wP] = 384, [wN] = 448, [wB] = 512, [wR] = 576, [wQ] = 640, [wK] = 704,
        [bP] = 0,   [bN] = 64,  [bB] = 128, [bR] = 192, [bQ] = 256, [bK] = 320
    }
};

static void nnue_init_piece_offset(void) {
    for (int p = 0; p < 16; p++) {
        if (p == EMPTY || TYPE_OF(p) < PAWN || TYPE_OF(p) > KING) {
            s_piece_offset[WHITE][p] = 0;
            s_piece_offset[BLACK][p] = 0;
            continue;
        }
        int c = COLOR_OF(p);
        int pt = PTYPE_OF(p);
        s_piece_offset[WHITE][p] = (uint16_t)(c * 384 + pt * 64);
        s_piece_offset[BLACK][p] = (uint16_t)((c ^ 1) * 384 + pt * 64);
    }
}

/* ── Feature Index Calculation ───────────────────────────────────────────── */
static inline void nnue_king_state(const S_BOARD *pos, int us, uint8_t *out_flip, uint8_t *out_bucket) {
    U64 kbb = pos->byColorBB[us] & pos->byTypeBB[KING];
    if (!kbb) {
        *out_flip = 0;
        *out_bucket = 0;
        return;
    }
    int ksq = LSBINDEX(kbb);
    *out_flip = nnue_sq_flip(ksq);
    *out_bucket = nnue_sq_bucket(us, ksq);
}

static inline size_t nnue_feature_index(int us, int piece, int sq, int flip, int bucket) {
    int s = ((us == WHITE) ? sq : (sq ^ 56)) ^ flip;
    return (size_t)bucket * NNUE_INPUT_SIZE + (size_t)(s_piece_offset[us][piece] + s);
}

static inline const int16_t *nnue_ft_row(int us, int piece, int sq, int flip, int bucket) {
    return g_weights->ft_w + (nnue_feature_index(us, piece, sq, flip, bucket) << 10);
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

NNUE_AVX512_FN
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

NNUE_AVX512_FN
static inline void nnue_acc_1add_1sub_avx512(int16_t *curr, const int16_t *prev,
                                             const int16_t *row_add, const int16_t *row_sub) {
    for (int i = 0; i < NNUE_HIDDEN_SIZE; i += 32) {
        __m512i vp = _mm512_load_si512((const void *)(prev + i));
        __m512i va = _mm512_load_si512((const void *)(row_add + i));
        __m512i vs = _mm512_load_si512((const void *)(row_sub + i));
        _mm512_store_si512((void *)(curr + i), _mm512_sub_epi16(_mm512_add_epi16(vp, va), vs));
    }
}

NNUE_AVX512_FN
static inline void nnue_acc_1add_2sub_avx512(int16_t *curr, const int16_t *prev,
                                             const int16_t *row_add,
                                             const int16_t *row_sub0, const int16_t *row_sub1) {
    for (int base = 0; base < NNUE_HIDDEN_SIZE; base += 32 * NNUE_TILE_REGS_512) {
        __m512i r[NNUE_TILE_REGS_512];
        NNUE_UNROLL
        for (int k = 0; k < NNUE_TILE_REGS_512; k++)
            r[k] = _mm512_load_si512((const void *)(prev + base + 32 * k));
        NNUE_UNROLL
        for (int k = 0; k < NNUE_TILE_REGS_512; k++)
            r[k] = _mm512_add_epi16(r[k], _mm512_load_si512((const void *)(row_add + base + 32 * k)));
        NNUE_UNROLL
        for (int k = 0; k < NNUE_TILE_REGS_512; k++)
            r[k] = _mm512_sub_epi16(r[k], _mm512_load_si512((const void *)(row_sub0 + base + 32 * k)));
        NNUE_UNROLL
        for (int k = 0; k < NNUE_TILE_REGS_512; k++)
            r[k] = _mm512_sub_epi16(r[k], _mm512_load_si512((const void *)(row_sub1 + base + 32 * k)));
        NNUE_UNROLL
        for (int k = 0; k < NNUE_TILE_REGS_512; k++)
            _mm512_store_si512((void *)(curr + base + 32 * k), r[k]);
    }
}

NNUE_AVX512_FN
static inline int32_t nnue_forward_avx512(const int16_t *us, const int16_t *them, int bucket) {
    const __m512i zero = _mm512_setzero_si512();
    const __m512i qa = _mm512_set1_epi16(NNUE_QA);
    const int16_t *w_us = g_weights->out_w[bucket];
    const int16_t *w_them = g_weights->out_w[bucket] + NNUE_HIDDEN_SIZE;
    __m512i sum0 = zero, sum1 = zero, sum2 = zero, sum3 = zero;
    for (int i = 0; i < NNUE_HIDDEN_SIZE; i += 64) {
        __m512i u0 = _mm512_min_epi16(_mm512_max_epi16(_mm512_load_si512((const void *)(us + i)), zero), qa);
        __m512i u1 = _mm512_min_epi16(_mm512_max_epi16(_mm512_load_si512((const void *)(us + i + 32)), zero), qa);
        __m512i t0 = _mm512_min_epi16(_mm512_max_epi16(_mm512_load_si512((const void *)(them + i)), zero), qa);
        __m512i t1 = _mm512_min_epi16(_mm512_max_epi16(_mm512_load_si512((const void *)(them + i + 32)), zero), qa);

        __m512i wu0 = _mm512_load_si512((const void *)(w_us + i));
        __m512i wu1 = _mm512_load_si512((const void *)(w_us + i + 32));
        __m512i wt0 = _mm512_load_si512((const void *)(w_them + i));
        __m512i wt1 = _mm512_load_si512((const void *)(w_them + i + 32));

        sum0 = _mm512_add_epi32(sum0, _mm512_madd_epi16(_mm512_mullo_epi16(wu0, u0), u0));
        sum1 = _mm512_add_epi32(sum1, _mm512_madd_epi16(_mm512_mullo_epi16(wu1, u1), u1));
        sum2 = _mm512_add_epi32(sum2, _mm512_madd_epi16(_mm512_mullo_epi16(wt0, t0), t0));
        sum3 = _mm512_add_epi32(sum3, _mm512_madd_epi16(_mm512_mullo_epi16(wt1, t1), t1));
    }
    __m512i total = _mm512_add_epi32(_mm512_add_epi32(sum0, sum1),
                                     _mm512_add_epi32(sum2, sum3));
    return nnue_finish(_mm512_reduce_add_epi32(total), bucket);
}

/* Exact variant for nets with |out_w| > 128: v^2 (<= 65025) is widened to
 * 32 bits and multiplied by the sign-extended weight. */
NNUE_AVX512_FN
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
            r[k] = _mm256_load_si256((const __m256i *)(src + base + 16 * k));
        for (int a = 0; a < n_add; a++) {
            const int16_t *row = adds[a] + base;
            NNUE_UNROLL
            for (int k = 0; k < NNUE_TILE_REGS_256; k++)
                r[k] = _mm256_add_epi16(r[k], _mm256_load_si256((const __m256i *)(row + 16 * k)));
        }
        for (int s = 0; s < n_sub; s++) {
            const int16_t *row = subs[s] + base;
            NNUE_UNROLL
            for (int k = 0; k < NNUE_TILE_REGS_256; k++)
                r[k] = _mm256_sub_epi16(r[k], _mm256_load_si256((const __m256i *)(row + 16 * k)));
        }
        NNUE_UNROLL
        for (int k = 0; k < NNUE_TILE_REGS_256; k++)
            _mm256_store_si256((__m256i *)(dst + base + 16 * k), r[k]);
    }
}

NNUE_AVX2_FN
static inline void nnue_acc_1add_1sub_avx2(int16_t *curr, const int16_t *prev,
                                           const int16_t *row_add, const int16_t *row_sub) {
    for (int base = 0; base < NNUE_HIDDEN_SIZE; base += 128) {
        __m256i r0 = _mm256_load_si256((const __m256i *)(prev + base + 0));
        __m256i r1 = _mm256_load_si256((const __m256i *)(prev + base + 16));
        __m256i r2 = _mm256_load_si256((const __m256i *)(prev + base + 32));
        __m256i r3 = _mm256_load_si256((const __m256i *)(prev + base + 48));
        __m256i r4 = _mm256_load_si256((const __m256i *)(prev + base + 64));
        __m256i r5 = _mm256_load_si256((const __m256i *)(prev + base + 80));
        __m256i r6 = _mm256_load_si256((const __m256i *)(prev + base + 96));
        __m256i r7 = _mm256_load_si256((const __m256i *)(prev + base + 112));

        r0 = _mm256_add_epi16(r0, _mm256_load_si256((const __m256i *)(row_add + base + 0)));
        r1 = _mm256_add_epi16(r1, _mm256_load_si256((const __m256i *)(row_add + base + 16)));
        r2 = _mm256_add_epi16(r2, _mm256_load_si256((const __m256i *)(row_add + base + 32)));
        r3 = _mm256_add_epi16(r3, _mm256_load_si256((const __m256i *)(row_add + base + 48)));
        r4 = _mm256_add_epi16(r4, _mm256_load_si256((const __m256i *)(row_add + base + 64)));
        r5 = _mm256_add_epi16(r5, _mm256_load_si256((const __m256i *)(row_add + base + 80)));
        r6 = _mm256_add_epi16(r6, _mm256_load_si256((const __m256i *)(row_add + base + 96)));
        r7 = _mm256_add_epi16(r7, _mm256_load_si256((const __m256i *)(row_add + base + 112)));

        r0 = _mm256_sub_epi16(r0, _mm256_load_si256((const __m256i *)(row_sub + base + 0)));
        r1 = _mm256_sub_epi16(r1, _mm256_load_si256((const __m256i *)(row_sub + base + 16)));
        r2 = _mm256_sub_epi16(r2, _mm256_load_si256((const __m256i *)(row_sub + base + 32)));
        r3 = _mm256_sub_epi16(r3, _mm256_load_si256((const __m256i *)(row_sub + base + 48)));
        r4 = _mm256_sub_epi16(r4, _mm256_load_si256((const __m256i *)(row_sub + base + 64)));
        r5 = _mm256_sub_epi16(r5, _mm256_load_si256((const __m256i *)(row_sub + base + 80)));
        r6 = _mm256_sub_epi16(r6, _mm256_load_si256((const __m256i *)(row_sub + base + 96)));
        r7 = _mm256_sub_epi16(r7, _mm256_load_si256((const __m256i *)(row_sub + base + 112)));

        _mm256_store_si256((__m256i *)(curr + base + 0), r0);
        _mm256_store_si256((__m256i *)(curr + base + 16), r1);
        _mm256_store_si256((__m256i *)(curr + base + 32), r2);
        _mm256_store_si256((__m256i *)(curr + base + 48), r3);
        _mm256_store_si256((__m256i *)(curr + base + 64), r4);
        _mm256_store_si256((__m256i *)(curr + base + 80), r5);
        _mm256_store_si256((__m256i *)(curr + base + 96), r6);
        _mm256_store_si256((__m256i *)(curr + base + 112), r7);
    }
}

NNUE_AVX2_FN
static inline void nnue_acc_1add_2sub_avx2(int16_t *curr, const int16_t *prev,
                                           const int16_t *row_add,
                                           const int16_t *row_sub0, const int16_t *row_sub1) {
    for (int base = 0; base < NNUE_HIDDEN_SIZE; base += 128) {
        __m256i r0 = _mm256_load_si256((const __m256i *)(prev + base + 0));
        __m256i r1 = _mm256_load_si256((const __m256i *)(prev + base + 16));
        __m256i r2 = _mm256_load_si256((const __m256i *)(prev + base + 32));
        __m256i r3 = _mm256_load_si256((const __m256i *)(prev + base + 48));
        __m256i r4 = _mm256_load_si256((const __m256i *)(prev + base + 64));
        __m256i r5 = _mm256_load_si256((const __m256i *)(prev + base + 80));
        __m256i r6 = _mm256_load_si256((const __m256i *)(prev + base + 96));
        __m256i r7 = _mm256_load_si256((const __m256i *)(prev + base + 112));

        r0 = _mm256_add_epi16(r0, _mm256_load_si256((const __m256i *)(row_add + base + 0)));
        r1 = _mm256_add_epi16(r1, _mm256_load_si256((const __m256i *)(row_add + base + 16)));
        r2 = _mm256_add_epi16(r2, _mm256_load_si256((const __m256i *)(row_add + base + 32)));
        r3 = _mm256_add_epi16(r3, _mm256_load_si256((const __m256i *)(row_add + base + 48)));
        r4 = _mm256_add_epi16(r4, _mm256_load_si256((const __m256i *)(row_add + base + 64)));
        r5 = _mm256_add_epi16(r5, _mm256_load_si256((const __m256i *)(row_add + base + 80)));
        r6 = _mm256_add_epi16(r6, _mm256_load_si256((const __m256i *)(row_add + base + 96)));
        r7 = _mm256_add_epi16(r7, _mm256_load_si256((const __m256i *)(row_add + base + 112)));

        r0 = _mm256_sub_epi16(r0, _mm256_load_si256((const __m256i *)(row_sub0 + base + 0)));
        r1 = _mm256_sub_epi16(r1, _mm256_load_si256((const __m256i *)(row_sub0 + base + 16)));
        r2 = _mm256_sub_epi16(r2, _mm256_load_si256((const __m256i *)(row_sub0 + base + 32)));
        r3 = _mm256_sub_epi16(r3, _mm256_load_si256((const __m256i *)(row_sub0 + base + 48)));
        r4 = _mm256_sub_epi16(r4, _mm256_load_si256((const __m256i *)(row_sub0 + base + 64)));
        r5 = _mm256_sub_epi16(r5, _mm256_load_si256((const __m256i *)(row_sub0 + base + 80)));
        r6 = _mm256_sub_epi16(r6, _mm256_load_si256((const __m256i *)(row_sub0 + base + 96)));
        r7 = _mm256_sub_epi16(r7, _mm256_load_si256((const __m256i *)(row_sub0 + base + 112)));

        r0 = _mm256_sub_epi16(r0, _mm256_load_si256((const __m256i *)(row_sub1 + base + 0)));
        r1 = _mm256_sub_epi16(r1, _mm256_load_si256((const __m256i *)(row_sub1 + base + 16)));
        r2 = _mm256_sub_epi16(r2, _mm256_load_si256((const __m256i *)(row_sub1 + base + 32)));
        r3 = _mm256_sub_epi16(r3, _mm256_load_si256((const __m256i *)(row_sub1 + base + 48)));
        r4 = _mm256_sub_epi16(r4, _mm256_load_si256((const __m256i *)(row_sub1 + base + 64)));
        r5 = _mm256_sub_epi16(r5, _mm256_load_si256((const __m256i *)(row_sub1 + base + 80)));
        r6 = _mm256_sub_epi16(r6, _mm256_load_si256((const __m256i *)(row_sub1 + base + 96)));
        r7 = _mm256_sub_epi16(r7, _mm256_load_si256((const __m256i *)(row_sub1 + base + 112)));

        _mm256_store_si256((__m256i *)(curr + base + 0), r0);
        _mm256_store_si256((__m256i *)(curr + base + 16), r1);
        _mm256_store_si256((__m256i *)(curr + base + 32), r2);
        _mm256_store_si256((__m256i *)(curr + base + 48), r3);
        _mm256_store_si256((__m256i *)(curr + base + 64), r4);
        _mm256_store_si256((__m256i *)(curr + base + 80), r5);
        _mm256_store_si256((__m256i *)(curr + base + 96), r6);
        _mm256_store_si256((__m256i *)(curr + base + 112), r7);
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
    __m256i sum0 = zero, sum1 = zero, sum2 = zero, sum3 = zero;

    for (int i = 0; i < NNUE_HIDDEN_SIZE; i += 32) {
        __m256i u0 = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i *)(us + i)), zero), qa);
        __m256i u1 = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i *)(us + i + 16)), zero), qa);
        __m256i t0 = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i *)(them + i)), zero), qa);
        __m256i t1 = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i *)(them + i + 16)), zero), qa);

        __m256i wu0 = _mm256_load_si256((const __m256i *)(w_us + i));
        __m256i wu1 = _mm256_load_si256((const __m256i *)(w_us + i + 16));
        __m256i wt0 = _mm256_load_si256((const __m256i *)(w_them + i));
        __m256i wt1 = _mm256_load_si256((const __m256i *)(w_them + i + 16));

        sum0 = _mm256_add_epi32(sum0, _mm256_madd_epi16(_mm256_mullo_epi16(wu0, u0), u0));
        sum1 = _mm256_add_epi32(sum1, _mm256_madd_epi16(_mm256_mullo_epi16(wu1, u1), u1));
        sum2 = _mm256_add_epi32(sum2, _mm256_madd_epi16(_mm256_mullo_epi16(wt0, t0), t0));
        sum3 = _mm256_add_epi32(sum3, _mm256_madd_epi16(_mm256_mullo_epi16(wt1, t1), t1));
    }
    __m256i total = _mm256_add_epi32(_mm256_add_epi32(sum0, sum1),
                                     _mm256_add_epi32(sum2, sum3));
    return nnue_finish(nnue_hsum_epi32_avx2(total), bucket);
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

static inline void nnue_acc_1add_2sub_scalar(int16_t *curr, const int16_t *prev,
                                             const int16_t *row_add,
                                             const int16_t *row_sub0, const int16_t *row_sub1) {
    for (int i = 0; i < NNUE_HIDDEN_SIZE; i++)
        curr[i] = (int16_t)(prev[i] + row_add[i] - row_sub0[i] - row_sub1[i]);
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

#if defined(UNIVERSAL_BUILD)
typedef void (*nnue_acc_apply_fn)(int16_t *dst, const int16_t *src,
                                  const int16_t *const *adds, int n_add,
                                  const int16_t *const *subs, int n_sub);
typedef void (*nnue_acc_1add_1sub_fn)(int16_t *curr, const int16_t *prev,
                                      const int16_t *row_add, const int16_t *row_sub);
typedef void (*nnue_acc_1add_2sub_fn)(int16_t *curr, const int16_t *prev,
                                      const int16_t *row_add,
                                      const int16_t *row_sub0, const int16_t *row_sub1);
typedef int32_t (*nnue_forward_fn)(const int16_t *us, const int16_t *them, int bucket);

static nnue_acc_apply_fn    s_acc_apply    = nnue_acc_apply_scalar;
static nnue_acc_1add_1sub_fn s_acc_1add_1sub = nnue_acc_1add_1sub_scalar;
static nnue_acc_1add_2sub_fn s_acc_1add_2sub = nnue_acc_1add_2sub_scalar;
static nnue_forward_fn      s_forward      = nnue_forward_scalar;
#endif

/* ── Dispatchers ─────────────────────────────────────────────────────────── */
static inline void nnue_acc_apply(int16_t *dst, const int16_t *src,
                                  const int16_t *const *adds, int n_add,
                                  const int16_t *const *subs, int n_sub) {
#if defined(UNIVERSAL_BUILD)
    s_acc_apply(dst, src, adds, n_add, subs, n_sub);
#elif defined(NNUE_USE_AVX512)
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
#if defined(UNIVERSAL_BUILD)
    s_acc_1add_1sub(curr, prev, row_add, row_sub);
#elif defined(NNUE_USE_AVX512)
    nnue_acc_1add_1sub_avx512(curr, prev, row_add, row_sub);
#else
#if defined(NNUE_HAVE_AVX2_DISPATCH)
    if (cpu_supports_avx2()) { nnue_acc_1add_1sub_avx2(curr, prev, row_add, row_sub); return; }
#endif
    nnue_acc_1add_1sub_scalar(curr, prev, row_add, row_sub);
#endif
}

static inline void nnue_acc_1add_2sub(int16_t *curr, const int16_t *prev,
                                      const int16_t *row_add,
                                      const int16_t *row_sub0, const int16_t *row_sub1) {
#if defined(UNIVERSAL_BUILD)
    s_acc_1add_2sub(curr, prev, row_add, row_sub0, row_sub1);
#elif defined(NNUE_USE_AVX512)
    nnue_acc_1add_2sub_avx512(curr, prev, row_add, row_sub0, row_sub1);
#else
#if defined(NNUE_HAVE_AVX2_DISPATCH)
    if (cpu_supports_avx2()) { nnue_acc_1add_2sub_avx2(curr, prev, row_add, row_sub0, row_sub1); return; }
#endif
    nnue_acc_1add_2sub_scalar(curr, prev, row_add, row_sub0, row_sub1);
#endif
}

static inline int32_t nnue_forward(const int16_t *us, const int16_t *them, int bucket) {
#if defined(UNIVERSAL_BUILD)
    return s_forward(us, them, bucket);
#elif defined(NNUE_USE_AVX512)
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

/* ── Perspective Refresh (Finny Table Cache + Fresh Fallback) ────────────── */
static void nnue_refresh_perspective_fresh(const S_BOARD *pos, int us, int16_t *acc,
                                           uint8_t *out_flip, uint8_t *out_bkt) {
    const int16_t *rows[64];
    int n = 0;
    uint8_t flip, bkt;
    nnue_king_state(pos, us, &flip, &bkt);
    if (out_flip) *out_flip = flip;
    if (out_bkt)  *out_bkt  = bkt;
    U64 pieces = pos->byTypeBB[ALL_PIECES];
    while (pieces) {
        int sq = poplsb(&pieces);
        int p = pos->pieces[sq];
        rows[n++] = nnue_ft_row(us, p, sq, flip, bkt);
    }
    nnue_acc_apply(acc, g_weights->ft_b, rows, n, NULL, 0);
}

static void nnue_refresh_both_fresh(const S_BOARD *pos, int16_t *acc_w, int16_t *acc_b,
                                    uint8_t *flip_w, uint8_t *flip_b,
                                    uint8_t *bkt_w, uint8_t *bkt_b) {
    const int16_t *rows_w[64];
    const int16_t *rows_b[64];
    int n = 0;
    uint8_t fw, fb, bw, bb;
    nnue_king_state(pos, WHITE, &fw, &bw);
    nnue_king_state(pos, BLACK, &fb, &bb);
    if (flip_w) *flip_w = fw;
    if (flip_b) *flip_b = fb;
    if (bkt_w)  *bkt_w  = bw;
    if (bkt_b)  *bkt_b  = bb;
    U64 pieces = pos->byTypeBB[ALL_PIECES];
    while (pieces) {
        int sq = poplsb(&pieces);
        int p = pos->pieces[sq];
        rows_w[n] = nnue_ft_row(WHITE, p, sq, fw, bw);
        rows_b[n] = nnue_ft_row(BLACK, p, sq, fb, bb);
        n++;
    }
    nnue_acc_apply(acc_w, g_weights->ft_b, rows_w, n, NULL, 0);
    nnue_acc_apply(acc_b, g_weights->ft_b, rows_b, n, NULL, 0);
}

static void nnue_refresh_perspective(S_BOARD *pos, int us, int16_t *acc,
                                     uint8_t *out_flip, uint8_t *out_bkt) {
    if (!pos->search) {
        nnue_refresh_perspective_fresh(pos, us, acc, out_flip, out_bkt);
        return;
    }

    uint8_t flip, bkt;
    nnue_king_state(pos, us, &flip, &bkt);
    if (out_flip) *out_flip = flip;
    if (out_bkt)  *out_bkt  = bkt;

    NNUE_CacheEntry *e = &pos->search->nnue_cache[us][flip != 0][bkt];
    if (e->gen != g_nnue_cache_gen) {
        memcpy(e->acc, g_weights->ft_b, NNUE_HIDDEN_SIZE * sizeof(int16_t));
        memset(e->pieceBB, 0, sizeof(e->pieceBB));
        e->gen = g_nnue_cache_gen;
    }

    const int16_t *adds[32];
    const int16_t *subs[32];
    int na_total = 0, ns_total = 0;

    for (int c = WHITE; c <= BLACK; c++) {
        U64 color_bb = pos->byColorBB[c];
        for (int pt = PAWN; pt <= KING; pt++) {
            U64 cur  = color_bb & pos->byTypeBB[pt];
            U64 prev = e->pieceBB[c][pt];
            if (cur == prev) continue;

            int p = (c == WHITE) ? pt : (pt + 8);
            U64 added   = cur & ~prev;
            U64 removed = prev & ~cur;
            while (added) {
                int sq = poplsb(&added);
                adds[na_total++] = nnue_ft_row(us, p, sq, flip, bkt);
            }
            while (removed) {
                int sq = poplsb(&removed);
                subs[ns_total++] = nnue_ft_row(us, p, sq, flip, bkt);
            }
            e->pieceBB[c][pt] = cur;
        }
    }

    int a = 0, s = 0;
    while (a < na_total || s < ns_total) {
        int na = (na_total - a > 4) ? 4 : (na_total - a);
        int ns = (ns_total - s > 4) ? 4 : (ns_total - s);
        nnue_acc_apply(e->acc, e->acc, adds + a, na, subs + s, ns);
        a += na;
        s += ns;
    }

    memcpy(acc, e->acc, NNUE_HIDDEN_SIZE * sizeof(int16_t));
}

static void nnue_refresh_both(S_BOARD *pos, int16_t *acc_w, int16_t *acc_b,
                              uint8_t *flip_w, uint8_t *flip_b,
                              uint8_t *bkt_w, uint8_t *bkt_b) {
    if (!pos->search) {
        nnue_refresh_both_fresh(pos, acc_w, acc_b, flip_w, flip_b, bkt_w, bkt_b);
        return;
    }
    nnue_refresh_perspective(pos, WHITE, acc_w, flip_w, bkt_w);
    nnue_refresh_perspective(pos, BLACK, acc_b, flip_b, bkt_b);
}

void nnue_refresh_accumulator(S_BOARD *pos) {
    if (!nnue_loaded || !pos->search) return;
    int ply = pos->ply;
    /* Plies outside the stack are evaluated fresh by nnue_eval; writing them
     * into the last slot (as before) could leave a wrong accumulator marked
     * as computed for the position that really lives at MAXDEPTH - 1. */
    if (ply < 0 || ply >= MAXDEPTH) return;
    NNUE_Accumulator *acc = &pos->search->nnue_accumulators[ply];
    nnue_refresh_both(pos,
                      acc->accumulation[WHITE],
                      acc->accumulation[BLACK],
                      &acc->flip[WHITE],
                      &acc->flip[BLACK],
                      &acc->bucket[WHITE],
                      &acc->bucket[BLACK]);
    acc->computed[WHITE] = 1;
    acc->computed[BLACK] = 1;
}

/* ── Incremental Move Step ───────────────────────────────────────────────── */
static void nnue_update_accumulator_step(int us, int flip, int bkt,
                                         const int16_t *prev_acc,
                                         int16_t *curr_acc,
                                         const DirtyPiece *dp) {
    if (dp->remove_count == 1 && dp->add_count == 1) {
        nnue_acc_1add_1sub(curr_acc, prev_acc,
                           nnue_ft_row(us, dp->piece_add[0], dp->to[0], flip, bkt),
                           nnue_ft_row(us, dp->piece_remove[0], dp->from[0], flip, bkt));
        return;
    }
    if (dp->remove_count == 2 && dp->add_count == 1) {
        nnue_acc_1add_2sub(curr_acc, prev_acc,
                           nnue_ft_row(us, dp->piece_add[0], dp->to[0], flip, bkt),
                           nnue_ft_row(us, dp->piece_remove[0], dp->from[0], flip, bkt),
                           nnue_ft_row(us, dp->piece_remove[1], dp->from[1], flip, bkt));
        return;
    }
    if (dp->remove_count == 0 && dp->add_count == 0) {
        memcpy(curr_acc, prev_acc, NNUE_HIDDEN_SIZE * sizeof(int16_t));
        return;
    }

    /* Castling (2/2) and any other count: one tiled pass per group of up to 4 adds / 4 subs. */
    const int16_t *adds[4], *subs[4];
    const int16_t *src = prev_acc;
    int a = 0, r = 0;
    do {
        int na = 0, nr = 0;
        while (a < dp->add_count && na < 4) {
            adds[na++] = nnue_ft_row(us, dp->piece_add[a], dp->to[a], flip, bkt);
            a++;
        }
        while (r < dp->remove_count && nr < 4) {
            subs[nr++] = nnue_ft_row(us, dp->piece_remove[r], dp->from[r], flip, bkt);
            r++;
        }
        nnue_acc_apply(curr_acc, src, adds, na, subs, nr);
        src = curr_acc;
    } while (a < dp->add_count || r < dp->remove_count);
}

static void nnue_update_accumulator_step_both(int flip_w, int bkt_w,
                                              int flip_b, int bkt_b,
                                              const int16_t *prev_w, int16_t *curr_w,
                                              const int16_t *prev_b, int16_t *curr_b,
                                              const DirtyPiece *dp) {
    if (dp->remove_count == 1 && dp->add_count == 1) {
        nnue_acc_1add_1sub(curr_w, prev_w,
                           nnue_ft_row(WHITE, dp->piece_add[0], dp->to[0], flip_w, bkt_w),
                           nnue_ft_row(WHITE, dp->piece_remove[0], dp->from[0], flip_w, bkt_w));
        nnue_acc_1add_1sub(curr_b, prev_b,
                           nnue_ft_row(BLACK, dp->piece_add[0], dp->to[0], flip_b, bkt_b),
                           nnue_ft_row(BLACK, dp->piece_remove[0], dp->from[0], flip_b, bkt_b));
        return;
    }
    if (dp->remove_count == 2 && dp->add_count == 1) {
        nnue_acc_1add_2sub(curr_w, prev_w,
                           nnue_ft_row(WHITE, dp->piece_add[0], dp->to[0], flip_w, bkt_w),
                           nnue_ft_row(WHITE, dp->piece_remove[0], dp->from[0], flip_w, bkt_w),
                           nnue_ft_row(WHITE, dp->piece_remove[1], dp->from[1], flip_w, bkt_w));
        nnue_acc_1add_2sub(curr_b, prev_b,
                           nnue_ft_row(BLACK, dp->piece_add[0], dp->to[0], flip_b, bkt_b),
                           nnue_ft_row(BLACK, dp->piece_remove[0], dp->from[0], flip_b, bkt_b),
                           nnue_ft_row(BLACK, dp->piece_remove[1], dp->from[1], flip_b, bkt_b));
        return;
    }
    if (dp->remove_count == 0 && dp->add_count == 0) {
        memcpy(curr_w, prev_w, NNUE_HIDDEN_SIZE * sizeof(int16_t));
        memcpy(curr_b, prev_b, NNUE_HIDDEN_SIZE * sizeof(int16_t));
        return;
    }

    nnue_update_accumulator_step(WHITE, flip_w, bkt_w, prev_w, curr_w, dp);
    nnue_update_accumulator_step(BLACK, flip_b, bkt_b, prev_b, curr_b, dp);
}

static inline void nnue_trace_mirror_refresh(S_BOARD *pos) {
#ifdef TRACE
    if (COUNTBIT(pos->byTypeBB[ALL_PIECES]) > 12) {
        TRACE_INC(pos, nnue_refresh_mg);
    } else {
        TRACE_INC(pos, nnue_refresh_eg);
    }
#else
    (void)pos;
#endif
}

/* ── Lazy Accumulator Multi-Ply Traversal ────────────────────────────────── */
static void nnue_update_accumulators_to_ply(S_BOARD *pos, int target_ply) {
    NNUE_Accumulator *accs = pos->search->nnue_accumulators;
    if (accs[target_ply].computed[WHITE] && accs[target_ply].computed[BLACK]) {
        return;
    }

    int ancestor[COLOR_NB]     = { -1, -1 };
    int need_update[COLOR_NB]  = { !accs[target_ply].computed[WHITE], !accs[target_ply].computed[BLACK] };
    int need_refresh[COLOR_NB] = { 0, 0 };
    int crossed[COLOR_NB]      = { 0, 0 };

    for (int c = WHITE; c <= BLACK; c++) {
        if (!need_update[c]) continue;
        for (int p = target_ply; p > 0; p--) {
#if NNUE_MIRROR || (NNUE_KING_BUCKETS > 1)
            if (pos->search->dirtyPieces[p].refresh[c]) {
                crossed[c] = 1;
                break;
            }
#endif
            if (accs[p - 1].computed[c]) {
                ancestor[c] = p - 1;
                break;
            }
        }
        if (crossed[c] || ancestor[c] < 0 || (target_ply - ancestor[c] > NNUE_REFRESH_THRESHOLD)) {
            need_refresh[c] = 1;
        }
    }

    /* Both perspectives need a refresh at target_ply */
    if (need_update[WHITE] && need_update[BLACK] && need_refresh[WHITE] && need_refresh[BLACK]) {
        if (crossed[WHITE]) nnue_trace_mirror_refresh(pos);
        if (crossed[BLACK]) nnue_trace_mirror_refresh(pos);
        nnue_refresh_both(pos,
                          accs[target_ply].accumulation[WHITE],
                          accs[target_ply].accumulation[BLACK],
                          &accs[target_ply].flip[WHITE],
                          &accs[target_ply].flip[BLACK],
                          &accs[target_ply].bucket[WHITE],
                          &accs[target_ply].bucket[BLACK]);
        accs[target_ply].computed[WHITE] = 1;
        accs[target_ply].computed[BLACK] = 1;
        return;
    }

    /* Fast path: both perspectives can be incrementally updated from the same ancestor */
    if (need_update[WHITE] && need_update[BLACK] &&
        !need_refresh[WHITE] && !need_refresh[BLACK] &&
        ancestor[WHITE] == ancestor[BLACK]) {
        uint8_t fw = accs[ancestor[WHITE]].flip[WHITE];
        uint8_t fb = accs[ancestor[BLACK]].flip[BLACK];
        uint8_t bw = accs[ancestor[WHITE]].bucket[WHITE];
        uint8_t bb = accs[ancestor[BLACK]].bucket[BLACK];
        for (int step = ancestor[WHITE] + 1; step <= target_ply; step++) {
            if (!accs[step].computed[WHITE] && !accs[step].computed[BLACK]) {
                nnue_update_accumulator_step_both(
                    fw, bw, fb, bb,
                    accs[step - 1].accumulation[WHITE],
                    accs[step].accumulation[WHITE],
                    accs[step - 1].accumulation[BLACK],
                    accs[step].accumulation[BLACK],
                    &pos->search->dirtyPieces[step]);
                accs[step].flip[WHITE]   = fw;
                accs[step].flip[BLACK]   = fb;
                accs[step].bucket[WHITE] = bw;
                accs[step].bucket[BLACK] = bb;
                accs[step].computed[WHITE] = 1;
                accs[step].computed[BLACK] = 1;
            } else {
                if (!accs[step].computed[WHITE]) {
                    nnue_update_accumulator_step(WHITE, fw, bw,
                                                 accs[step - 1].accumulation[WHITE],
                                                 accs[step].accumulation[WHITE],
                                                 &pos->search->dirtyPieces[step]);
                    accs[step].flip[WHITE]   = fw;
                    accs[step].bucket[WHITE] = bw;
                    accs[step].computed[WHITE] = 1;
                }
                if (!accs[step].computed[BLACK]) {
                    nnue_update_accumulator_step(BLACK, fb, bb,
                                                 accs[step - 1].accumulation[BLACK],
                                                 accs[step].accumulation[BLACK],
                                                 &pos->search->dirtyPieces[step]);
                    accs[step].flip[BLACK]   = fb;
                    accs[step].bucket[BLACK] = bb;
                    accs[step].computed[BLACK] = 1;
                }
            }
        }
        return;
    }

    /* Per-perspective path (when one perspective crossed a boundary or ancestors differ) */
    for (int c = WHITE; c <= BLACK; c++) {
        if (!need_update[c]) continue;
        if (need_refresh[c]) {
            if (crossed[c]) nnue_trace_mirror_refresh(pos);
            nnue_refresh_perspective(pos, c,
                                     accs[target_ply].accumulation[c],
                                     &accs[target_ply].flip[c],
                                     &accs[target_ply].bucket[c]);
            accs[target_ply].computed[c] = 1;
        } else {
            uint8_t fc = accs[ancestor[c]].flip[c];
            uint8_t bc = accs[ancestor[c]].bucket[c];
            for (int step = ancestor[c] + 1; step <= target_ply; step++) {
                if (!accs[step].computed[c]) {
                    nnue_update_accumulator_step(c, fc, bc,
                                                 accs[step - 1].accumulation[c],
                                                 accs[step].accumulation[c],
                                                 &pos->search->dirtyPieces[step]);
                    accs[step].flip[c]   = fc;
                    accs[step].bucket[c] = bc;
                    accs[step].computed[c] = 1;
                }
            }
        }
    }
}

/* ── Evaluation ──────────────────────────────────────────────────────────── */
static inline int nnue_output_bucket(const S_BOARD *pos) {
    int pieces = COUNTBIT(pos->byTypeBB[ALL_PIECES]);
    int bucket = (pieces - 2) >> 2;
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
        nnue_refresh_both_fresh(pos, acc[WHITE], acc[BLACK], NULL, NULL, NULL, NULL);
        return nnue_forward(acc[stm], acc[other], bucket);
    }

    nnue_update_accumulators_to_ply(pos, ply);

    const int16_t *acc_stm = pos->search->nnue_accumulators[ply].accumulation[stm];
    const int16_t *acc_other = pos->search->nnue_accumulators[ply].accumulation[other];
    return nnue_forward(acc_stm, acc_other, bucket);
}

/* ── Weight Loading & Initialization ─────────────────────────────────────── */
static void *nnue_aligned_alloc(size_t align, size_t size) {
    return goob_aligned_alloc(align, size);
}

static void nnue_aligned_free(void *p) {
    goob_aligned_free(p);
}

typedef struct {
    uint32_t version;
    uint32_t mirror;
    uint32_t king_buckets;
    uint32_t hidden_size;
    uint32_t output_buckets;
    int      tagged;
} NNUE_TrailerInfo;

static void nnue_parse_trailer(const unsigned char *buf, size_t len, NNUE_TrailerInfo *info) {
    info->tagged         = 0;
    info->version        = 1;
    info->mirror         = 0;
    info->king_buckets   = 1;
    info->hidden_size    = 1024;
    info->output_buckets = 8;
    if (len >= NNUE_BULLET_PADDING && memcmp(buf, "GOOBNNUE", 8) == 0) {
        uint32_t fields[5];
        memcpy(fields, buf + 8, sizeof(fields));
        info->tagged         = 1;
        info->version        = fields[0];
        info->mirror         = fields[1] & 1u;
        info->king_buckets   = fields[2];
        info->hidden_size    = fields[3];
        info->output_buckets = fields[4];
    }
}

static int nnue_validate_trailer(const unsigned char *buf, size_t len, const char *opened) {
    NNUE_TrailerInfo info;
    nnue_parse_trailer(buf, len, &info);
    if (info.mirror != (uint32_t)NNUE_MIRROR ||
        info.king_buckets != (uint32_t)NNUE_KING_BUCKETS ||
        info.hidden_size != (uint32_t)NNUE_HIDDEN_SIZE ||
        info.output_buckets != (uint32_t)NNUE_OUTPUT_BUCKETS) {
        printf("info string NNUE error: architecture mismatch in '%s': "
               "expected (mirror=%d, king_buckets=%d, hidden=%d, out_buckets=%d), "
               "found %s (mirror=%u, king_buckets=%u, hidden=%u, out_buckets=%u)\n",
               opened,
               NNUE_MIRROR, NNUE_KING_BUCKETS, NNUE_HIDDEN_SIZE, NNUE_OUTPUT_BUCKETS,
               info.tagged ? "GOOBNNUE tag" : "legacy/untagged",
               info.mirror, info.king_buckets, info.hidden_size, info.output_buckets);
        fflush(stdout);
        return 0;
    }
    return 1;
}

int nnue_init(const char *path) {
    NNUE_Weights *w = NULL;
    const char *opened = NULL;

    if (path && path[0] && strcmp(path, "<empty>") != 0 && strcmp(path, "default") != 0) {
        FILE *f = fopen(path, "rb");
        if (f) {
            opened = path;
            w = (NNUE_Weights *)nnue_aligned_alloc(64, sizeof(NNUE_Weights));
            if (!w) {
                fclose(f);
                return 0;
            }

            size_t read = 0;
            read += fread(w->ft_w, sizeof(int16_t), NNUE_FT_SIZE * NNUE_HIDDEN_SIZE, f);
            read += fread(w->ft_b, sizeof(int16_t), NNUE_HIDDEN_SIZE, f);
            read += fread(w->out_w, sizeof(int16_t), NNUE_OUTPUT_BUCKETS * NNUE_HIDDEN_SIZE * 2, f);
            read += fread(w->out_b, sizeof(int16_t), NNUE_OUTPUT_BUCKETS, f);

            unsigned char trailer[NNUE_BULLET_PADDING];
            memset(trailer, 0, sizeof(trailer));
            long extra = 0;
            size_t tlen = 0;
            {
                long here = ftell(f);
                tlen = fread(trailer, 1, NNUE_BULLET_PADDING, f);
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
            if (!nnue_validate_trailer(trailer, tlen, opened)) {
                nnue_aligned_free(w);
                return 0;
            }
        } else {
            printf("info string NNUE: cannot open '%s'\n", path);
            fflush(stdout);
            return 0;
        }
    } else {
        /* No path specified, or "<empty>" / "default": use embedded weights */
        size_t payload_bytes = (size_t)NNUE_TOTAL_SHORTS * sizeof(int16_t);
        if (gEmbeddedNetSize < payload_bytes) {
            printf("info string NNUE error: embedded network is missing or too small (%u bytes)\n",
                   gEmbeddedNetSize);
            fflush(stdout);
            return 0;
        }
        opened = "embedded network (weights/quantised.bin)";
        size_t tlen = (gEmbeddedNetSize > payload_bytes) ? (gEmbeddedNetSize - payload_bytes) : 0;
        if (!nnue_validate_trailer(gEmbeddedNetData + payload_bytes, tlen, opened)) {
            return 0;
        }
        w = (NNUE_Weights *)nnue_aligned_alloc(64, sizeof(NNUE_Weights));
        if (!w) {
            printf("info string NNUE error: memory allocation failed\n");
            fflush(stdout);
            return 0;
        }
        memcpy(w, gEmbeddedNetData, sizeof(NNUE_Weights));
    }

    int max_abs_out = 0;
    for (int b = 0; b < NNUE_OUTPUT_BUCKETS; b++) {
        for (int i = 0; i < 2 * NNUE_HIDDEN_SIZE; i++) {
            int v = w->out_w[b][i];
            if (v < 0) v = -v;
            if (v > max_abs_out) max_abs_out = v;
        }
    }

    nnue_init_piece_offset();

    NNUE_Weights *old = g_weights;
    g_weights = w;
    g_out_needs_exact = max_abs_out > NNUE_FAST_OUT_W_LIMIT;
    nnue_loaded = 1;
    nnue_clear_caches();
#if defined(UNIVERSAL_BUILD)
    if (g_cpu_tier == ARCH_AVX512) {
        s_acc_apply     = nnue_acc_apply_avx512;
        s_acc_1add_1sub = nnue_acc_1add_1sub_avx512;
        s_acc_1add_2sub = nnue_acc_1add_2sub_avx512;
        s_forward       = g_out_needs_exact ? nnue_forward_exact_avx512 : nnue_forward_avx512;
    } else if (g_cpu_tier == ARCH_AVX2 || g_cpu_tier == ARCH_AVX2_BMI2) {
        s_acc_apply     = nnue_acc_apply_avx2;
        s_acc_1add_1sub = nnue_acc_1add_1sub_avx2;
        s_acc_1add_2sub = nnue_acc_1add_2sub_avx2;
        s_forward       = g_out_needs_exact ? nnue_forward_exact_avx2 : nnue_forward_avx2;
    } else {
        s_acc_apply     = nnue_acc_apply_scalar;
        s_acc_1add_1sub = nnue_acc_1add_1sub_scalar;
        s_acc_1add_2sub = nnue_acc_1add_2sub_scalar;
        s_forward       = nnue_forward_scalar;
    }
#endif
    if (old) nnue_aligned_free(old);

    printf("info string NNUE: loaded %s (mirror=%d, king_buckets=%d)\n",
           opened, NNUE_MIRROR, NNUE_KING_BUCKETS);
    if (g_out_needs_exact) {
        printf("info string NNUE: output weights reach %d (fast kernel needs <= %d); "
               "using the exact kernel. Retrain with weight clipping for full speed.\n",
               max_abs_out, NNUE_FAST_OUT_W_LIMIT);
    }
    fflush(stdout);
    return 1;
}

#endif /* NNUE_IMPLEMENTATION */