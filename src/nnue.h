/*
 * nnue.c / nnue.h
 * ================
 * Modern NNUE implementation inspired by Stockfish's HalfKP architecture
 * with efficiently updatable features and optimized SIMD inference.
 * 
 * Architecture: HalfKP (King Position + Piece placement relative to King)
 *   - Input: 768 * 64 = 49152 (64 king squares * 12 piece planes * 64 from-squares)
 *            But only ~768 active features at any time (one per piece)
 *   - L1 (Accumulator): 256 neurons, int16, clipped ReLU
 *   - L2: 32 neurons, int8, clipped ReLU  
 *   - L3: 32 neurons, int8, clipped ReLU
 *   - Output: 1 neuron (raw eval)
 *
 * Key Features:
 *   - Efficiently Updatable Architecture (EUA): Only 2-4 features change per move
 *   - Dual accumulator buffering (side-to-move perspective)
 *   - Full SIMD vectorization (AVX2/AVX-512)
 *   - Integer-only arithmetic until final output scaling
 *   - Optimal feature indexing for cache-friendly access
 */

#ifndef NNUE_H
#define NNUE_H

#include "defs.h"
#include "board.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Configuration - Match these to your trained network
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Network architecture dimensions */
#define NNUE_INPUT_SIZE     49152   /* 64 king squares * 768 piece features */
#define NNUE_L1_SIZE        256     /* Accumulator size (must match board.h) */
#define NNUE_L2_SIZE        32      /* First hidden layer */
#define NNUE_L3_SIZE        32      /* Second hidden layer */
#define NNUE_OUTPUT_SIZE    1       /* Single output neuron */

/* Quantization scales (must match export script) */
#define NNUE_QA_SCALE       64      /* L1 accumulator scale */
#define NNUE_QB_SCALE       64      /* L2 weight scale */
#define NNUE_QC_SCALE       64      /* L3 weight scale */
#define NNUE_QD_SCALE       64      /* Output scale */
#define NNUE_OUTPUT_SCALE   400     /* Final CP conversion scale */

/* Feature encoding */
#define NNUE_KING_SQ_BITS   6       /* log2(64) */
#define NNUE_PIECE_BITS     4       /* log2(16 pieces max) */
#define NNUE_FEATURE_BITS   (NNUE_KING_SQ_BITS + NNUE_PIECE_BITS + 6)

/* Maximum active features per side (for buffer sizing) */
#define NNUE_MAX_ACTIVE     16      /* King + 15 pieces max */

/* ═══════════════════════════════════════════════════════════════════════════
 * Data Structures
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Accumulator state - one per side (White/Black POV) */
typedef struct {
    int16_t values[NNUE_L1_SIZE];   /* Current accumulator values */
    int     computed[NNUE_L1_SIZE]; /* Bitmask: which neurons need refresh */
    int     needs_full_refresh;     /* Flag for complete rebuild */
} NNUE_Accumulator;

/* Network weights - loaded from binary file */
typedef struct {
    /* Header info */
    uint32_t magic;                 /* 'N''U''E''4' */
    uint32_t version;
    uint32_t input_size;
    uint32_t l1_size;
    uint32_t l2_size;
    uint32_t l3_size;
    
    /* Quantization parameters */
    int32_t qa_scale;
    int32_t qb_scale;
    int32_t qc_scale;
    int32_t qd_scale;
    float   output_scale;
    
    /* L1: Input -> Accumulator (feature-major for fast updates) */
    int16_t *l1_weights;            /* [INPUT_SIZE][L1_SIZE] transposed */
    int32_t *l1_bias;               /* [L1_SIZE] */
    
    /* L2: L1 -> L2 (neuron-major, standard layout) */
    int8_t  *l2_weights;            /* [L2_SIZE][L1_SIZE] */
    int32_t *l2_bias;               /* [L2_SIZE] */
    
    /* L3: L2 -> L3 */
    int8_t  *l3_weights;            /* [L3_SIZE][L2_SIZE] */
    int32_t *l3_bias;               /* [L3_SIZE] */
    
    /* Output: L3 -> 1 */
    int8_t  *out_weights;           /* [1][L3_SIZE] */
    int32_t out_bias;               /* scalar */
    
    /* Precomputed combined scale for final output */
    float   final_scale;
    
    /* Optimization flags */
    int     has_avx2;
    int     has_avx512;
    int     has_vnni;               /* AVX-512 VNNI for dot product */
} NNUE_Weights;

