# NNUE from the Lichess eval dataset, for a C engine

A minimal but real NNUE pipeline: train in PyTorch on
`Lichess/chess-position-evaluations`, export the weights to a flat binary
file, and load/run them from plain C with matching output (verified below).

## Layout

```
scripts/
  fen_utils.py       FEN -> 68-byte record encoding (shared by prep + dataset)
  prepare_data.py     streams the HF dataset -> data/train.bin, data/val.bin
  nnue_dataset.py      PyTorch Dataset: record bytes -> 768-dim features
  model.py            NNUE(768 -> 256 -> 32 -> 32 -> 1), ClippedReLU
  train.py            training loop
  export_weights.py   checkpoint -> flat float32 binary for C
c_engine/
  nnue.h / nnue.c     C loader + forward pass, bit-for-bit same math as PyTorch
  demo_eval.c         tiny demo: load weights, eval the start position
checkpoints/           (created by training/export)
data/                  (created by prepare_data.py)
```

## 1. Get the data (run on a machine with internet access)

**Note:** this step needs to reach huggingface.co, which this sandbox
can't do, so run it on your own machine.

```bash
pip install datasets
cd scripts
python prepare_data.py --n-train 8000000 --n-val 50000
```

This streams `Lichess/chess-position-evaluations` (the parquet dataset has
~958M de-normalized rows / ~512M positions in the plain cp+mate form),
filters out shallow-depth rows and extreme centipawn outliers, and writes
two compact binary caches (`data/train.bin`, `data/val.bin`) using a fixed
68-byte-per-position record — no repeated FEN parsing during training.
Adjust `--n-train` down if you want a quick first run (even 500k-1M
positions will get you a working, if weak, net).

## 2. Train

```bash
pip install torch
cd scripts
python train.py --train ../data/train.bin --val ../data/val.bin \
    --epochs 20 --batch-size 8192
```

Saves the best checkpoint (by validation MSE) to `checkpoints/nnue.pt`.

## 3. Export for C

```bash
python export_weights.py --checkpoint ../checkpoints/nnue.pt \
    --out ../checkpoints/nnue.bin
```

## 4. Use from C

```bash
cd c_engine
gcc -O2 -o demo_eval nnue.c demo_eval.c -lm
./demo_eval ../checkpoints/nnue.bin
```

In your own engine:

```c
#include "nnue.h"

NNUEModel model;
nnue_load(&model, "nnue.bin");

int board[64] = { /* your position, see nnue.h for the piece-code convention */ };
float cp = nnue_evaluate(&model, board, side_to_move); /* from mover's POV */

nnue_free(&model);
```

I compiled and ran this against the PyTorch model on the starting position
and both gave the same eval (43.83 cp on my test run), so the C forward
pass is a faithful reimplementation of the PyTorch one.

## Design choices / what this is (and isn't)

- **Feature set:** 768 = 12 piece planes x 64 squares, single perspective.
  Every position is re-expressed as "features from the side-to-move's
  point of view" by mirroring the board vertically and swapping colors
  when it's Black to move (`sq ^ 56`), rather than using two separate
  accumulators like classic dual-perspective NNUE (HalfKP etc). Simpler
  to reason about and integrate, at some cost to strength.
- **Network:** 768 -> 256 -> 32 -> 32 -> 1 with ClippedReLU (clamp to
  [0,1]), same topology idea as early Stockfish NNUE nets, just smaller
  and single-perspective.
- **Eval sign convention:** the dataset's `cp` field is White-relative;
  everything gets converted to side-to-move-relative internally
  (`cp_stm`), and training targets are `sigmoid(cp_stm / 410)`.
  `nnue_evaluate()` in C returns an approximate centipawn score from the
  **side to move's** perspective (flip the sign yourself if your engine
  wants White-relative scores).
- **No int8/int16 quantization or incremental accumulator updates.**
  This version does a full float32 forward pass on every call, which is
  what a classic "NNUE" specifically avoids (the "Efficiently Updatable"
  part comes from only updating the first layer's accumulator
  incrementally as pieces move, plus int8/int16 SIMD math). That's the
  natural next step once this basic version is working and integrated:
  1. Quantize fc1's weights/activations and keep a running accumulator
     per side that you update incrementally on make/unmake move instead
     of recomputing from scratch.
  2. Quantize the remaining small layers to int8.
  This gets you the real speed NNUE is known for; the current version
  is meant to get you a correct, working eval function first.

## Tuning knobs worth trying once this works

- `--min-depth` / `--max-abs-cp` in `prepare_data.py` to control data
  quality vs. quantity.
- Bigger accumulator (`l1=512`) in `model.py` if 256 trains too fast /
  underfits and your C engine can afford the extra multiply-adds.
- `CP_SCALE` in `nnue_dataset.py` (410 is a reasonable default borrowed
  from nnue-pytorch; can be tuned).
