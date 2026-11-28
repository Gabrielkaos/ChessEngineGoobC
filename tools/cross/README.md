# Cross-Platform Build & Test Suite (`tools/cross/`)

Build, run, and verify **macOS**, **Linux (x86-64)**, and **Windows (x86-64 `.exe`)** binaries of GOOB directly from an Apple Silicon MacBook using Docker (OrbStack or Docker Desktop).

## How It Works

* **macOS binaries** (`apple-silicon`, `native`) compile and run natively on the host Mac with Apple Clang.
* **Linux x86-64 binaries** (`universal`, `x86-64`, `x86-64-v2`, `x86-64-v3`, `x86-64-v4`) compile inside the `goob-cross:latest` (`linux/amd64`, Debian Trixie) container using GCC 14.
  * `universal`, `x86-64`, `x86-64-v2`, and `x86-64-v3` (AVX2 + BMI2/PEXT) execute directly via Rosetta 2.
  * `universal` runtime CPUID dispatch across `x86-64` (SSE2), `SSE4.2`, `AVX2` (without BMI2), and `AVX2-BMI2` is additionally verified under `qemu-x86_64` with explicit `-cpu` models (`qemu64`, `Nehalem`, `Haswell,-bmi2,-hle,-rtm`, `Haswell,-hle,-rtm`).
* **Windows x86-64 binaries** (`universal.exe`, `x86-64.exe`, `x86-64-v2.exe`, `x86-64-v3.exe`, `x86-64-v4.exe`) cross-compile inside the container using `x86_64-w64-mingw32-gcc` (statically linked, 16 MB stack) and execute under Wine (`wine`) with a pre-initialized headless 64-bit prefix.
* **AVX-512 (`x86-64-v4`)**: compiled, linked, and format-checked for both Linux and Windows; AVX-512 instruction execution is not yet supported by Rosetta 2 or QEMU 10 TCG.
* **NNUE Weight Safety**: `src/weights` is always bind-mounted **read-only** (`:ro`) inside the container.

## Quick Start

From the repository root:

```bash
# 1. One-time Docker image setup (builds goob-cross:latest)
make cross-setup

# 2. Build all platforms (macOS + Linux + Windows)
make cross-build

# 3. Build & run the full verification matrix across macOS, Linux, and Windows
make cross-test
```

## CLI Reference (`tools/cross/cross.sh`)

```bash
# Build a single platform or specific tiers
./tools/cross/cross.sh build mac
./tools/cross/cross.sh build linux universal x86-64-v3
./tools/cross/cross.sh build windows universal x86-64-v3

# Run any binary interactively or with CLI arguments
./tools/cross/cross.sh run mac apple-silicon compiler
./tools/cross/cross.sh run linux x86-64-v3 bench 8
./tools/cross/cross.sh run linux universal --cpu Haswell,-hle,-rtm compiler
./tools/cross/cross.sh run windows x86-64-v3 bench 8

# Run tools/sprt/bench.py's 16-position benchmark on any platform/tier
./tools/cross/cross.sh bench windows universal 10
./tools/cross/cross.sh bench linux x86-64-v3 12

# Run the automated test matrix with options
./tools/cross/cross.sh test --depth 8
./tools/cross/cross.sh test --skip-build --only linux,windows

# Drop into an interactive shell inside the Linux/MinGW/Wine/QEMU container
./tools/cross/cross.sh shell
```

## What `test_matrix.py` Verifies

For every executable configuration across macOS, Linux, and Windows:

1. **Binary format** (`Mach-O 64-bit arm64`, `ELF 64-bit LSB`, `PE32+ executable`).
2. **`compiler` output** (expected CPU tier, active SIMD kernel, and PEXT vs Magic sliders).
3. **Built-in TT unit tests** (`test` command — all 7 pass).
4. **Move generation** (`perft 4` from startpos == `197281`).
5. **Static NNUE evaluation** (`eval` on startpos — must match bit-for-bit across all OSes and ISAs).
6. **Fixed-depth search determinism** (`bench <depth>` — total node count must match bit-for-bit across macOS NEON, Linux SSE2/SSE4.2/AVX2/AVX2-BMI2, and Windows `.exe`).
7. **Multi-threaded UCI smoke test** (`Threads 2`, `go depth 7`, legal `bestmove`, clean exit).
