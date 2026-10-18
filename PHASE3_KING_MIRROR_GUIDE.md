# Phase 3: Horizontal King Mirroring – Step-by-Step Guide

Run all commands below from the repository root (`/home/gabriel/Desktop/ChessEngineGoobC`).

---

## Step 1: Fine-tune the Mirrored Network (Variant M)

Warm-start from your accepted Phase 1 checkpoint (`exp_control/nnue.pt`) for 10 epochs at `lr=3e-4` with `--mirror`:

```bash
mkdir -p tools/nnue_project/checkpoints/exp_mirror

tools/.venv/bin/python3 tools/nnue_project/scripts/train.py \
    --train tools/nnue_project/data/train.bin \
    --val tools/nnue_project/data/val.bin \
    --init tools/nnue_project/checkpoints/exp_control/nnue.pt \
    --mirror \
    --epochs 10 \
    --batch-size 8192 \
    --lr 3e-4 \
    --wdl-lambda 0.0 \
    --clip 1.98 \
    --seed 42 \
    --out-best tools/nnue_project/checkpoints/exp_mirror/nnue.pt \
    --out-last tools/nnue_project/checkpoints/exp_mirror/last.pt
```

> **Note:** Epoch 1 starts at a higher loss (~0.005–0.010) because kings on `a..d` in the unmirrored net saw raw `e..h` piece squares; it recovers within 1–2 epochs and converges below the unmirrored validation MSE by epoch 6–10.

---

## Step 2: Export & Verify the Mirrored Network

```bash
tools/.venv/bin/python3 tools/nnue_project/scripts/export_weights.py \
    --checkpoint tools/nnue_project/checkpoints/exp_mirror/nnue.pt \
    --out tools/nnue_project/checkpoints/exp_mirror/quantised.bin

tools/.venv/bin/python3 tools/nnue_project/scripts/check_net.py \
    --net tools/nnue_project/checkpoints/exp_mirror/quantised.bin \
    --checkpoint tools/nnue_project/checkpoints/exp_mirror/nnue.pt \
    --pipeline
```

---

## Step 3: Build Baseline (`GOOB-base`) and Mirrored Candidate (`GOOB-mirror`)

Because `src/weights/quantised.bin` is currently your Phase 1 unmirrored net, build `GOOB-base` with `NNUE_MIRROR=0`, and build `GOOB-mirror` with `NNUE_MIRROR=1` (the default):

```bash
mkdir -p tools/sprt/bin

# 1. Build Phase 1 baseline (unmirrored)
make -C src clean
make -C src native NNUE_MIRROR=0 -j
cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-base

# 2. Build Phase 3 candidate (mirrored)
make -C src clean
make -C src native NNUE_MIRROR=1 EVALFILE="$(pwd)/tools/nnue_project/checkpoints/exp_mirror/quantised.bin" -j
cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-mirror
```

---

## Step 4: Run Incremental Test & Sanity Bench

```bash
gcc -O2 -march=native -pthread -DUSE_PEXT -DNNUE_MIRROR=1 \
    -DEVALFILE="\"$(pwd)/tools/nnue_project/checkpoints/exp_mirror/quantised.bin\"" \
    -Isrc tools/test_incremental.c $(ls src/*.c | grep -v src/main.c) -lm -o /tmp/test_inc_mirror
/tmp/test_inc_mirror

python3 tools/sprt/bench.py tools/sprt/bin/GOOB-base 12
python3 tools/sprt/bench.py tools/sprt/bin/GOOB-mirror 12
```

---

## Step 5: Run STC & LTC SPRT

```bash
# STC (6+0.06)
tools/sprt/run_sprt.sh \
    tools/sprt/bin/GOOB-mirror \
    tools/sprt/bin/GOOB-base \
    nnue-hmirror-stc 6+0.06 10

# LTC (12+0.1) after STC passes
tools/sprt/run_sprt.sh \
    tools/sprt/bin/GOOB-mirror \
    tools/sprt/bin/GOOB-base \
    nnue-hmirror-ltc 12+0.1 10
```

---

## Step 6: Promote Weights After Passing STC & LTC

Once `GOOB-mirror` passes STC and LTC:

```bash
cp src/weights/quantised.bin src/weights/quantised_phase1_control.bin
cp tools/nnue_project/checkpoints/exp_mirror/quantised.bin src/weights/quantised.bin
make -C src clean
make -C src native -j
```
