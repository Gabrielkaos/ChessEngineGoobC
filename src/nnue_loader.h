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
 *   - Fully quantized integer forward pass (AVX2 SIMD):
 *       eval = sum(screlu * weight) / QA + bias
 *       eval = eval * scale / (QA * QB)   [scale=400, QA=255, QB=64]
 *   - Memory footprint: ~1.6 MB (803,848 int16 values = 1,607,696 bytes).
 *     Fits entirely into CPU L3 cache with zero DRAM thrashing.
 */

#ifndef NNUE_LOADER_H
#define NNUE_LOADER_H

#include "defs.h"
#include "board.h"
#include "bitboards.h"

/* ── Public API ──────────────────────────────────────────────────────────── */

/* Load weights from binary file. Returns 1 on success, 0 on failure.
 * If path is NULL or "<empty>", checks default candidate paths. */
int nnue_init(const char *path);

/* Full rebuild of pos->search->nnue_accumulators[ply] for both perspectives. */
void nnue_refresh_accumulator(S_BOARD *pos);

/* Compatibility hooks for move updates (search uses dirtyPieces) */
void nnue_update_add(S_BOARD *pos, int piece, int sq);
void nnue_update_remove(S_BOARD *pos, int piece, int sq);
void nnue_update_move(S_BOARD *pos, int piece, int from, int to);

/* Evaluate board position using NNUE.
 * Returns centipawns from the SIDE-TO-MOVE's perspective. */
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

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define NNUE_ARCH_X86 1
#endif

#if defined(NNUE_ARCH_X86) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define NNUE_HAVE_AVX2_DISPATCH 1
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

/* ── SIMD / Scalar Accumulator Update Kernels ────────────────────────────── */

#if defined(NNUE_HAVE_AVX2_DISPATCH)
__attribute__((target("avx2")))
static void nnue_acc_add_row_avx2(int16_t *acc, const int16_t *row, int n) {
    for (int i = 0; i < n; i += 16) {
        __m256i va = _mm256_load_si256((const __m256i *)(acc + i));
        __m256i vr = _mm256_loadu_si256((const __m256i *)(row + i));
        _mm256_store_si256((__m256i *)(acc + i), _mm256_add_epi16(va, vr));
    }
}

__attribute__((target("avx2")))
static void nnue_acc_sub_row_avx2(int16_t *acc, const int16_t *row, int n) {
    for (int i = 0; i < n; i += 16) {
        __m256i va = _mm256_load_si256((const __m256i *)(acc + i));
        __m256i vr = _mm256_loadu_si256((const __m256i *)(row + i));
        _mm256_store_si256((__m256i *)(acc + i), _mm256_sub_epi16(va, vr));
    }
}

__attribute__((target("avx2")))
static void nnue_acc_update_1add_1sub_avx2(int16_t *curr, const int16_t *prev,
                                          const int16_t *row_add, const int16_t *row_sub, int n) {
    for (int i = 0; i < n; i += 16) {
        __m256i vp = _mm256_load_si256((const __m256i *)(prev + i));
        __m256i va = _mm256_loadu_si256((const __m256i *)(row_add + i));
        __m256i vs = _mm256_loadu_si256((const __m256i *)(row_sub + i));
        __m256i res = _mm256_sub_epi16(_mm256_add_epi16(vp, va), vs);
        _mm256_store_si256((__m256i *)(curr + i), res);
    }
}

__attribute__((target("avx2")))
static void nnue_acc_update_1add_2sub_avx2(int16_t *curr, const int16_t *prev,
                                          const int16_t *row_add,
                                          const int16_t *row_sub0, const int16_t *row_sub1, int n) {
    for (int i = 0; i < n; i += 16) {
        __m256i vp = _mm256_load_si256((const __m256i *)(prev + i));
        __m256i va = _mm256_loadu_si256((const __m256i *)(row_add + i));
        __m256i vs0 = _mm256_loadu_si256((const __m256i *)(row_sub0 + i));
        __m256i vs1 = _mm256_loadu_si256((const __m256i *)(row_sub1 + i));
        __m256i res = _mm256_sub_epi16(_mm256_sub_epi16(_mm256_add_epi16(vp, va), vs0), vs1);
        _mm256_store_si256((__m256i *)(curr + i), res);
    }
}