/* Board extension for NNUE state */
typedef struct {
    NNUE_Accumulator acc[2];        /* White POV, Black POV accumulators */
    int     king_sq[2];             /* King positions for each side */
    int     active_features[2][NNUE_MAX_ACTIVE];  /* Cached feature indices */
    int     active_count[2];        /* Number of active features per side */
    int     dirty_acc;              /* Which accumulator needs update (-1=none, 0=W, 1=B, 2=both) */
} NNUE_State;

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Initialize NNUE - load weights from file. Returns 1 on success, 0 on failure */
extern int nnue_init(const char *filepath);

/* Free allocated weight memory */
extern void nnue_free(void);

/* Refresh all accumulators from current board state */
extern void nnue_refresh(S_BOARD *pos);

/* Incremental update: add piece at square */
extern void nnue_add_piece(S_BOARD *pos, int piece, int sq);

/* Incremental update: remove piece from square */
extern void nnue_remove_piece(S_BOARD *pos, int piece, int sq);

/* Incremental update: move piece from->to (more efficient than remove+add) */
extern void nnue_move_piece(S_BOARD *pos, int piece, int from, int to);

/* Evaluate position - returns centipawns from side-to-move perspective */
extern int nnue_evaluate(const S_BOARD *pos);

/* Check if NNUE is initialized and ready */
extern int nnue_is_ready(void);

/* Get pointer to NNUE state in board (for direct accumulator access) */
extern NNUE_State* nnue_get_state(S_BOARD *pos);

/* ═══════════════════════════════════════════════════════════════════════════
 * Feature Encoding Helpers (inline for performance)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* 
 * Compute HalfKP feature index
 * 
 * HalfKP encoding: For each piece, we encode:
 *   - King position (0-63)
 *   - Piece type and color (0-11)  
 *   - From square (0-63)
 *   
 * This creates 64 * 12 * 64 = 49152 possible features,
 * but only ~16 are active (one per piece).
 * 
 * The key insight: when a piece moves, only 2 features change
 * (old position removed, new position added), regardless of
 * where the king is.
 */
static inline int nnue_encode_feature(int king_sq, int piece, int from_sq) {
    /* 
     * Feature index formula:
     *   index = (king_sq * 12 * 64) + (piece * 64) + from_sq
     * 
     * Where:
     *   king_sq  : 0-63 (king position)
     *   piece    : 0-11 (piece type: WP=0, WN=1, ... BK=11)
     *   from_sq  : 0-63 (piece square)
     */
    return (king_sq << 10) | (piece << 6) | from_sq;
}

/* 
 * Get piece index for NNUE (0-11)
 * Maps internal piece encoding to NNUE feature space
 */
static inline int nnue_piece_index(int piece) {
    /* Assuming piece encoding: WP=1, WN=2, ..., wk=12, bk=13 */
    /* Convert to 0-based: WP=0, WN=1, ..., BK=11 */
    return piece - 1;
}

/* 
 * Check if feature is from white's or black's perspective
 * Used for dual accumulator management
 */
