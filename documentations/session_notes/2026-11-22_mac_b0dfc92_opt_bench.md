# macOS Benchmark & Verification: `b0dfc92` vs `028b5ba`

- **Date:** 2026-11-22
- **Machine:** MacBook Pro — **Apple M5** (`arm64-apple-darwin27.2.0`)
- **Compiler:** Apple clang 21.0.0 (`clang-2100.4.1.1`)
- **Base Commit:** `028b5ba` (`Add SPDX license headers to GOOB source files`)
- **Tested Commit:** `b0dfc92` (`refactor: optimize move making, lazy evaluation, and NNUE accumulator updates`)
- **NNUE Net:** Embedded `src/weights/quantised.bin` (`mirror=1, king_buckets=4`, identical between base and new)

---

## 1. Build Verification

Clean builds succeeded with zero warnings on both macOS ARM64 tiers:

- `native` (`-O3 -flto -pthread -DPLATFORM_MAC -mcpu=native`)
  - Base: `tools/sprt/bin/GOOB-base-028b5ba`
  - New: `tools/sprt/bin/GOOB-new-b0dfc92`
- `apple-silicon` (`-O3 -flto -pthread -DPLATFORM_MAC -arch arm64 -mcpu=apple-m1`)
  - Base: `tools/sprt/bin/GOOB-base-028b5ba-apple-silicon`
  - New: `tools/sprt/bin/GOOB-new-b0dfc92-apple-silicon`

---

## 2. Correctness & Node Parity

All correctness and determinism checks matched 100% between `028b5ba` and `b0dfc92`:

- **`python3 tools/sprt/bench.py <bin> 12` (16 positions, 64 MB hash):**
  - Identical best move, score, and node count on every position (`726,116` total nodes).
- **Built-in `./GOOB bench 14` (30 positions):**
  - Identical total node count (`4,719,883` nodes).
- **`perfttest` (33 positions, 1,225,549,258 nodes):**
  - `33 / 33 PASS` on both binaries (verifying lazy `blockersForKing` / `checkersBB` and cached `kingSq[2]` in `makemove.c` / `makemove.h`).

---

## 3. Performance Results (A/B Interleaved)

| Benchmark | Tier | Base (`028b5ba`) | New (`b0dfc92`) | Delta | Rounds Faster |
| :--- | :--- | ---: | ---: | ---: | :---: |
| `tools/sprt/compare_nps.py` depth 12 (10 rounds, 64 MB hash) | `native` | `2,293,380 ± 22,793` nps (`316.6 ms`) | `2,348,203 ± 12,539` nps (`309.2 ms`) | **+2.39%** | 10 / 10 |
| `tools/sprt/compare_nps.py` depth 15 (6 rounds, 64 MB hash) | `native` | `2,344,696 ± 44,699` nps (`1537.3 ms`) | `2,397,046 ± 32,995` nps (`1503.5 ms`) | **+2.23%** | 6 / 6 |
| Built-in `bench 14` (30 pos, 6 interleaved rounds) | `native` | `1,944,144 ± 76,830` nps (`2430.9 ms`) | `1,963,998 ± 74,787` nps (`2406.1 ms`) | **+1.02%** | 4 / 6 *(noisy; thermal/clock drift)* |
| `tools/sprt/compare_nps.py` depth 12 (10 rounds, 64 MB hash) | `apple-silicon` | `2,263,237 ± 67,712` nps (`321.1 ms`) | `2,319,666 ± 55,869` nps (`313.2 ms`) | **+2.49%** | 9 / 10 |
| `perfttest` (33 pos, 1,225,549,258 nodes) | `native` | `22,756 ms` (`53.86M` real / `55.71M` rep nps) | `18,576 ms` (`65.97M` nps) | **+22.5%** | — |

---

## 4. Observations & Follow-Ups for Future Sessions

1. **Verdict on macOS / Apple Silicon:** Confirmed **node-identical** and **consistently faster** on Apple M5: ~**+2.2% to +2.5%** search NPS across both `native` and `apple-silicon` builds, and **+22.5%** faster perft from the `makeMove` / lazy pin / cached `kingSq` optimizations.
2. **Potential ARM64 NEON follow-up (`src/nnue_loader.h:973-1037`):**
   - `nnue_acc_1add_1sub_forward_neon()` and `nnue_acc_1add_2sub_forward_neon()` process `us` and `them` in two separate `i += 8` loops, feeding only **2** `vmlal` accumulator chains (`s0`/`s1`, then `s2`/`s3`) per loop.
   - By contrast, `nnue_forward_neon()` (`src/nnue_loader.h:1039-1058`) interleaves `us` and `them` across **4** accumulator chains, and the AVX2 fused kernels unroll 4 vectors / 4 chains (`i += 64`).
   - Unrolling the NEON fused kernels (e.g., `i += 16` with 4 accumulator chains per loop, or interleaving `us`/`them`) is a good candidate to test on Mac if aiming to match larger x86 speedups.