__attribute__((target("avx2")))
static void nnue_acc_update_2add_2sub_avx2(int16_t *curr, const int16_t *prev,
                                          const int16_t *row_add0, const int16_t *row_add1,
                                          const int16_t *row_sub0, const int16_t *row_sub1, int n) {
    for (int i = 0; i < n; i += 16) {
        __m256i vp = _mm256_load_si256((const __m256i *)(prev + i));
        __m256i va0 = _mm256_loadu_si256((const __m256i *)(row_add0 + i));
        __m256i va1 = _mm256_loadu_si256((const __m256i *)(row_add1 + i));
        __m256i vs0 = _mm256_loadu_si256((const __m256i *)(row_sub0 + i));
        __m256i vs1 = _mm256_loadu_si256((const __m256i *)(row_sub1 + i));
        __m256i res = _mm256_add_epi16(_mm256_add_epi16(vp, va0), va1);
        res = _mm256_sub_epi16(_mm256_sub_epi16(res, vs0), vs1);
        _mm256_store_si256((__m256i *)(curr + i), res);
    }
}
#endif

static void nnue_acc_add_row_scalar(int16_t *acc, const int16_t *row, int n) {
    for (int i = 0; i < n; i++) acc[i] += row[i];
}

static void nnue_acc_sub_row_scalar(int16_t *acc, const int16_t *row, int n) {
    for (int i = 0; i < n; i++) acc[i] -= row[i];
}

static void nnue_acc_update_1add_1sub_scalar(int16_t *curr, const int16_t *prev,
                                            const int16_t *row_add, const int16_t *row_sub, int n) {
    for (int i = 0; i < n; i++) curr[i] = prev[i] + row_add[i] - row_sub[i];
}

static void nnue_acc_update_1add_2sub_scalar(int16_t *curr, const int16_t *prev,
                                            const int16_t *row_add,
                                            const int16_t *row_sub0, const int16_t *row_sub1, int n) {
    for (int i = 0; i < n; i++) curr[i] = prev[i] + row_add[i] - row_sub0[i] - row_sub1[i];
}

static void nnue_acc_update_2add_2sub_scalar(int16_t *curr, const int16_t *prev,
                                            const int16_t *row_add0, const int16_t *row_add1,
                                            const int16_t *row_sub0, const int16_t *row_sub1, int n) {
    for (int i = 0; i < n; i++) curr[i] = prev[i] + row_add0[i] + row_add1[i] - row_sub0[i] - row_sub1[i];
}

static inline void nnue_acc_add_row(int16_t *acc, const int16_t *row, int n) {
#if defined(NNUE_HAVE_AVX2_DISPATCH)
    if (cpu_supports_avx2()) { nnue_acc_add_row_avx2(acc, row, n); return; }
#endif
    nnue_acc_add_row_scalar(acc, row, n);
}

static inline void nnue_acc_sub_row(int16_t *acc, const int16_t *row, int n) {
#if defined(NNUE_HAVE_AVX2_DISPATCH)
    if (cpu_supports_avx2()) { nnue_acc_sub_row_avx2(acc, row, n); return; }
#endif
    nnue_acc_sub_row_scalar(acc, row, n);
}

static inline void nnue_acc_update_1add_1sub(int16_t *curr, const int16_t *prev,
                                             const int16_t *row_add, const int16_t *row_sub, int n) {
#if defined(NNUE_HAVE_AVX2_DISPATCH)
    if (cpu_supports_avx2()) { nnue_acc_update_1add_1sub_avx2(curr, prev, row_add, row_sub, n); return; }
#endif
    nnue_acc_update_1add_1sub_scalar(curr, prev, row_add, row_sub, n);
}