static inline int nnue_feature_perspective(int piece) {
    return (piece >= 1 && piece <= 6) ? 0 : 1;  /* 0=White, 1=Black */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Implementation
 * ═══════════════════════════════════════════════════════════════════════════ */
#ifdef NNUE_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* Platform detection for SIMD */
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #define NNUE_PLATFORM_X86
#endif

#if defined(NNUE_PLATFORM_X86) && (defined(__GNUC__) || defined(__clang__))
    #include <immintrin.h>
    #define NNUE_SIMD_AVAILABLE
#endif

/* Global state */
static NNUE_Weights g_nnue_weights = {0};
static int g_nnue_initialized = 0;

/* Forward declarations */
static void nnue_accumulator_add(NNUE_Accumulator *acc, int feature_idx);
static void nnue_accumulator_subtract(NNUE_Accumulator *acc, int feature_idx);
static int32_t nnue_dot_product_int8(const int8_t *a, const int8_t *b, int n);
static void nnue_clipped_relu_i8(int8_t *dst, const int32_t *src, int n, int scale);

/* ═══════════════════════════════════════════════════════════════════════════
 * SIMD-optimized dot product kernels
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Scalar fallback - always available */
static int32_t nnue_dot_product_scalar(const int8_t *a, const int8_t *b, int n) {
    int32_t sum = 0;
    for (int i = 0; i < n; i++) {
        sum += (int32_t)(uint8_t)a[i] * (int32_t)b[i];
    }
    return sum;
}

#ifdef NNUE_SIMD_AVAILABLE

/* AVX2 implementation: 32 lanes per iteration using VNNI-style madd */
__attribute__((target("avx2")))
static int32_t nnue_dot_product_avx2(const int8_t *a, const int8_t *b, int n) {
    __m256i acc = _mm256_setzero_si256();
    const __m256i ones = _mm256_set1_epi16(1);
    int i = 0;
    
    /* Process 32 elements per iteration */
    for (; i + 32 <= n; i += 32) {
        __m256i va = _mm256_loadu_si256((const __m256i*)(a + i));
        __m256i vb = _mm256_loadu_si256((const __m256i*)(b + i));
        
        /* Multiply adjacent pairs: u8 x s8 -> 16 x i16 */
        __m256i prod16 = _mm256_maddubs_epi16(va, vb);
        /* Sum adjacent pairs: 16 x i16 -> 8 x i32 */
        __m256i prod32 = _mm256_madd_epi16(prod16, ones);
        
        /* Accumulate */
        acc = _mm256_add_epi32(acc, prod32);
    }
    
    /* Horizontal reduction: 8 x i32 -> 1 x i32 */
    __m128i lo = _mm256_castsi256_si128(acc);
    __m128i hi = _mm256_extracti128_si256(acc, 1);
    __m128i sum128 = _mm_add_epi32(lo, hi);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    int32_t sum = _mm_cvtsi128_si32(sum128);
    
    /* Handle remainder */
    for (; i < n; i++) {
        sum += (int32_t)(uint8_t)a[i] * (int32_t)b[i];
    }
    
    return sum;
}

/* AVX-512 VNNI implementation: 64 lanes per iteration */
#ifdef __AVX512VNNI__
__attribute__((target("avx512f,avx512vnni")))
static int32_t nnue_dot_product_avx512_vnni(const int8_t *a, const int8_t *b, int n) {
    __m512i acc = _mm512_setzero_si512();
    int i = 0;
    
    /* Process 64 elements per iteration */
    for (; i + 64 <= n; i += 64) {
        __m512i va = _mm512_loadu_si512((const __m512i*)(a + i));
        __m512i vb = _mm512_loadu_si512((const __m512i*)(b + i));
        
        /* VNNI: directly accumulate u8 x s8 -> i32 */
        acc = _mm512_dpbusd_epi32(acc, va, vb);
    }
    
    /* Horizontal reduction: 16 x i32 -> 1 x i32 */
    __m256i lo = _mm512_castsi512_si256(acc);
    __m256i hi = _mm512_extracti64x4_epi64(acc, 1);
    __m256i sum256 = _mm256_add_epi32(lo, hi);
    
    __m128i sum128_lo = _mm256_castsi256_si128(sum256);
    __m128i sum128_hi = _mm256_extracti128_si256(sum256, 1);
    __m128i sum128 = _mm_add_epi32(sum128_lo, sum128_hi);
    
    sum128 = _mm_hadd_epi32(sum128, sum128);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    
    int32_t sum = _mm_cvtsi128_si32(sum128);
    
    /* Handle remainder */
    for (; i < n; i++) {
        sum += (int32_t)(uint8_t)a[i] * (int32_t)b[i];
    }
    
    return sum;
}
#endif /* __AVX512VNNI__ */

#endif /* NNUE_SIMD_AVAILABLE */

/* Runtime dispatch to best available kernel */
static int32_t nnue_dot_product_int8(const int8_t *a, const int8_t *b, int n) {
#ifdef NNUE_SIMD_AVAILABLE
    static int kernel_selected = -1;
    
    if (kernel_selected < 0) {
        __builtin_cpu_init();
        
#ifdef __AVX512VNNI__
        if (__builtin_cpu_supports("avx512vnni") && 
            __builtin_cpu_supports("avx512f")) {
            kernel_selected = 2;  /* AVX-512 VNNI */
        } else
#endif
        if (__builtin_cpu_supports("avx2")) {
            kernel_selected = 1;  /* AVX2 */
        } else {
            kernel_selected = 0;  /* Scalar */
        }
    }
    
    switch (kernel_selected) {
#ifdef __AVX512VNNI__
        case 2: return nnue_dot_product_avx512_vnni(a, b, n);
#endif
        case 1: return nnue_dot_product_avx2(a, b, n);
        default: break;
    }
#endif
    
    return nnue_dot_product_scalar(a, b, n);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Clipped ReLU with requantization
 * ═══════════════════════════════════════════════════════════════════════════ */

/* 
 * Clipped ReLU: clamp(x, 0, 127) after dequantization
 * Input: int32 accumulator value (scaled by qa_scale)
 * Output: int8 in range [0, 127]
 */
static inline int8_t nnue_clipped_relu_single(int32_t value, int scale) {
    float v = (float)value / (float)scale;
    if (v < 0.0f) v = 0.0f;
    if (v > 127.0f) v = 127.0f;
    return (int8_t)(v + 0.5f);
}

/* Vectorized clipped ReLU for entire layer */
static void nnue_clipped_relu_i8(int8_t *dst, const int32_t *src, int n, int scale) {
    for (int i = 0; i < n; i++) {
        dst[i] = nnue_clipped_relu_single(src[i], scale);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Accumulator operations
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Add feature weights to accumulator */
static void nnue_accumulator_add(NNUE_Accumulator *acc, int feature_idx) {
    if (!g_nnue_initialized || feature_idx < 0) return;
    
    const int16_t *weights = g_nnue_weights.l1_weights + (size_t)feature_idx * NNUE_L1_SIZE;
    
    /* SIMD-vectorized accumulator update */
#ifdef NNUE_SIMD_AVAILABLE
    if (g_nnue_weights.has_avx2) {
        for (int i = 0; i < NNUE_L1_SIZE; i += 16) {
            __m256i acc_vec = _mm256_loadu_si256((__m256i*)&acc->values[i]);
            __m256i w_vec = _mm256_loadu_si256((const __m256i*)&weights[i]);
            __m256i result = _mm256_add_epi16(acc_vec, w_vec);
            _mm256_storeu_si256((__m256i*)&acc->values[i], result);
        }
    } else
#endif
    {
        for (int i = 0; i < NNUE_L1_SIZE; i++) {
            acc->values[i] += weights[i];
        }
    }
}

/* Subtract feature weights from accumulator */
static void nnue_accumulator_subtract(NNUE_Accumulator *acc, int feature_idx) {
    if (!g_nnue_initialized || feature_idx < 0) return;
    
    const int16_t *weights = g_nnue_weights.l1_weights + (size_t)feature_idx * NNUE_L1_SIZE;
    
#ifdef NNUE_SIMD_AVAILABLE
    if (g_nnue_weights.has_avx2) {
        for (int i = 0; i < NNUE_L1_SIZE; i += 16) {
            __m256i acc_vec = _mm256_loadu_si256((__m256i*)&acc->values[i]);
            __m256i w_vec = _mm256_loadu_si256((const __m256i*)&weights[i]);
            __m256i result = _mm256_sub_epi16(acc_vec, w_vec);
            _mm256_storeu_si256((__m256i*)&acc->values[i], result);
        }
    } else
#endif
    {
        for (int i = 0; i < NNUE_L1_SIZE; i++) {
            acc->values[i] -= weights[i];
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Weight loading
 * ═══════════════════════════════════════════════════════════════════════════ */

int nnue_init(const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "[NNUE] Cannot open weight file: %s\n", filepath);
        return 0;
    }
    
    /* Read header */
    uint32_t magic, version, input_size, l1_size, l2_size, l3_size;
    int32_t qa, qb, qc, qd;
    float out_scale;
    
    if (fread(&magic, sizeof(magic), 1, f) != 1 ||
        fread(&version, sizeof(version), 1, f) != 1 ||
        fread(&input_size, sizeof(input_size), 1, f) != 1 ||
        fread(&l1_size, sizeof(l1_size), 1, f) != 1 ||
        fread(&l2_size, sizeof(l2_size), 1, f) != 1 ||
        fread(&l3_size, sizeof(l3_size), 1, f) != 1 ||
        fread(&qa, sizeof(qa), 1, f) != 1 ||
        fread(&qb, sizeof(qb), 1, f) != 1 ||
        fread(&qc, sizeof(qc), 1, f) != 1 ||
        fread(&qd, sizeof(qd), 1, f) != 1 ||
        fread(&out_scale, sizeof(out_scale), 1, f) != 1) {
        fprintf(stderr, "[NNUE] Failed to read header from %s\n", filepath);
        fclose(f);
        return 0;
    }
    
    if (magic != 0x3445554E) {  /* 'NU E4' in little-endian */
        fprintf(stderr, "[NNUE] Invalid magic number in %s\n", filepath);
        fclose(f);
        return 0;
    }
    
    if (version != 4) {
        fprintf(stderr, "[NNUE] Unsupported version %d in %s (expected 4)\n", version, filepath);
        fclose(f);
        return 0;
    }
    
    /* Validate dimensions */
    if (l1_size != NNUE_L1_SIZE || l2_size != NNUE_L2_SIZE || l3_size != NNUE_L3_SIZE) {
        fprintf(stderr, "[NNUE] Dimension mismatch in %s\n", filepath);
        fprintf(stderr, "  Expected: L1=%d L2=%d L3=%d\n", NNUE_L1_SIZE, NNUE_L2_SIZE, NNUE_L3_SIZE);
        fprintf(stderr, "  Got:      L1=%d L2=%d L3=%d\n", l1_size, l2_size, l3_size);
        fclose(f);
        return 0;
    }
    
    /* Allocate and load weights */
    g_nnue_weights.l1_weights = (int16_t*)malloc(sizeof(int16_t) * (size_t)input_size * l1_size);
    g_nnue_weights.l1_bias = (int32_t*)malloc(sizeof(int32_t) * l1_size);
    g_nnue_weights.l2_weights = (int8_t*)malloc(sizeof(int8_t) * (size_t)l2_size * l1_size);
    g_nnue_weights.l2_bias = (int32_t*)malloc(sizeof(int32_t) * l2_size);
    g_nnue_weights.l3_weights = (int8_t*)malloc(sizeof(int8_t) * (size_t)l3_size * l2_size);
    g_nnue_weights.l3_bias = (int32_t*)malloc(sizeof(int32_t) * l3_size);
    g_nnue_weights.out_weights = (int8_t*)malloc(sizeof(int8_t) * l3_size);
    
    if (!g_nnue_weights.l1_weights || !g_nnue_weights.l1_bias ||
        !g_nnue_weights.l2_weights || !g_nnue_weights.l2_bias ||
        !g_nnue_weights.l3_weights || !g_nnue_weights.l3_bias ||
        !g_nnue_weights.out_weights) {
        fprintf(stderr, "[NNUE] Memory allocation failed\n");
        nnue_free();
        fclose(f);
        return 0;
    }
    
    /* Read weight arrays */
    if (fread(g_nnue_weights.l1_weights, sizeof(int16_t), (size_t)input_size * l1_size, f) != (size_t)input_size * l1_size ||
        fread(g_nnue_weights.l1_bias, sizeof(int32_t), l1_size, f) != (size_t)l1_size ||
        fread(g_nnue_weights.l2_weights, sizeof(int8_t), (size_t)l2_size * l1_size, f) != (size_t)l2_size * l1_size ||
        fread(g_nnue_weights.l2_bias, sizeof(int32_t), l2_size, f) != (size_t)l2_size ||
        fread(g_nnue_weights.l3_weights, sizeof(int8_t), (size_t)l3_size * l2_size, f) != (size_t)l3_size * l2_size ||
        fread(g_nnue_weights.l3_bias, sizeof(int32_t), l3_size, f) != (size_t)l3_size ||
        fread(g_nnue_weights.out_weights, sizeof(int8_t), l3_size, f) != (size_t)l3_size ||
        fread(&g_nnue_weights.out_bias, sizeof(int32_t), 1, f) != 1) {
        fprintf(stderr, "[NNUE] Failed to read weights from %s\n", filepath);
        nnue_free();
        fclose(f);
        return 0;
    }
    
    fclose(f);
    
    /* Store metadata */
    g_nnue_weights.magic = magic;
    g_nnue_weights.version = version;
    g_nnue_weights.input_size = input_size;
    g_nnue_weights.l1_size = l1_size;
    g_nnue_weights.l2_size = l2_size;
    g_nnue_weights.l3_size = l3_size;
    g_nnue_weights.qa_scale = qa;
    g_nnue_weights.qb_scale = qb;
    g_nnue_weights.qc_scale = qc;
    g_nnue_weights.qd_scale = qd;
    g_nnue_weights.output_scale = out_scale;
    
    /* Precompute final scale */
    g_nnue_weights.final_scale = out_scale / (127.0f * (float)qd);
    
    /* Detect CPU features */
#ifdef NNUE_SIMD_AVAILABLE
    __builtin_cpu_init();
    g_nnue_weights.has_avx2 = __builtin_cpu_supports("avx2");
#ifdef __AVX512VNNI__
    g_nnue_weights.has_avx512 = __builtin_cpu_supports("avx512f");
    g_nnue_weights.has_vnni = __builtin_cpu_supports("avx512vnni");
#else
    g_nnue_weights.has_avx512 = 0;
    g_nnue_weights.has_vnni = 0;
#endif
#else
    g_nnue_weights.has_avx2 = 0;
    g_nnue_weights.has_avx512 = 0;
    g_nnue_weights.has_vnni = 0;
#endif
    
    g_nnue_initialized = 1;
    
    printf("[NNUE] Loaded v%d network: %dx%dx%dx1 (QA=%d QB=%d QC=%d QD=%d)\n",
           version, l1_size, l2_size, l3_size, qa, qb, qc, qd);
    printf("[NNUE] SIMD: AVX2=%d AVX512=%d VNNI=%d\n",
           g_nnue_weights.has_avx2, g_nnue_weights.has_avx512, g_nnue_weights.has_vnni);
    
    return 1;
}

void nnue_free(void) {
    free(g_nnue_weights.l1_weights);
    free(g_nnue_weights.l1_bias);
    free(g_nnue_weights.l2_weights);
    free(g_nnue_weights.l2_bias);
    free(g_nnue_weights.l3_weights);
    free(g_nnue_weights.l3_bias);
    free(g_nnue_weights.out_weights);
    
    memset(&g_nnue_weights, 0, sizeof(g_nnue_weights));
    g_nnue_initialized = 0;
}

int nnue_is_ready(void) {
    return g_nnue_initialized;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Incremental updates
 * ═══════════════════════════════════════════════════════════════════════════ */

void nnue_add_piece(S_BOARD *pos, int piece, int sq) {
    if (!g_nnue_initialized || piece == EMPTY) return;
    
    NNUE_State *state = (NNUE_State*)pos->nnue_state;
    if (!state) return;
    
    int king_sq_w = state->king_sq[0];
    int king_sq_b = state->king_sq[1];
    int piece_idx = nnue_piece_index(piece);
    int pov = nnue_feature_perspective(piece);
    
    /* Add feature for both perspectives */
    if (king_sq_w >= 0) {
        int feat_w = nnue_encode_feature(king_sq_w, piece_idx, sq);
        nnue_accumulator_add(&state->acc[0], feat_w);
    }
    if (king_sq_b >= 0) {
        int feat_b = nnue_encode_feature(king_sq_b, piece_idx, sq);
        nnue_accumulator_add(&state->acc[1], feat_b);
    }
    
    state->dirty_acc = 2;  /* Both accumulators dirty */
}

void nnue_remove_piece(S_BOARD *pos, int piece, int sq) {
    if (!g_nnue_initialized || piece == EMPTY) return;
    
    NNUE_State *state = (NNUE_State*)pos->nnue_state;
    if (!state) return;
    
    int king_sq_w = state->king_sq[0];
    int king_sq_b = state->king_sq[1];
    int piece_idx = nnue_piece_index(piece);
    
    /* Remove feature for both perspectives */
    if (king_sq_w >= 0) {
        int feat_w = nnue_encode_feature(king_sq_w, piece_idx, sq);
        nnue_accumulator_subtract(&state->acc[0], feat_w);
    }
    if (king_sq_b >= 0) {
        int feat_b = nnue_encode_feature(king_sq_b, piece_idx, sq);
        nnue_accumulator_subtract(&state->acc[1], feat_b);
    }
    
    state->dirty_acc = 2;
}

void nnue_move_piece(S_BOARD *pos, int piece, int from, int to) {
    if (!g_nnue_initialized || piece == EMPTY) return;
    
    NNUE_State *state = (NNUE_State*)pos->nnue_state;
    if (!state) return;
    
    int king_sq_w = state->king_sq[0];
    int king_sq_b = state->king_sq[1];
    int piece_idx = nnue_piece_index(piece);
    
    /* Move = remove old + add new (but more efficient as single operation) */
    if (king_sq_w >= 0) {
        int feat_from = nnue_encode_feature(king_sq_w, piece_idx, from);
        int feat_to = nnue_encode_feature(king_sq_w, piece_idx, to);
        
        const int16_t *w_from = g_nnue_weights.l1_weights + (size_t)feat_from * NNUE_L1_SIZE;
        const int16_t *w_to = g_nnue_weights.l1_weights + (size_t)feat_to * NNUE_L1_SIZE;
        
#ifdef NNUE_SIMD_AVAILABLE
        if (g_nnue_weights.has_avx2) {
            for (int i = 0; i < NNUE_L1_SIZE; i += 16) {
                __m256i acc_vec = _mm256_loadu_si256((__m256i*)&state->acc[0].values[i]);
                __m256i w_from_vec = _mm256_loadu_si256((const __m256i*)&w_from[i]);
                __m256i w_to_vec = _mm256_loadu_si256((const __m256i*)&w_to[i]);
                __m256i delta = _mm256_sub_epi16(w_to_vec, w_from_vec);
                __m256i result = _mm256_add_epi16(acc_vec, delta);
                _mm256_storeu_si256((__m256i*)&state->acc[0].values[i], result);
            }
        } else
#endif
        {
            for (int i = 0; i < NNUE_L1_SIZE; i++) {
                state->acc[0].values[i] += w_to[i] - w_from[i];
            }
        }
    }
    
    if (king_sq_b >= 0) {
        int feat_from = nnue_encode_feature(king_sq_b, piece_idx, from);
        int feat_to = nnue_encode_feature(king_sq_b, piece_idx, to);
        
        const int16_t *w_from = g_nnue_weights.l1_weights + (size_t)feat_from * NNUE_L1_SIZE;
        const int16_t *w_to = g_nnue_weights.l1_weights + (size_t)feat_to * NNUE_L1_SIZE;
        
#ifdef NNUE_SIMD_AVAILABLE
        if (g_nnue_weights.has_avx2) {
            for (int i = 0; i < NNUE_L1_SIZE; i += 16) {
                __m256i acc_vec = _mm256_loadu_si256((__m256i*)&state->acc[1].values[i]);
                __m256i w_from_vec = _mm256_loadu_si256((const __m256i*)&w_from[i]);
                __m256i w_to_vec = _mm256_loadu_si256((const __m256i*)&w_to[i]);
                __m256i delta = _mm256_sub_epi16(w_to_vec, w_from_vec);
                __m256i result = _mm256_add_epi16(acc_vec, delta);
                _mm256_storeu_si256((__m256i*)&state->acc[1].values[i], result);
            }
        } else
#endif
        {
            for (int i = 0; i < NNUE_L1_SIZE; i++) {
                state->acc[1].values[i] += w_to[i] - w_from[i];
            }
        }
    }
    
    state->dirty_acc = 2;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Full accumulator refresh
 * ═══════════════════════════════════════════════════════════════════════════ */

void nnue_refresh(S_BOARD *pos) {
    if (!g_nnue_initialized) return;
    
    NNUE_State *state = (NNUE_State*)pos->nnue_state;
    if (!state) return;
    
    /* Find kings */
    int king_sq_w = -1, king_sq_b = -1;
    for (int sq = 0; sq < 64; sq++) {
        if (pos->pieces[sq] == WK) king_sq_w = sq;
        if (pos->pieces[sq] == BK) king_sq_b = sq;
    }
    
    state->king_sq[0] = king_sq_w;
    state->king_sq[1] = king_sq_b;
    
    /* Initialize accumulators with bias */
    for (int pov = 0; pov < 2; pov++) {
        for (int i = 0; i < NNUE_L1_SIZE; i++) {
            state->acc[pov].values[i] = g_nnue_weights.l1_bias[i];
        }
    }
    
    /* Add all pieces */
    for (int sq = 0; sq < 64; sq++) {
        int piece = pos->pieces[sq];
        if (piece == EMPTY) continue;
        
        int piece_idx = nnue_piece_index(piece);
        
        if (king_sq_w >= 0) {
            int feat = nnue_encode_feature(king_sq_w, piece_idx, sq);
            nnue_accumulator_add(&state->acc[0], feat);
        }
        if (king_sq_b >= 0) {
            int feat = nnue_encode_feature(king_sq_b, piece_idx, sq);
            nnue_accumulator_add(&state->acc[1], feat);
        }
    }
    
    state->dirty_acc = -1;  /* Clean */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Forward pass (inference)
 * ═══════════════════════════════════════════════════════════════════════════ */

int nnue_evaluate(const S_BOARD *pos) {
    if (!g_nnue_initialized) return 0;
    
    NNUE_State *state = (NNUE_State*)pos->nnue_state;
    if (!state) return 0;
    
    /* Select accumulator based on side to move */
    int pov = (pos->side == WHITE) ? 0 : 1;
    NNUE_Accumulator *acc = &state->acc[pov];
    
    /* L1 -> L2: clipped ReLU + int8 quantization */
    int8_t l2_input[NNUE_L1_SIZE];
    nnue_clipped_relu_i8(l2_input, acc->values, NNUE_L1_SIZE, g_nnue_weights.qa_scale);
    
    /* L2 forward pass */
    int32_t l2_output[NNUE_L2_SIZE];
    for (int i = 0; i < NNUE_L2_SIZE; i++) {
        const int8_t *row = g_nnue_weights.l2_weights + (size_t)i * NNUE_L1_SIZE;
        l2_output[i] = g_nnue_weights.l2_bias[i] + nnue_dot_product_int8(l2_input, row, NNUE_L1_SIZE);
    }
    
    /* L2 -> L3: clipped ReLU + int8 quantization */
    int8_t l3_input[NNUE_L2_SIZE];
    nnue_clipped_relu_i8(l3_input, l2_output, NNUE_L2_SIZE, g_nnue_weights.qb_scale);
    
    /* L3 forward pass */
    int32_t l3_output[NNUE_L3_SIZE];
    for (int i = 0; i < NNUE_L3_SIZE; i++) {
        const int8_t *row = g_nnue_weights.l3_weights + (size_t)i * NNUE_L2_SIZE;
        l3_output[i] = g_nnue_weights.l3_bias[i] + nnue_dot_product_int8(l3_input, row, NNUE_L2_SIZE);
    }
    
    /* L3 -> Output: clipped ReLU + int8 quantization */
    int8_t out_input[NNUE_L3_SIZE];
    nnue_clipped_relu_i8(out_input, l3_output, NNUE_L3_SIZE, g_nnue_weights.qc_scale);
    
    /* Output layer */
    int32_t raw_eval = g_nnue_weights.out_bias + 
                       nnue_dot_product_int8(out_input, g_nnue_weights.out_weights, NNUE_L3_SIZE);
    
    /* Convert to centipawns */
    int cp = (int)((float)raw_eval * g_nnue_weights.final_scale);
    
    /* Return from side-to-move perspective */
    return (pos->side == WHITE) ? cp : -cp;
}

NNUE_State* nnue_get_state(S_BOARD *pos) {
    return (NNUE_State*)pos->nnue_state;
}

#endif /* NNUE_IMPLEMENTATION */

#endif /* NNUE_H */
