# Phase 2: Native Data Generation & Self-Play Fine-Tuning – Step-by-Step Guide

Run all commands below from the repository root (`/home/gabriel/Desktop/ChessEngineGoobC`).

---

## Step 1: Compile Native Data Generator & Verify

Compile the standalone native C generator linked directly with engine objects:

```bash
# Compile datagen binary (matches src/makefile flags and embeds src/weights/quantised.bin)
gcc -O3 -march=native -pthread -DUSE_PEXT -DPLATFORM_LINUX \
    -DNNUE_MIRROR=1 -DNNUE_KING_BUCKETS=4 \
    -DEVALFILE='"src/weights/quantised.bin"' \
    -Isrc tools/datagen.c $(ls src/*.c | grep -v 'src/main.c') -lm -o tools/datagen

# Run Phase 2 automated test suite (format, quietness, WDL flags, NNUE protection)
tools/.venv/bin/python3 tools/test_datagen.py
```

---

## Step 2: Generate Self-Play Data (Parallel Multi-Core)

Spawns one worker per logical CPU core with distinct seeds. Output `.bin` files are written to `tools/nnue_project/data_gen/`:

```bash
# Generate 5M positions at 5000 nodes/move across all CPU cores
tools/.venv/bin/python3 tools/run_datagen.py \
    --positions 5000000 \
    --nodes 5000 \
    --out-dir tools/nnue_project/data_gen
```

*(Optional: To generate a larger set such as 10M positions, change `--positions 10000000`.)*

---

## Step 3: Mix Self-Play Data with Base Data (D1 & D2)

Mix the generated data with the Lichess base training data (`tools/nnue_project/data/train.bin`). Also holds out 50k generated positions for evaluation:

```bash
mkdir -p tools/nnue_project/data_mixed

# Dataset D1: 75% Lichess base + 25% self-play
tools/.venv/bin/python3 tools/nnue_project/scripts/mix_data.py \
    --gen-data tools/nnue_project/data_gen \
    --base-data tools/nnue_project/data/train.bin \
    --out tools/nnue_project/data_mixed/train_d1.bin \
    --val-out tools/nnue_project/data_mixed/val_gen.bin \
    --val-count 50000 \
    --gen-share 0.25

# Dataset D2: 50% Lichess base + 50% self-play
tools/.venv/bin/python3 tools/nnue_project/scripts/mix_data.py \
    --gen-data tools/nnue_project/data_gen \
    --base-data tools/nnue_project/data/train.bin \
    --out tools/nnue_project/data_mixed/train_d2.bin \
    --gen-share 0.50
```

---

## Step 4: Fine-Tune Networks (Net-D1, Net-D2, Net-D3)

Fine-tune starting from your current Phase 4 production checkpoint (`exp_4bkt/nnue.pt` with `--mirror --king-buckets 4`):

```bash
mkdir -p tools/nnue_project/checkpoints/exp_datagen_d1
mkdir -p tools/nnue_project/checkpoints/exp_datagen_d2
mkdir -p tools/nnue_project/checkpoints/exp_datagen_d3

# Net-D1: 25% self-play, WDL lambda = 0.75
tools/.venv/bin/python3 tools/nnue_project/scripts/train.py \
    --train tools/nnue_project/data_mixed/train_d1.bin \
    --val tools/nnue_project/data/val.bin \
    --init tools/nnue_project/checkpoints/exp_4bkt/nnue.pt \
    --mirror \
    --king-buckets 4 \
    --optimizer adamw \
    --weight-decay 0.0 \
    --bucket-decay 1e-4 \
    --epochs 8 \
    --batch-size 8192 \
    --lr 1e-4 \
    --wdl-lambda 0.75 \
    --clip 1.98 \
    --seed 42 \
    --out-best tools/nnue_project/checkpoints/exp_datagen_d1/nnue.pt \
    --out-last tools/nnue_project/checkpoints/exp_datagen_d1/last.pt

# Net-D2: 50% self-play, WDL lambda = 0.75
tools/.venv/bin/python3 tools/nnue_project/scripts/train.py \
    --train tools/nnue_project/data_mixed/train_d2.bin \
    --val tools/nnue_project/data/val.bin \
    --init tools/nnue_project/checkpoints/exp_4bkt/nnue.pt \
    --mirror \
    --king-buckets 4 \
    --optimizer adamw \
    --weight-decay 0.0 \
    --bucket-decay 1e-4 \
    --epochs 8 \
    --batch-size 8192 \
    --lr 1e-4 \
    --wdl-lambda 0.75 \
    --clip 1.98 \
    --seed 42 \
    --out-best tools/nnue_project/checkpoints/exp_datagen_d2/nnue.pt \
    --out-last tools/nnue_project/checkpoints/exp_datagen_d2/last.pt

# Net-D3: 50% self-play, WDL lambda = 0.50 (higher game-outcome weight)
tools/.venv/bin/python3 tools/nnue_project/scripts/train.py \
    --train tools/nnue_project/data_mixed/train_d2.bin \
    --val tools/nnue_project/data/val.bin \
    --init tools/nnue_project/checkpoints/exp_4bkt/nnue.pt \
    --mirror \
    --king-buckets 4 \
    --optimizer adamw \
    --weight-decay 0.0 \
    --bucket-decay 1e-4 \
    --epochs 8 \
    --batch-size 8192 \
    --lr 1e-4 \
    --wdl-lambda 0.50 \
    --clip 1.98 \
    --seed 42 \
    --out-best tools/nnue_project/checkpoints/exp_datagen_d3/nnue.pt \
    --out-last tools/nnue_project/checkpoints/exp_datagen_d3/last.pt
```