static inline void nnue_acc_update_1add_2sub(int16_t *curr, const int16_t *prev,
                                             const int16_t *row_add,
                                             const int16_t *row_sub0, const int16_t *row_sub1, int n) {
#if defined(NNUE_HAVE_AVX2_DISPATCH)
    if (cpu_supports_avx2()) { nnue_acc_update_1add_2sub_avx2(curr, prev, row_add, row_sub0, row_sub1, n); return; }
#endif
    nnue_acc_update_1add_2sub_scalar(curr, prev, row_add, row_sub0, row_sub1, n);
}

static inline void nnue_acc_update_2add_2sub(int16_t *curr, const int16_t *prev,
                                             const int16_t *row_add0, const int16_t *row_add1,
                                             const int16_t *row_sub0, const int16_t *row_sub1, int n) {
#if defined(NNUE_HAVE_AVX2_DISPATCH)
    if (cpu_supports_avx2()) { nnue_acc_update_2add_2sub_avx2(curr, prev, row_add0, row_add1, row_sub0, row_sub1, n); return; }
#endif
    nnue_acc_update_2add_2sub_scalar(curr, prev, row_add0, row_add1, row_sub0, row_sub1, n);
}

/* ── Full Perspective Refresh ────────────────────────────────────────────── */
static void nnue_refresh_perspective(const S_BOARD *pos, int us, int16_t *acc) {
    memcpy(acc, g_weights->ft_b, NNUE_HIDDEN_SIZE * sizeof(int16_t));
    for (int sq = 0; sq < 64; sq++) {
        int p = pos->pieces[sq];
        if (p != EMPTY) {
            size_t idx = nnue_feature_index(us, p, sq);
            const int16_t *row = g_weights->ft_w + idx * NNUE_HIDDEN_SIZE;
            nnue_acc_add_row(acc, row, NNUE_HIDDEN_SIZE);
        }
    }
}

