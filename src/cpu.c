#include "cpu.h"
#include <stdio.h>

CpuArchTier g_cpu_tier      = ARCH_BASELINE;
int         g_has_popcnt    = 0;
int         g_use_pext      = 0;
const char *g_cpu_tier_name = "x86-64";
const char *g_cpu_simd_name = "Scalar / SSE2";

static int has_slow_bmi2(void) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_cpu_is("amd")
        && (__builtin_cpu_is("bdver4") || __builtin_cpu_is("znver1") || __builtin_cpu_is("znver2"));
#else
    return 0;
#endif
}

void init_cpu_features(void) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();

    int sse42   = __builtin_cpu_supports("sse4.2");
    int popcnt  = __builtin_cpu_supports("popcnt");
    int avx2    = __builtin_cpu_supports("avx2");
    int bmi2    = __builtin_cpu_supports("bmi2");
    int avx512f = __builtin_cpu_supports("avx512f");
    int avx512bw= __builtin_cpu_supports("avx512bw");
    int avx512vl= __builtin_cpu_supports("avx512vl");
    int avx512dq= __builtin_cpu_supports("avx512dq");

    g_has_popcnt = popcnt;
    int fast_bmi2 = bmi2 && !has_slow_bmi2();

#if defined(UNIVERSAL_BUILD)
    // In universal build, dynamically select the highest tier supported by this CPU
    if (avx512f && avx512bw && avx512vl && avx512dq) {
        g_cpu_tier      = ARCH_AVX512;
        g_cpu_tier_name = "AVX-512";
        g_cpu_simd_name = "AVX-512BW (512-bit ZMM)";
        g_use_pext      = fast_bmi2;
    } else if (avx2 && fast_bmi2) {
        g_cpu_tier      = ARCH_AVX2_BMI2;
        g_cpu_tier_name = "AVX2-BMI2";
        g_cpu_simd_name = "AVX2 (256-bit YMM)";
        g_use_pext      = 1;
    } else if (avx2) {
        g_cpu_tier      = ARCH_AVX2;
        g_cpu_tier_name = "AVX2";
        g_cpu_simd_name = "AVX2 (256-bit YMM)";
        g_use_pext      = 0;
    } else if (sse42 && popcnt) {
        g_cpu_tier      = ARCH_SSE42;
        g_cpu_tier_name = "SSE4.2";
        g_cpu_simd_name = "SSE4.2 / POPCNT";
        g_use_pext      = 0;
    } else {
        g_cpu_tier      = ARCH_BASELINE;
        g_cpu_tier_name = "x86-64";
        g_cpu_simd_name = "Scalar / SSE2";
        g_use_pext      = 0;
    }
#else
    // In non-universal (dedicated ISA) builds:
#if defined(__AVX512BW__)
    g_cpu_tier      = ARCH_AVX512;
    g_cpu_tier_name = "AVX-512";
    g_cpu_simd_name = "AVX-512BW (512-bit ZMM)";
    g_use_pext      = fast_bmi2;
#elif defined(__BMI2__) && defined(USE_PEXT)
    g_cpu_tier      = ARCH_AVX2_BMI2;
    g_cpu_tier_name = "AVX2-BMI2";
    g_cpu_simd_name = "AVX2 (256-bit YMM)";
    g_use_pext      = 1;
#elif defined(__AVX2__)
    g_cpu_tier      = ARCH_AVX2;
    g_cpu_tier_name = "AVX2";
    g_cpu_simd_name = "AVX2 (256-bit YMM)";
    g_use_pext      = 0;
#elif defined(__SSE4_2__)
    g_cpu_tier      = ARCH_SSE42;
    g_cpu_tier_name = "SSE4.2";
    g_cpu_simd_name = "SSE4.2 / POPCNT";
    g_use_pext      = 0;
#else
    g_cpu_tier      = ARCH_BASELINE;
    g_cpu_tier_name = "x86-64";
    g_cpu_simd_name = "Scalar / SSE2";
    g_use_pext      = 0;
#endif
#endif

#else
    g_cpu_tier      = ARCH_BASELINE;
    g_has_popcnt    = 0;
    g_use_pext      = 0;
    g_cpu_tier_name = "x86-64";
    g_cpu_simd_name = "Scalar";
#endif
}

void print_compiler_info(void) {
    printf("GOOB 2.2-BETA by Gabriel Montes\n\n");
#if defined(UNIVERSAL_BUILD)
    printf("Binary type                : Universal Multi-ISA Binary\n");
#else
    printf("Binary type                : Dedicated ISA Binary\n");
#endif
    printf("Detected CPU Architecture  : %s\n", g_cpu_tier_name);
    printf("Active SIMD Kernel         : %s\n", g_cpu_simd_name);
    printf("Active Slider Attacks      : %s\n", g_use_pext ? "BMI2 PEXT (Hardware)" : "Magic Bitboards (Multiplication)");
    printf("Hardware Popcount          : %s\n", g_has_popcnt ? "YES" : "NO");
    fflush(stdout);
}
