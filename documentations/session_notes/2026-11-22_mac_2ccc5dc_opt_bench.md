# macOS Benchmark & Verification: `2ccc5dc` vs `b0dfc92` (and Cumulative vs `028b5ba`)

- **Date:** 2026-11-22
- **Machine:** MacBook Pro — **Apple M5** (`arm64-apple-darwin27.2.0`)
- **Compiler:** Apple clang 21.0.0 (`clang-2100.4.1.1`)
- **Immediate Base:** `b0dfc92` (`refactor: optimize move making, lazy evaluation, and NNUE accumulator updates`)
- **Pre-Optimization Base:** `028b5ba` (`Add SPDX license headers to GOOB source files`)
- **Tested Commit:** `2ccc5dc` (`refactor: optimize history updates and transposition table SMP generation writes`)
- **NNUE Net:** Embedded `src/weights/quantised.bin` (`mirror=1, king_buckets=4`, identical across all builds)

---

## 1. Key Changes Tested on ARM64 / macOS

- **NEON fused 1-ply update+forward unrolling (`src/nnue_loader.h`):** `nnue_acc_1add_1sub_forward_neon` and `nnue_acc_1add_2sub_forward_neon` unrolled to `i += 16` feeding all 4 `vmlal` accumulator chains (`s0..s3`) per loop, plus `NNUE_RESTRICT` on `w_us`/`w_them` in `nnue_forward_neon`.
- **Transposition Table SMP writes (`src/pvtable.h`):** Replaced `__atomic_thread_fence(__ATOMIC_RELEASE)` (full `dmb ish` barrier on ARM64) with `__atomic_store_n(&smp_key, new_key, __ATOMIC_RELEASE)` (`stlr` on ARM64), and guarded TT probe generation refresh (`if (generation != table->generation)`) to avoid redundant dirty cacheline writes.
- **History / Search hot path (`src/history.h`, `src/history.c`, `src/movepicker.c`, `src/search.c`):** Hoisted 4 continuation-history slices (`getContHistSlices()`), deferred `getHistoryFast()` / `getCaptureHistory()` until needed (skipping move-1 cutoffs), and passed `NULL` PV pointers to zero-window searches.

---

## 2. Build Verification

Clean builds succeeded with zero warnings on both macOS ARM64 tiers:

- `native` (`-O3 -flto -pthread -DPLATFORM_MAC -mcpu=native`) -> `tools/sprt/bin/GOOB-new-2ccc5dc`
- `apple-silicon` (`-O3 -flto -pthread -DPLATFORM_MAC -arch arm64 -mcpu=apple-m1`) -> `tools/sprt/bin/GOOB-new-2ccc5dc-apple-silicon`

---

## 3. Correctness & Node Parity

100% node-identical and stable across single-thread and 4-thread SMP runs:

- **`python3 tools/sprt/bench.py <bin> 12` (16 positions, 64 MB hash):**
  - Identical best move, score, and node count on every position (`726,116` total nodes; `282 ms`, `2,574,879` nps single run).
- **Built-in `./GOOB bench 14` (30 positions):**
  - Identical total node count (`4,719,883` nodes; `1874 ms`, `2,518,614` nps single run).
- **4-thread SMP stability (`Threads=4`, `Hash=128`, 4 positions × `movetime 1000`, 3 rounds):**
  - Clean completion across all 12 searches, zero crashes or hangs.

---

## 4. Performance Results (A/B Interleaved)

### Incremental: `b0dfc92` -> `2ccc5dc`

| Benchmark | Tier | Base (`b0dfc92`) | New (`2ccc5dc`) | Delta | Rounds Faster |
| :--- | :--- | ---: | ---: | ---: | :---: |
| `tools/sprt/compare_nps.py` depth 12 (10 rounds, 64 MB hash) | `native` | `2,498,994 ± 7,896` nps (`290.6 ms`) | `2,746,809 ± 9,545` nps (`264.4 ms`) | **+9.92%** | 10 / 10 |
| `tools/sprt/compare_nps.py` depth 15 (6 rounds, 64 MB hash) | `native` | `2,443,632 ± 53,542` nps (`1475.2 ms`) | `2,705,972 ± 16,687` nps (`1331.7 ms`) | **+10.74%** | 6 / 6 |
| `tools/sprt/compare_nps.py` depth 12 (10 rounds, 64 MB hash) | `apple-silicon` | `2,350,490 ± 57,635` nps (`309.1 ms`) | `2,543,933 ± 58,284` nps (`285.6 ms`) | **+8.23%** | 10 / 10 |
| 4-thread SMP (`Threads=4`, `Hash=128`, 4 pos × 1000 ms, 3 rounds) | `native` | `8,374,990 ± 106,307` nps | `9,103,955 ± 188,751` nps | **+8.70%** | 3 / 3 |

### Cumulative: `028b5ba` -> `2ccc5dc`

| Benchmark | Tier | Base (`028b5ba`) | New (`2ccc5dc`) | Delta | Rounds Faster |
| :--- | :--- | ---: | ---: | ---: | :---: |
| `tools/sprt/compare_nps.py` depth 15 (6 rounds, 64 MB hash) | `native` | `2,350,207 ± 32,174` nps (`1533.5 ms`) | `2,679,322 ± 40,196` nps (`1345.2 ms`) | **+14.00%** | 6 / 6 |

---

## 5. Summary for PC (Linux/Windows) Sessions

- **Major ARM64 win:** `2ccc5dc` delivers a huge **+8.2% to +10.7%** single-thread NPS jump on Apple M5 (and **+8.70%** on 4-thread SMP) while staying 100% node-identical.
- **Combined across `b0dfc92` + `2ccc5dc`:** **+14.00% cumulative NPS** (`2.35M -> 2.68M` nps at depth 15 on `native`, `2.75M` nps at depth 12, `9.10M` nps at 4 threads).
- **Why ARM64 jumped so much here:** On top of the history/PV hoisting and the 4-chain `i += 16` NEON fused-kernel unrolling, replacing `__atomic_thread_fence(__ATOMIC_RELEASE)` with `__atomic_store_n(..., __ATOMIC_RELEASE)` turns a full `dmb ish` fence on every TT store into a lightweight `stlr` release-store on ARM64, and skipping redundant TT generation writes reduces cacheline dirtiness on both 1T and 4T SMP.