---

## Step 5: Export & Verify Weights

```bash
# Export quantized binary weights for Net-D1
tools/.venv/bin/python3 tools/nnue_project/scripts/export_weights.py \
    --checkpoint tools/nnue_project/checkpoints/exp_datagen_d1/nnue.pt \
    --out tools/nnue_project/checkpoints/exp_datagen_d1/quantised.bin

# Verify bit-exact consistency and pipeline validation
tools/.venv/bin/python3 tools/nnue_project/scripts/check_net.py \
    --net tools/nnue_project/checkpoints/exp_datagen_d1/quantised.bin \
    --checkpoint tools/nnue_project/checkpoints/exp_datagen_d1/nnue.pt \
    --pipeline
```

---

## Step 6: Build Candidate & Run Incremental Test

```bash
mkdir -p tools/sprt/bin

# 1. Build current Phase 4 baseline engine (mirrored, 4 buckets)
make -C src clean
make -C src native NNUE_MIRROR=1 NNUE_KING_BUCKETS=4 -j8
cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-base

# 2. Build candidate engine with Net-D1 embedded
make -C src clean
make -C src native NNUE_MIRROR=1 NNUE_KING_BUCKETS=4 \
    EVALFILE="$(pwd)/tools/nnue_project/checkpoints/exp_datagen_d1/quantised.bin" -j8
cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-d1

# 3. Incremental + Finny cache test on the newly trained weights
gcc -O2 -march=native -pthread -DUSE_PEXT -DNNUE_MIRROR=1 -DNNUE_KING_BUCKETS=4 \
    -DEVALFILE="\"$(pwd)/tools/nnue_project/checkpoints/exp_datagen_d1/quantised.bin\"" \
    -Isrc tools/test_incremental.c $(ls src/*.c | grep -v src/main.c) -lm -o /tmp/test_inc_d1
/tmp/test_inc_d1 tools/nnue_project/checkpoints/exp_datagen_d1/quantised.bin

# 4. Sanity bench
python3 tools/sprt/bench.py tools/sprt/bin/GOOB-base 12
python3 tools/sprt/bench.py tools/sprt/bin/GOOB-d1 12
```

---

## Step 7: Run SPRT Matches (STC & LTC)

```bash
# STC (6+0.06s)
tools/sprt/run_sprt.sh \
    tools/sprt/bin/GOOB-d1 \
    tools/sprt/bin/GOOB-base \
    nnue-d1-stc 6+0.06 10

# LTC (12+0.1s) after STC passes
tools/sprt/run_sprt.sh \
    tools/sprt/bin/GOOB-d1 \
    tools/sprt/bin/GOOB-base \
    nnue-d1-ltc 12+0.1 10
```

---

## Step 8: Promote Weights If Accepted

Once Net-D1 (or D2 / D3) passes SPRT:

```bash
cp src/weights/quantised.bin src/weights/quantised_phase4_backup.bin
cp tools/nnue_project/checkpoints/exp_datagen_d1/quantised.bin src/weights/quantised.bin
make -C src clean
make -C src native -j8
```
