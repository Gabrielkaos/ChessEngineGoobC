#ifndef CPU_H
#define CPU_H

#include "defs.h"

typedef enum {
    ARCH_BASELINE  = 0, // x86-64 (SSE2 baseline)
    ARCH_SSE42     = 1, // x86-64-v2 (SSE4.2 + POPCNT)
    ARCH_AVX2      = 2, // x86-64-v3 without fast BMI2 (e.g. AMD Zen 1/2)
    ARCH_AVX2_BMI2 = 3, // x86-64-v3 with fast BMI2/PEXT (Intel Haswell+, Zen 3+)
    ARCH_AVX512    = 4  // x86-64-v4 (AVX-512BW, AVX-512F, AVX-512VL, AVX-512DQ)
} CpuArchTier;

extern CpuArchTier g_cpu_tier;
extern int g_has_popcnt;
extern int g_use_pext;
extern const char *g_cpu_tier_name;
extern const char *g_cpu_simd_name;

void init_cpu_features(void);
void print_compiler_info(void);

#endif // CPU_H