void nnue_refresh_accumulator(S_BOARD *pos) {
    if (!nnue_loaded || !pos->search) return;
    int ply = pos->ply;
    if (ply >= MAXDEPTH) ply = MAXDEPTH - 1;
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
    int l1 = NNUE_HIDDEN_SIZE;
    if (dp->remove_count == 0 && dp->add_count == 0) {
        memcpy(curr_acc, prev_acc, l1 * sizeof(int16_t));
        return;
    }

    if (dp->remove_count == 1 && dp->add_count == 1) {
        size_t idx_sub = nnue_feature_index(us, dp->piece_remove[0], dp->from[0]);
        size_t idx_add = nnue_feature_index(us, dp->piece_add[0], dp->to[0]);
        const int16_t *row_sub = g_weights->ft_w + idx_sub * (size_t)l1;
        const int16_t *row_add = g_weights->ft_w + idx_add * (size_t)l1;
        nnue_acc_update_1add_1sub(curr_acc, prev_acc, row_add, row_sub, l1);
        return;
    }

    if (dp->remove_count == 2 && dp->add_count == 1) {
        size_t idx_sub0 = nnue_feature_index(us, dp->piece_remove[0], dp->from[0]);
        size_t idx_sub1 = nnue_feature_index(us, dp->piece_remove[1], dp->from[1]);
        size_t idx_add0 = nnue_feature_index(us, dp->piece_add[0], dp->to[0]);
        const int16_t *row_sub0 = g_weights->ft_w + idx_sub0 * (size_t)l1;
        const int16_t *row_sub1 = g_weights->ft_w + idx_sub1 * (size_t)l1;
        const int16_t *row_add0 = g_weights->ft_w + idx_add0 * (size_t)l1;
        nnue_acc_update_1add_2sub(curr_acc, prev_acc, row_add0, row_sub0, row_sub1, l1);
        return;
    }

    if (dp->remove_count == 2 && dp->add_count == 2) {
        size_t idx_sub0 = nnue_feature_index(us, dp->piece_remove[0], dp->from[0]);
        size_t idx_sub1 = nnue_feature_index(us, dp->piece_remove[1], dp->from[1]);
        size_t idx_add0 = nnue_feature_index(us, dp->piece_add[0], dp->to[0]);
        size_t idx_add1 = nnue_feature_index(us, dp->piece_add[1], dp->to[1]);
        const int16_t *row_sub0 = g_weights->ft_w + idx_sub0 * (size_t)l1;
        const int16_t *row_sub1 = g_weights->ft_w + idx_sub1 * (size_t)l1;
        const int16_t *row_add0 = g_weights->ft_w + idx_add0 * (size_t)l1;
        const int16_t *row_add1 = g_weights->ft_w + idx_add1 * (size_t)l1;
        nnue_acc_update_2add_2sub(curr_acc, prev_acc, row_add0, row_add1, row_sub0, row_sub1, l1);
        return;
    }

    // General fallback
    memcpy(curr_acc, prev_acc, l1 * sizeof(int16_t));
    for (int r = 0; r < dp->remove_count; r++) {
        size_t idx_sub = nnue_feature_index(us, dp->piece_remove[r], dp->from[r]);
        const int16_t *row_sub = g_weights->ft_w + idx_sub * (size_t)l1;
        nnue_acc_sub_row(curr_acc, row_sub, l1);
    }
    for (int a = 0; a < dp->add_count; a++) {
        size_t idx_add = nnue_feature_index(us, dp->piece_add[a], dp->to[a]);
        const int16_t *row_add = g_weights->ft_w + idx_add * (size_t)l1;
        nnue_acc_add_row(curr_acc, row_add, l1);
    }
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

/* ── Forward Evaluation Pass (SCReLU + 8 Material Buckets) ────────────────── */

#if defined(NNUE_HAVE_AVX2_DISPATCH)
__attribute__((target("avx2")))
static int32_t nnue_forward_avx2(const int16_t *us, const int16_t *them, int bucket) {
    const __m256i vecZero = _mm256_setzero_si256();
    const __m256i vecQA = _mm256_set1_epi16(NNUE_QA);
    __m256i sum = vecZero;
    const int16_t *w_us = g_weights->out_w[bucket];
    const int16_t *w_them = g_weights->out_w[bucket] + NNUE_HIDDEN_SIZE;

    for (int i = 0; i < NNUE_HIDDEN_SIZE; i += 16) {
        __m256i usVec = _mm256_load_si256((const __m256i *)(us + i));
        __m256i themVec = _mm256_load_si256((const __m256i *)(them + i));
        __m256i usWeights = _mm256_loadu_si256((const __m256i *)(w_us + i));
        __m256i themWeights = _mm256_loadu_si256((const __m256i *)(w_them + i));

        __m256i usClamped = _mm256_min_epi16(_mm256_max_epi16(usVec, vecZero), vecQA);
        __m256i themClamped = _mm256_min_epi16(_mm256_max_epi16(themVec, vecZero), vecQA);

        __m256i usResults = _mm256_madd_epi16(_mm256_mullo_epi16(usWeights, usClamped), usClamped);
        __m256i themResults = _mm256_madd_epi16(_mm256_mullo_epi16(themWeights, themClamped), themClamped);

        sum = _mm256_add_epi32(sum, _mm256_add_epi32(usResults, themResults));
    }

    __m128i lo = _mm256_castsi256_si128(sum);
    __m128i hi = _mm256_extracti128_si256(sum, 1);
    __m128i sum128 = _mm_add_epi32(lo, hi);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    int32_t eval = _mm_cvtsi128_si32(sum128);

    eval /= NNUE_QA;
    eval += g_weights->out_b[bucket];
    eval *= NNUE_SCALE;
    eval /= (NNUE_QA * NNUE_QB);
    return eval;
}
#endif

static inline int32_t screlu(int16_t v) {
    int32_t c = v;
    if (c < 0) c = 0;
    else if (c > NNUE_QA) c = NNUE_QA;
    return c * c;
}

static int32_t nnue_forward_scalar(const int16_t *us, const int16_t *them, int bucket) {
    int32_t sum = 0;
    const int16_t *w_us = g_weights->out_w[bucket];
    const int16_t *w_them = g_weights->out_w[bucket] + NNUE_HIDDEN_SIZE;

    for (int i = 0; i < NNUE_HIDDEN_SIZE; i++) {
        sum += screlu(us[i]) * (int32_t)w_us[i] +
               screlu(them[i]) * (int32_t)w_them[i];
    }

    int32_t eval = sum / NNUE_QA;
    eval += g_weights->out_b[bucket];
    eval *= NNUE_SCALE;
    eval /= (NNUE_QA * NNUE_QB);
    return eval;
}

static inline int32_t nnue_forward(const int16_t *us, const int16_t *them, int bucket) {
#if defined(NNUE_HAVE_AVX2_DISPATCH)
    if (cpu_supports_avx2()) return nnue_forward_avx2(us, them, bucket);
#endif
    return nnue_forward_scalar(us, them, bucket);
}

int nnue_eval(S_BOARD *pos) {
    if (!nnue_loaded || !g_weights) return 0;

    int ply = pos->ply;
    if (ply >= MAXDEPTH) ply = MAXDEPTH - 1;

    nnue_update_perspective_to_ply(pos, WHITE, ply);
    nnue_update_perspective_to_ply(pos, BLACK, ply);

    int stm = pos->side;
    int other = stm ^ 1;

    const int16_t *acc_stm = pos->search->nnue_accumulators[ply].accumulation[stm];
    const int16_t *acc_other = pos->search->nnue_accumulators[ply].accumulation[other];

    int pieces = COUNTBIT(pos->byTypeBB[ALL_PIECES]);
    int bucket = (pieces - 2) / 4;
    if (bucket < 0) bucket = 0;
    else if (bucket > 7) bucket = 7;

    return nnue_forward(acc_stm, acc_other, bucket);
}

/* ── Weight Loading & Initialization ─────────────────────────────────────── */
int nnue_init(const char *path) {
    FILE *f = NULL;
    if (path && strlen(path) > 0 && strcmp(path, "<empty>") != 0) {
        f = fopen(path, "rb");
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
            if (f) break;
        }
    }

    if (!f) {
        return 0;
    }

    if (!g_weights) {
        size_t size = (sizeof(NNUE_Weights) + 63) & ~(size_t)63;
        g_weights = (NNUE_Weights *)aligned_alloc(64, size);
        if (!g_weights) {
            fclose(f);
            return 0;
        }
    }

    size_t read = 0;
    read += fread(g_weights->ft_w, sizeof(int16_t), NNUE_INPUT_SIZE * NNUE_HIDDEN_SIZE, f);
    read += fread(g_weights->ft_b, sizeof(int16_t), NNUE_HIDDEN_SIZE, f);
    read += fread(g_weights->out_w, sizeof(int16_t), NNUE_OUTPUT_BUCKETS * NNUE_HIDDEN_SIZE * 2, f);
    read += fread(g_weights->out_b, sizeof(int16_t), NNUE_OUTPUT_BUCKETS, f);
    fclose(f);

    if (read < (size_t)NNUE_TOTAL_SHORTS) {
        printf("info string NNUE load failed: expected %zu shorts, read %zu\n",
               (size_t)NNUE_TOTAL_SHORTS, read);
        return 0;
    }

    nnue_loaded = 1;
    return 1;
}

#endif /* NNUE_IMPLEMENTATION */