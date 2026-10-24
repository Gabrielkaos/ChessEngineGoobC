# GOOB Chess Engine – Tools & Machine Learning Pipeline (`/tools`)

This directory contains the complete offline training, data generation, tuning, testing, and diagnostic toolset for the **GOOB** chess engine.

---

## Table of Contents
1. [Overview & Tooling Ecosystem](#overview--tooling-ecosystem)
2. [Root Tools & Diagnostic Scripts](#root-tools--diagnostic-scripts)
3. [The NNUE Subproject (`tools/nnue_project/`)](#the-nnue-subproject-toolsnnue_project)
4. [Step-by-Step Workflows](#step-by-step-workflows)
   * [Workflow A: Generate Training Data via Self-Play](#workflow-a-generate-training-data-via-self-play)
   * [Workflow B: Stream & Prepare Lichess Evaluated Positions](#workflow-b-stream--prepare-lichess-evaluated-positions)
   * [Workflow C: Train an NNUE Network from Scratch or Fine-Tune](#workflow-c-train-an-nnue-network-from-scratch-or-fine-tune)
   * [Workflow D: Quantize & Export Weights for the Engine](#workflow-d-quantize--export-weights-for-the-engine)
   * [Workflow E: Verify NNUE Correctness](#workflow-e-verify-nnue-correctness)
   * [Workflow F: Trace & Benchmark Search Heuristics](#workflow-f-trace--benchmark-search-heuristics)
   * [Workflow G: Classical Evaluation Tuning (historical note)](#workflow-g-classical-evaluation-tuning-historical-note)
   * [Workflow H: Tune Search Constants with SPSA](#workflow-h-tune-search-constants-with-spsa)
   * [Workflow I: Estimate Engine Elo Strength via EPD Test Suites](#workflow-i-estimate-engine-elo-strength-via-epd-test-suites)
5. [Data Formats & Specifications](#data-formats--specifications)

---

## Overview & Tooling Ecosystem

The tooling is divided into three primary operational domains:

1. **NNUE Training & Quantization Pipeline:** Centered in [`nnue_project/`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project), this provides dataset conversion, memory-mapped data loaders, PyTorch training with weight bounds enforcement, and binary weight export matching the C engine's SIMD layout.
2. **Self-Play & Dataset Generation:** Native C generator ([`datagen.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/datagen.c)) and Python scripts ([`selfplay_nnue.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/selfplay_nnue.py) and [`datagen.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/datagen.py)) that harvest evaluated self-play positions with game outcomes.
3. **Verification, Tuning & Telemetry:** Tools for testing incremental accumulator updates ([`test_incremental.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/test_incremental.c)), comparing C inference against PyTorch floating-point output ([`test_nnue_comprehensive.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/test_nnue_comprehensive.py)), profiling search prunings ([`trace_search.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/trace_search.py)), and SPSA tuning of the search constants ([`spsa/spsa.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/spsa/spsa.py)).
4. **SPRT & Strength Testing Framework:** Centered in [`sprt/`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/sprt), this provides automated Cutechess-cli SPRT matches ([`run_sprt.sh`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/sprt/run_sprt.sh)), 12-position fixed-depth sanity benchmarks ([`bench.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/sprt/bench.py)), and testing guides ([`README.md`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/sprt/README.md)).
5. **EPD Benchmarks & Matchless Elo Estimation:** Centered in [`suites/`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/suites) and [`epd_test.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/epd_test.py), this provides standard match-free strength and Elo estimation using canonical test suites (BT-2630, STS 1–15, Bratko-Kopec, Win At Chess).
6. **Goob Research Laboratory:** Interactive web platform and experimentation dashboard ([`research_lab/`](file:///home/gabriel/Desktop/ChessEngineGoobC/research_lab)) integrating live position exploration, iterative deepening analysis, search heuristic telemetry, cutechess tournaments, SPSA parameter management, and persistent experiment tracking. Launched via `python3 run_lab.py`.

---

## Root Tools & Diagnostic Scripts

### 1. [`datagen.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/datagen.c)
* **Purpose:** Standalone native C self-play generator linked directly against the engine search (no UCI or Python overhead) that writes 68-byte [`fen_utils.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/fen_utils.py) binary records with WDL game outcomes.
* **Key Features:**
  * Starts from a random opening in [`book.epd`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/book.epd) plus `--random-plies` (default 4, or 8 from `START_FEN`) for diversity.
  * Runs node-limited searches (`--nodes`, default 5000) directly in-process (one process per core, seeded via `--seed`).
  * Records quiet positions only (side to move not in check, best move not a capture/promotion, best move does not give check), keeps lopsided scores (saturated at $\pm 3000$) and mate flags, and sets result bits (`1`=White win, `2`=Draw, `3`=Black win) after game end or adjudication (`--resign-eval 1500` for 4 plies; `--draw-eval 10` for 8 plies after move 40; `--max-ply 400`).
  * Refuses to open or overwrite any existing output file (`O_CREAT | O_EXCL`).
* **Build & Usage:**
  ```bash
  # Recommended build via makefile:
  make -C src datagen

  # Or compile manually:
  gcc -O3 -march=native -pthread -DUSE_PEXT -DNNUE_MIRROR=1 -DNNUE_KING_BUCKETS=4 \
      -DEVALFILE='"src/weights/quantised.bin"' \
      -Isrc tools/datagen.c $(ls src/*.c | grep -v src/main.c) \
      -lm -o tools/datagen

  ./tools/datagen --out tools/nnue_project/data_gen/gen_s1.bin --nodes 5000 --positions 1000000 --seed 1
  ```

### 2. [`run_datagen.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/run_datagen.py)
* **Purpose:** Multi-process coordinator that launches one `tools/datagen` process per CPU core, manages unique seeds and non-conflicting output files, and reports aggregate throughput and progress.
* **Usage:**
  ```bash
  # Generate 1,000,000 quiet positions across 8 workers at 5,000 nodes/move:
  python3 tools/run_datagen.py --workers 8 --positions 1000000 --nodes 5000 --out-dir tools/nnue_project/data_gen
  ```

### 3. [`test_datagen.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/test_datagen.py)
* **Purpose:** Automated unit and regression test suite verifying Phase 2 datagen determinism (identical binary files from same seed), quietness and in-check filtering via `python-chess`, WDL flag integrity, and NNUE protection guards.
* **Usage:**
  ```bash
  python3 tools/test_datagen.py
  ```

### 4. [`selfplay_nnue.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/selfplay_nnue.py)
* **Purpose:** Multi-threaded self-play game generator that runs GOOB directly via UCI and writes positions into the 68-byte packed binary format consumed by [`train.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/train.py).
* **Key Features:**
  * Runs multiple engine processes concurrently.
  * Plays moves from an opening book (e.g. UHO / EPD book).
  * Automatically filters out non-quiet positions: checks (`is_check()`), captures (`is_capture()`), and pawn promotions (`promotion is not None`).
  * Filters out forced mate scores and extreme centipawn evaluations ($|\text{cp}| > 3000$).
  * Enforces per-game position caps to prevent long endgames from dominating training batches.
  * Encodes final game outcomes (White win, Draw, Black win) into record flags to enable WDL target blending during NNUE training.
* **Usage:**
  ```bash
  python3 tools/selfplay_nnue.py --n-train 100000 --n-val 5000 --depth 20 --workers 8 --out-dir tools/nnue_project/data
  ```

### 3. [`datagen.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/datagen.py)
* **Purpose:** Generates self-play games outputting EPD lines formatted as `FEN;result` (with results from White's perspective), formerly used by the legacy hand-crafted-evaluation tuner. It still runs against the current engine.
* **Usage:**
  ```bash
  python3 tools/datagen.py <nodes_per_move> <total_games> <threads> <output_file> <opening_book>
  # Example:
  python3 tools/datagen.py 5000 20000 8 dataset.epd book.epd
  ```

### 3. [`trace_search.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/trace_search.py)
* **Purpose:** Search telemetry analysis and regression detection tool. Runs engine benchmarks under the `trace` command, collects over 50 internal counters, checks heuristic cutoff rates against expected ranges, and performs side-by-side comparisons of engine revisions.
* **Key Features:**
  * Validates trigger rates for RFP (Beta Pruning), NMP, IIR, ProbCut, LMP, Futility, Singular Extensions, and Surprise-SRD.
  * Warns if heuristics are over-pruning (causing tactical blindness) or under-pruning (wasting search nodes).
  * Saves and compares JSON telemetry profiles.
* **Usage:**
  ```bash
  # Run 30-position benchmark at depth 8:
  python3 tools/trace_search.py --bench 8

  # Save baseline profile:
  python3 tools/trace_search.py --bench 8 --save-json trace_baseline.json

  # Compare baseline against tuned version:
  python3 tools/trace_search.py --compare trace_baseline.json trace_tuned.json
  ```

### 4. [`test_incremental.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/test_incremental.c)
* **Purpose:** C unit test verifying that incremental accumulator updates, Finny table cache refreshes, and true from-scratch refreshes match bit-for-bit across complex game sequences (castling including `O-O-O`, king d/e boundary walks and captures, 4-bucket boundary transitions, promotions, en-passant, captures, multi-ply lazy crossings, `EvalFile` reload cache invalidation, and 50 random games) and that move unmaking ([`takeMove()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makemove.c#L438-L480)) cleanly returns accumulators to the initial position. Accepts an optional `.bin` network path on the command line.
* **Compilation & Execution:**
  ```bash
  # from the repository root; builds tools/test_incremental (ignored by git, or output to /tmp)
  gcc -O2 -march=native -pthread -DUSE_PEXT -DNNUE_MIRROR=1 -DNNUE_KING_BUCKETS=4 -DEVALFILE='"src/weights/quantised.bin"' -Isrc \
      tools/test_incremental.c $(ls src/*.c | grep -v src/main.c) -lm -o tools/test_incremental
  ./tools/test_incremental [optional_net.bin]
  ```

### 5. [`test_nnue_comprehensive.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/test_nnue_comprehensive.py)
* **Purpose:** Compares the engine's `eval` output with a Python re-implementation of the quantized integer forward pass and with a PyTorch float model built from the same weights, on positions covering every output bucket. If a Schoenemann binary exists at `Schoenemann-0.5.0/src/null` its raw eval is compared too; otherwise that column is skipped.
* **Usage** (from the repository root, after `make -C src native`; needs numpy and torch):
  ```bash
  python3 tools/test_nnue_comprehensive.py
  ```

### 6. Classical Evaluation Tuner (`tuner.c`, retired)
* **Status: retired.** The Texel tuner for GOOB's pre-NNUE hand-crafted evaluation has been removed from `tools/` as all classical evaluation arrays were replaced by NNUE.

### 7. SPRT Testing Suite ([`tools/sprt/`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/sprt))
* **Purpose:** Automated engine strength and regression testing harness comparing modified engine builds against a baseline version.
* **Key Components:**
  * [`run_sprt.sh`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/sprt/run_sprt.sh): Cutechess-cli runner executing SPRT matches with standard parameters (default 6+0.06s time control, 10 concurrent games capped at the CPU count, 1 thread per engine (`THREADS=` for SMP changes), 32MB hash, `tools/book.epd` openings).
  * [`bench.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/sprt/bench.py): Fast 12-position fixed-depth sanity benchmark verifying node counts, move stability, and lack of crashes.
  * [`README.md`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/sprt/README.md): Detailed testing guide, SPRT hypothesis bounds, and branch merge guidelines.
* **Usage:**
  ```bash
  # Sanity check:
  python3 tools/sprt/bench.py tools/sprt/bin/GOOB-candidate 12

  # Run cutechess SPRT match:
  tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-candidate tools/sprt/bin/GOOB-base
  ```

### 8. SPSA Search Tuner ([`tools/spsa/`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/spsa))
* **Purpose:** Tunes the search constants of [`src/tune.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/tune.h) (pruning margins, depth limits, NMP/LMR formula terms) by self-play SPSA, OpenBench style, with cutechess-cli.
* **Key Components:**
  * [`spsa.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/spsa/spsa.py): `run` (start/resume a run with parallel workers), `show` (tuned vs default values), `apply` (write the tuned values into `src/tune.h`). Needs a `make tune` engine build. Run state goes to `tools/spsa/runs/<name>/` (git-ignored).
  * [`README.md`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/spsa/README.md): step-by-step guide (how SPSA works, where the tuned values are, applying and SPRT-verifying them, choosing parameters, options).
* **Usage:**
  ```bash
  make -C src tune
  python3 tools/spsa/spsa.py run pruning1 --tc 5+0.05 --workers 6
  python3 tools/spsa/spsa.py show pruning1     # tuned values
  python3 tools/spsa/spsa.py apply pruning1    # write them into `src/tune.h`
  ```

### 9. Matchless Elo Estimator & EPD Benchmarks ([`tools/epd_test.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/epd_test.py))
* **Purpose:** Runs standard Extended Position Description (EPD) benchmark suites against the engine over UCI to measure tactical solving accuracy, strategic positional play, and estimate playing strength / Elo without needing engine-vs-engine tournament matches.
* **Included Suites ([`tools/suites/`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/suites)):**
  * `bt2630.epd`: Bednorz-Teschner 2630 classic 30-position benchmark with Bednorz-Teschner Elo rating formula.
  * `sts1.epd`: Strategic Test Suite Volume 1 (Undermining, 100 positions) for rapid strategic profiling.
  * `sts1-15.epd`: Complete 1500-position Strategic Test Suite across all 15 strategic themes by Dann Corbit & Swaminathan Natarajan, with Ferdinand Mosca's calibrated CCRL 40/4 linear regression formula (`Elo = 44.523 * Score% - 242.85`).
  * `bratko-kopec.epd`: Canonical 24-position Bratko-Kopec Test with separate tactical vs positional balance breakdowns and Elo estimation (`Elo = 1300 + 55 * Solved`).
  * `wac.epd`: Win At Chess 300-position tactical puzzle suite.
* **Usage:**
  ```bash
  # Quick test on BT-2630 (500ms per position):
  python3 tools/epd_test.py --suite bt2630 --movetime 500

  # Strategic Test Suite (STS-1) at fixed depth 10:
  python3 tools/epd_test.py --suite sts1 --depth 10

  # Bratko-Kopec Test (tactical vs positional balance):
  python3 tools/epd_test.py --suite bk --movetime 1000

  # Full 1500-position Strategic Test Suite (CCRL 40/4 Elo estimate):
  python3 tools/epd_test.py --suite sts1-15 --movetime 100 --save-json sts_results.json
  ```

---

## The NNUE Subproject (`tools/nnue_project/`)

The [`tools/nnue_project/`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project) directory houses the complete neural network training pipeline.

### Directory Structure
```
tools/nnue_project/
├── data_selfplay/        # Self-play records (train1.bin, val1.bin), tracked in git
├── checkpoints/          # Created by training: PyTorch (.pt) and exported binary (.bin) weights
├── data/                 # Created by data prep: binary training datasets (train.bin, val.bin)
├── data_json/            # Raw JSONL datasets (e.g. lichess_db_eval.jsonl.zst)
└── scripts/
    ├── model.py          # PyTorch Schoenemann NNUE topology + factorized king buckets + DEFAULT_ARCH metadata
    ├── nnue_dataset.py   # Memmap 68-byte dataset loader & collator (WDL blending, king mirroring, king buckets, flip augmentation)
    ├── train.py          # Training loop with Adam/AdamW, LR scheduling, weight clipping, --wdl-lambda, --mirror, --king-buckets, --init-from-unbucketed
    ├── export_weights.py # Quantizer folding factorized buckets & exporting .pt to quantised.bin with GOOBNNUE v2 trailer
    ├── fen_utils.py      # FEN to 68-byte record encoder/decoder
    ├── prepare_data.py   # Streams the Hugging Face Lichess evaluations to binary
    ├── prepare_data_jsonl.py # Streams a local Lichess JSONL(.zst) to binary, quiet positions only
    ├── append_data.py    # Appends new, de-duplicated Hugging Face positions to an existing train/val set
    ├── clear_cache.py    # Repairs caches from the original prepare_data.py (one label per position, no val leakage, optional shuffle)
    ├── check_net.py      # Checks an exported net: trailer, size, int16 limits, bit-exact reference & mirror evals
    ├── test_net.py       # Evaluates FENs with a saved PyTorch checkpoint
    ├── scale_test.py     # Quantization headroom of a checkpoint (largest safe QA / QB)
    └── mix_data.py       # Mixes native self-play generated data with base Lichess data for D1/D2/D3 ratios
```

### Script Reference

* **[`model.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/model.py):**
  * Topology: `(768xK -> 1024)x2 -> 1x8 buckets` ($K \in \{1, 4\}$, production default $K=4$), carrying `arch = {"mirror": bool, "king_buckets": int}` metadata (`DEFAULT_ARCH = {"mirror": False, "king_buckets": 1}`).
  * Feature transformer: Shared weights for both perspectives ($K \times 768$ inputs $\to 1024$ hidden outputs). When `king_buckets > 1`, uses factorized virtual features (`ft_buckets[K, 768, 1024] + ft_shared[768, 1024]`) folded into a flat `(K * 768, 1024)` table at export time.
  * Activation: Squared Clipped ReLU ($\text{clamp}(x, 0, 1)^2$).
  * Buckets: 8 material buckets selected by piece count:
    $$\text{bucket} = \text{clamp}\left(\frac{\text{pieces} - 2}{4}, 0, 7\right)$$
  * Forward pass uses a single batched matrix multiplication (`x @ output_weights.t()`) followed by sample bucket gathering.
* **[`nnue_dataset.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/nnue_dataset.py):**
  * Memory-mapped dataset handler reading 68-byte records via `np.memmap`.
  * Implements `__getitems__` for zero-copy batched array reading and vectorized collation directly into feature indices, supporting perspective horizontal king mirroring (`mirror=True`), 4 king buckets (`king_buckets=4`), and 50% horizontal flip augmentation (`flip_augment=True`).
* **[`train.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/train.py):**
  * Supports training from scratch, fine-tuning existing checkpoints (`--init`), or warm-starting a 4-bucket network from a 1-bucket checkpoint (`--init-from-unbucketed`, `--king-buckets 4`, `--optimizer adamw`, `--bucket-decay`), with mutually exclusive `--mirror` and `--flip-augment` options.
  * Saves `arch` metadata inside checkpoints and refuses `--resume` if the checkpoint architecture differs from the command-line flags.
  * Uses [`ResumableBatchSampler`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/train.py#L64-L107) for deterministic, mid-epoch checkpoint resumption.
  * Enforces weight clipping ([`clip_weights(model, limit=1.98)`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/train.py#L45-L61)) so that combined feature weights (`ft_shared + ft_buckets[b]`) and output weights never overflow 16-bit integer boundaries in C SIMD kernels.
* **[`export_weights.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/export_weights.py):**
  * Folds factorized virtual features (`ft_buckets + ft_shared[None, :, :]`) into a flat `(K * 768, 1024)` weight table, logs bucket residual statistics (`max |delta|`, `% |delta| < 1/QA`), and quantizes floating-point weights into `int16` binary format:
    * Feature weights and bias: $\times 255$ (`QA`)
    * Output weights: $\times 64$ (`QB`)
    * Output bias: $\times (255 \times 64 = 16320)$ (`QA * QB`)
  * Writes the 48-byte `GOOBNNUE` v2 architecture trailer (`<8sIIIII20s`) encoding `mirror`, `king_buckets`, `hidden_size`, and `output_buckets`.
  * Validates whether any output weight exceeds `FAST_OUT_W_LIMIT` ($\pm 128$) so the engine can safely run its fast SCReLU SIMD kernel.
* **[`fen_utils.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/fen_utils.py):**
  * Packs and unpacks 68-byte binary records containing 64 board squares, side to move, 16-bit signed eval, and flags.
* **[`mix_data.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/mix_data.py):**
  * Concatenates native self-play generated binary datasets (`data_gen/`) with base Lichess data (`train.bin`) at configured ratios (`--gen-share 0.25` for D1, `0.50` for D2), extracts held-out validation sets (`val_gen.bin`), and shuffles positions uniformly.

---

## Step-by-Step Workflows

### Workflow A: Generate Native Self-Play Training Data & Train with Mixed Sets (Phase 2)

#### 1. Compile Native Datagen Binary
```bash
gcc -O3 -march=native -pthread -DUSE_PEXT -DNNUE_MIRROR=1 -DNNUE_KING_BUCKETS=4 \
    -DEVALFILE='"src/weights/quantised.bin"' \
    -Isrc tools/datagen.c $(ls src/*.c | grep -v src/main.c) \
    -lm -o tools/datagen
```

#### 2. Run Multi-Core Data Generation
Generate self-play positions with WDL outcomes directly via native in-process search:
```bash
# Run across all CPU cores generating e.g. 10,000,000 positions at 5000 nodes/move:
python3 tools/run_datagen.py --workers $(nproc) --positions 10000000 --nodes 5000 --out-dir tools/nnue_project/data_gen
```

#### 3. Mix Generated Self-Play Data with Base Lichess Data
```bash
# D1 Mix (25% generated share) + held-out 50k generated validation set:
python3 tools/nnue_project/scripts/mix_data.py \
    --gen-data tools/nnue_project/data_gen \
    --base-data tools/nnue_project/data/train.bin \
    --gen-share 0.25 \
    --val-out tools/nnue_project/checkpoints/exp_datagen/val_gen.bin \
    --val-count 50000 \
    --out tools/nnue_project/checkpoints/exp_datagen/d1_train.bin

# D2 Mix (50% generated share):
python3 tools/nnue_project/scripts/mix_data.py \
    --gen-data tools/nnue_project/data_gen \
    --base-data tools/nnue_project/data/train.bin \
    --gen-share 0.50 \
    --out tools/nnue_project/checkpoints/exp_datagen/d2_train.bin
```

#### 4. Fine-Tune with WDL Blending
```bash
cd tools/nnue_project/scripts

# Train D1 (25% mix, --wdl-lambda 0.75):
python3 train.py \
    --train ../checkpoints/exp_datagen/d1_train.bin \
    --val ../data/val.bin \
    --init ../checkpoints/exp_4bkt/nnue.pt \
    --mirror --king-buckets 4 --optimizer adamw --bucket-decay 1e-4 \
    --lr 1e-4 --epochs 8 --wdl-lambda 0.75 \
    --out-best ../checkpoints/exp_datagen/d1/nnue.pt \
    --out-last ../checkpoints/exp_datagen/d1/last.pt

# Train D3 (D2 mix, --wdl-lambda 0.50):
python3 train.py \
    --train ../checkpoints/exp_datagen/d2_train.bin \
    --val ../data/val.bin \
    --init ../checkpoints/exp_4bkt/nnue.pt \
    --mirror --king-buckets 4 --optimizer adamw --bucket-decay 1e-4 \
    --lr 1e-4 --epochs 8 --wdl-lambda 0.50 \
    --out-best ../checkpoints/exp_datagen/d3/nnue.pt \
    --out-last ../checkpoints/exp_datagen/d3/last.pt
```

### Workflow B: Stream & Prepare Lichess Evaluated Positions
If you have downloaded `lichess_db_eval.jsonl.zst` into `tools/nnue_project/data_json/`:
```bash
cd tools/nnue_project/scripts
python3 prepare_data_jsonl.py \
    --n-val 1000000 \
    --min-depth 20 \
    --out-dir ../data

# Or append new unique positions from an updated Lichess dump (e.g. October):
python3 prepare_data_jsonl.py \
    --input ../data_json/lichess_db_eval.jsonl.zst \
    --out-dir ../data \
    --append \
    --dedup \
    --n-val 0
```

### Workflow C: Train an NNUE Network from Scratch or Fine-Tune
```bash
cd tools/nnue_project/scripts

# Train from scratch (with horizontal king mirroring and 4 factorized king buckets):
python3 train.py \
    --train ../data/train.bin \
    --val ../data/val.bin \
    --mirror \
    --king-buckets 4 \
    --optimizer adamw \
    --bucket-decay 1e-4 \
    --epochs 20 \
    --batch-size 8192 \
    --lr 1e-3 \
    --clip 1.98 \
    --out-best ../checkpoints/nnue_best.pt \
    --out-last ../checkpoints/last.pt

# Fine-tune an existing 4-bucket network:
python3 train.py \
    --train ../data/train.bin \
    --val ../data/val.bin \
    --init ../checkpoints/nnue_best.pt \
    --mirror \
    --king-buckets 4 \
    --optimizer adamw \
    --bucket-decay 1e-4 \
    --epochs 10 \
    --batch-size 8192 \
    --lr 2e-4 \
    --clip 1.98 \
    --out-best ../checkpoints/nnue_ft.pt \
    --out-last ../checkpoints/last_ft.pt
```

### Workflow D: Quantize & Export Weights for the Engine
```bash
cd tools/nnue_project/scripts
python3 export_weights.py \
    --checkpoint ../checkpoints/nnue_best.pt \
    --out ../checkpoints/quantised.bin

# Copy into the engine source directory:
cp ../checkpoints/quantised.bin ../../../src/weights/quantised.bin

# Recompile native engine binary with embedded network:
make -C ../../../src clean
make -C ../../../src native
```

### Workflow E: Verify NNUE Correctness
Incremental accumulator updates and unmaking moves must match full recalculation (`test_incremental.c`, build command in section 4), and the engine's eval must match the reference forward pass:
```bash
/tmp/test_incremental
python3 tools/test_nnue_comprehensive.py
```

### Workflow F: Trace & Benchmark Search Heuristics
Run a search trace benchmark to evaluate pruning efficiency:
```bash
# Compile engine with trace telemetry:
make -C src trace

# Run diagnostic benchmark:
python3 tools/trace_search.py --bench 8
```

### Workflow G: Classical Evaluation Tuning (historical note)
GOOB previously used Texel tuning for hand-crafted evaluation terms (PSTs, piece values, king safety). With the transition to pure NNUE evaluation, hand-crafted evaluation code and `tools/tuner.c` have been retired. Refer to Workflows C and D for the active NNUE training pipeline.

### Workflow H: Tune Search Constants with SPSA
1. Build the tunable engine and keep a normal baseline binary:
   ```bash
   make -C src tune
   make -C src native && cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-base
   ```
2. Tune (Ctrl-C pauses, the same command resumes; `--include`/`--exclude` pick parameters):
   ```bash
   python3 tools/spsa/spsa.py run pruning1 --tc 5+0.05 --workers 6 --iterations 5000
   ```
3. Write the values into `src/tune.h`, rebuild and SPRT against the baseline:
   ```bash
   python3 tools/spsa/spsa.py apply pruning1
   make -C src native && cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-tuned
   tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-tuned tools/sprt/bin/GOOB-base spsa-pruning1 6+0.06 6
   ```

### Workflow I: Estimate Engine Elo Strength via EPD Test Suites
Test engine revisions quickly without waiting for hundreds of game matches:

1. **Tactical Strength (BT-2630):**
   ```bash
   python3 tools/epd_test.py --suite bt2630 --movetime 1000
   ```
   Outputs classic Bednorz-Teschner Elo rating and solved-scale Elo estimate.

2. **Rapid Strategic Profiling (STS-1):**
   ```bash
   python3 tools/epd_test.py --suite sts1 --depth 10
   ```
   Evaluates 100 strategic positions with move point scoring (1–10 points) and CCRL 40/4 Elo mapping.

3. **Full Matchless Elo Rating (STS 1–15, 1500 positions):**
   ```bash
   python3 tools/epd_test.py --suite sts1-15 --movetime 100 --save-json sts_profile.json
   ```
   Calculates CCRL 40/4 Elo estimate (`Elo = 44.523 * Score% - 242.85`) across all 15 strategic themes with category breakdown.

4. **Tactical vs Positional Balance (Bratko-Kopec):**
   ```bash
   python3 tools/epd_test.py --suite bk --movetime 500
   ```
   Reports separate Tactical vs Positional accuracy percentages and estimated Elo (`1300 + 55 * Solved`).

---

## Data Formats & Specifications

### 68-Byte Packed Binary Record (`RECORD_STRUCT = "<64sBhB"`)
Each training position in `train.bin` and `val.bin` is packed into exactly 68 bytes:

| Byte Offset | Field | Type | Description |
| :--- | :--- | :--- | :--- |
| `0..63` | Board Squares | `uint8[64]` | Piece codes: `0`=empty, `1..6`=white P..K, `7..12`=black p..k. |
| `64` | Side to Move | `uint8` | `0` = White, `1` = Black. |
| `65..66` | Evaluation | `int16` | Little-endian centipawn score from White's perspective. |
| `67` | Flags | `uint8` | Bit 0: Mate score indicator flag. Bits 1..2: Game result (`00`=unknown/pure eval, `01`=White win, `10`=Draw, `11`=Black win). |

### Quantized Network File (`quantised.bin`)
Total size: **1,607,744 bytes** (~1.53 MB):

| Byte Range | Section | Size | Quantization Scale |
| :--- | :--- | :--- | :--- |
| `0..1,572,863` | Feature Weights (`ft.weight`) | `int16[768 * 1024]` | $\times 255$ (`QA`) |
| `1,572,864..1,574,911` | Feature Biases (`ft_bias`) | `int16[1024]` | $\times 255$ (`QA`) |
| `1,574,912..1,607,679` | Output Weights (`output_weights`) | `int16[8 * 2048]` | $\times 64$ (`QB`) |
| `1,607,680..1,607,695` | Output Biases (`output_biases`) | `int16[8]` | $\times 16320$ (`QA * QB`) |
| `1,607,696..1,607,743` | Architecture Trailer | `bytes[48]` | `GOOBNNUE` v2 trailer (`<8sIIIII20s`: magic `"GOOBNNUE"`, version `2`, flags `bit 0 = mirror`, `king_buckets`, `hidden_size`, `output_buckets`, 20 zero bytes) or legacy `b"bullet" * 8` |
