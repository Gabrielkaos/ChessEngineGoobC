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
   * [Workflow E: Verify NNUE Accumulator Correctness](#workflow-e-verify-nnue-accumulator-correctness)
   * [Workflow F: Trace & Benchmark Search Heuristics](#workflow-f-trace--benchmark-search-heuristics)
   * [Workflow G: Classical Evaluation Tuning (Texel Tuner)](#workflow-g-classical-evaluation-tuning-texel-tuner)
5. [Data Formats & Specifications](#data-formats--specifications)

---

## Overview & Tooling Ecosystem

The tooling is divided into three primary operational domains:

1. **NNUE Training & Quantization Pipeline:** Centered in [`nnue_project/`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project), this provides dataset conversion, memory-mapped data loaders, PyTorch training with weight bounds enforcement, and binary weight export matching the C engine's SIMD layout.
2. **Self-Play & Dataset Generation:** Scripts ([`selfplay_nnue.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/selfplay_nnue.py) and [`datagen.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/datagen.py)) that run the engine in parallel under UCI to harvest evaluated positions.
3. **Verification, Tuning & Telemetry:** Tools for testing incremental accumulator updates ([`test_incremental.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/test_incremental.c)), comparing C inference against PyTorch floating-point output ([`test_nnue_comprehensive.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/test_nnue_comprehensive.py)), profiling search prunings ([`trace_search.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/trace_search.py)), and optimizing evaluation parameters ([`tuner.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/tuner.c)).
4. **SPRT & Strength Testing Framework:** Centered in [`sprt/`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/sprt), this provides automated Cutechess-cli SPRT matches ([`run_sprt.sh`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/sprt/run_sprt.sh)), 12-position fixed-depth sanity benchmarks ([`bench.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/sprt/bench.py)), and testing guides ([`README.md`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/sprt/README.md)).

---

## Root Tools & Diagnostic Scripts

### 1. [`selfplay_nnue.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/selfplay_nnue.py)
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

### 2. [`datagen.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/datagen.py)
* **Purpose:** Generates self-play games outputting EPD lines formatted as `FEN;result` (with results from White's perspective) used by the classical evaluation tuner [`tuner.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/tuner.c).
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
* **Purpose:** C unit test verifying that incremental accumulator updates match complete accumulator refreshes bit-for-bit across complex game sequences (castling, promotions, en-passant, captures) and that move unmaking ([`takeMove()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makemove.c#L438-L480)) cleanly returns accumulators to the initial position.
* **Compilation & Execution:**
  ```bash
  ln -sf src/weights weights
  gcc -O3 -Isrc tools/test_incremental.c $(ls src/obj/native/*.o | grep -v main.o) -lm -o tools/test_incremental
  ./tools/test_incremental
  rm -f weights tools/test_incremental
  ```

### 5. [`test_nnue_comprehensive.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/test_nnue_comprehensive.py)
* **Purpose:** Cross-validates C integer inference directly against PyTorch floating-point and integer reference implementations across complex tactical, endgame, and quiet positions.
* **Usage:**
  ```bash
  python3 tools/test_nnue_comprehensive.py
  ```

### 6. [`tuner.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/tuner.c)
* **Purpose:** Standalone, high-speed Texel tuner implemented in C using OpenMP and the Adam optimizer to optimize classical Hand-Crafted Evaluation (HCE) parameters (PSTs, passed pawns, king safety tables, mobility).
* **Compilation:**
  ```bash
  gcc -O3 -fopenmp -Isrc tools/tuner.c -lm -o tools/tuner
  ```

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

---

## The NNUE Subproject (`tools/nnue_project/`)

The [`tools/nnue_project/`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project) directory houses the complete neural network training pipeline.

### Directory Structure
```
tools/nnue_project/
├── checkpoints/          # PyTorch (.pt) and exported binary (.bin) weights
├── data/                 # Binary training datasets (train.bin, val.bin)
├── data_json/            # Raw JSONL datasets (e.g. lichess_db_eval.jsonl.zst)
└── scripts/
    ├── model.py          # PyTorch Schoenemann NNUE topology
    ├── nnue_dataset.py   # Memmap 68-byte dataset loader & collator with WDL target blending
    ├── train.py          # Training loop with Adam, LR scheduling, weight clipping, and --wdl-lambda
    ├── export_weights.py # Quantizer exporting .pt to quantised.bin
    ├── fen_utils.py      # FEN to 68-byte record encoder/decoder
    ├── prepare_data.py   # Streams HuggingFace / PGN data to binary
    ├── prepare_data_jsonl.py # Streams Lichess JSONL.zst to binary
    ├── append_data.py    # Merges multiple .bin datasets
    ├── clear_cache.py    # Cache validation and cleanup
    ├── check_net.py      # Inspects weight ranges and accumulator bounds
    ├── test_net.py       # Validation tester for saved models
    └── scale_test.py     # Evaluates output score distribution scaling
```

### Script Reference

* **[`model.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/model.py):**
  * Topology: `(768 -> 1024)x2 -> 1x8 buckets`.
  * Feature transformer: Shared weights for both perspectives ($2 \times 6 \times 64 = 768$ inputs $\to 1024$ hidden outputs).
  * Activation: Squared Clipped ReLU ($\text{clamp}(x, 0, 1)^2$).
  * Buckets: 8 material buckets selected by piece count:
    $$\text{bucket} = \text{clamp}\left(\frac{\text{pieces} - 2}{4}, 0, 7\right)$$
  * Forward pass uses a single batched matrix multiplication (`x @ output_weights.t()`) followed by sample bucket gathering.
* **[`nnue_dataset.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/nnue_dataset.py):**
  * Memory-mapped dataset handler reading 68-byte records via `np.memmap`.
  * Implements `__getitems__` for zero-copy batched array reading and vectorized collation directly into feature indices.
* **[`train.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/train.py):**
  * Supports training from scratch or fine-tuning existing checkpoints.
  * Uses [`ResumableBatchSampler`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/train.py#L57-L100) for deterministic, mid-epoch checkpoint resumption.
  * Enforces weight clipping ([`clip_weights(model, limit=1.98)`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/train.py#L45-L55)) to guarantee that output weights and accumulators do not overflow 16-bit integer boundaries in C SIMD kernels.
* **[`export_weights.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/export_weights.py):**
  * Quantizes floating-point weights into `int16` binary format:
    * Feature weights and bias: $\times 255$ (`QA`)
    * Output weights: $\times 64$ (`QB`)
    * Output bias: $\times (255 \times 64 = 16320)$ (`QA * QB`)
  * Validates whether any output weight exceeds `FAST_OUT_W_LIMIT` ($\pm 128$) so the engine can safely run its fast SCReLU SIMD kernel.
* **[`fen_utils.py`](file:///home/gabriel/Desktop/ChessEngineGoobC/tools/nnue_project/scripts/fen_utils.py):**
  * Packs and unpacks 68-byte binary records containing 64 board squares, side to move, 16-bit signed eval, and flags.

---

## Step-by-Step Workflows

### Workflow A: Generate Training Data via Self-Play
To generate fresh training data using the current engine binary:
```bash
python3 tools/selfplay_nnue.py \
    --n-train 500000 \
    --n-val 25000 \
    --depth 20 \
    --workers 8 \
    --book path/to/openings.epd \
    --out-dir tools/nnue_project/data
```

### Workflow B: Stream & Prepare Lichess Evaluated Positions
If you have downloaded `lichess_db_eval.jsonl.zst` into `tools/nnue_project/data_json/`:
```bash
cd tools/nnue_project/scripts
python3 prepare_data_jsonl.py \
    --n-val 1000000 \
    --min-depth 20 \
    --out-dir ../data
```

### Workflow C: Train an NNUE Network from Scratch or Fine-Tune
```bash
cd tools/nnue_project/scripts

# Train from scratch:
python3 train.py \
    --train ../data/train.bin \
    --val ../data/val.bin \
    --epochs 20 \
    --batch-size 8192 \
    --lr 1e-3 \
    --clip 1.98 \
    --out-best ../checkpoints/nnue_best.pt \
    --out-last ../checkpoints/last.pt

# Fine-tune an existing network on new data:
python3 train.py \
    --train ../data/train.bin \
    --val ../data/val.bin \
    --init ../checkpoints/nnue_best.pt \
    --epochs 8 \
    --batch-size 8192 \
    --lr 3e-4 \
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

### Workflow E: Verify NNUE Accumulator Correctness
Ensure that incremental updates and unmaking moves match full recalculation:
```bash
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

### Workflow G: Classical Evaluation Tuning (Texel Tuner)
1. Generate an EPD dataset:
   ```bash
   python3 tools/datagen.py 5000 50000 8 tools/dataset.epd path/to/book.epd
   ```
2. Build and run the Texel tuner:
   ```bash
   gcc -O3 -fopenmp -Isrc tools/tuner.c -lm -o tools/tuner
   ./tools/tuner
   ```

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
| `1,607,696..1,607,743` | Trailing Padding | `bytes[48]` | Bullet format compatibility string (`b"bullet" * 8`) |
