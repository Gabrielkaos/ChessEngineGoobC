# Copy-Paste Commands for Phase 4

#### Step 1: Fine-Tune the 4-Bucket Network (Warm-Started from Phase 3 `exp_mirror`)

```bash
mkdir -p tools/nnue_project/checkpoints/exp_4bkt

tools/.venv/bin/python3 tools/nnue_project/scripts/train.py \
    --train tools/nnue_project/data/train.bin \
    --val tools/nnue_project/data/val.bin \
    --init-from-unbucketed tools/nnue_project/checkpoints/exp_mirror/nnue.pt \
    --mirror \
    --king-buckets 4 \
    --optimizer adamw \
    --weight-decay 0.0 \
    --bucket-decay 1e-4 \
    --epochs 10 \
    --batch-size 8192 \
    --lr 2e-4 \
    --wdl-lambda 0.0 \
    --clip 1.98 \
    --seed 42 \
    --out-best tools/nnue_project/checkpoints/exp_4bkt/nnue.pt \
    --out-last tools/nnue_project/checkpoints/exp_4bkt/last.pt
```

#### Step 2: Export & Verify the Quantized 4-Bucket Network

```bash
tools/.venv/bin/python3 tools/nnue_project/scripts/export_weights.py \
    --checkpoint tools/nnue_project/checkpoints/exp_4bkt/nnue.pt \
    --out tools/nnue_project/checkpoints/exp_4bkt/quantised.bin

tools/.venv/bin/python3 tools/nnue_project/scripts/check_net.py \
    tools/nnue_project/checkpoints/exp_4bkt/quantised.bin \
    --checkpoint tools/nnue_project/checkpoints/exp_4bkt/nnue.pt \
    --pipeline
```

#### Step 3: Build Candidate (`K=4`) and Baseline (`K=1`) + Run Incremental/Finny Cache Tests

```bash
mkdir -p tools/sprt/bin

# 1. Build Phase 3 baseline (1 bucket, mirrored, embedded src/weights/quantised.bin)
make -C src clean
make -C src native NNUE_MIRROR=1 NNUE_KING_BUCKETS=1 -j8
cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-phase3-mirror

# 2. Build Phase 4 candidate (4 buckets, mirrored, embedded exp_4bkt/quantised.bin)
make -C src clean
make -C src native NNUE_MIRROR=1 NNUE_KING_BUCKETS=4 \
    EVALFILE=../tools/nnue_project/checkpoints/exp_4bkt/quantised.bin -j8
cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-phase4-4bkt

# 3. Run C incremental + Finny cache unit test on the 4-bucket net
gcc -O2 -march=native -pthread -DUSE_PEXT -DNNUE_MIRROR=1 -DNNUE_KING_BUCKETS=4 \
    -DEVALFILE='"tools/nnue_project/checkpoints/exp_4bkt/quantised.bin"' -Isrc \
    tools/test_incremental.c $(ls src/*.c | grep -v src/main.c) -lm -o /tmp/test_incremental_4bkt
/tmp/test_incremental_4bkt tools/nnue_project/checkpoints/exp_4bkt/quantised.bin

# 4. Run Python comprehensive bit-exact verification against the 4-bucket engine binary
tools/.venv/bin/python3 tools/test_nnue_comprehensive.py \
    --net tools/nnue_project/checkpoints/exp_4bkt/quantised.bin \
    --engine tools/sprt/bin/GOOB-phase4-4bkt
```

#### Step 4: Sanity Bench (Check NPS Overhead Before SPRT)

```bash
python3 tools/sprt/bench.py tools/sprt/bin/GOOB-phase3-mirror 12
python3 tools/sprt/bench.py tools/sprt/bin/GOOB-phase4-4bkt 12
```

#### Step 5: Run STC and LTC SPRT Matches

```bash
# STC gate (6+0.06s)
TC=6+0.06 ELO0=0 ELO1=5 GAMES=10000 \
    tools/sprt/run_sprt.sh \
    tools/sprt/bin/GOOB-phase4-4bkt \
    tools/sprt/bin/GOOB-phase3-mirror

# LTC confirmation (20+0.2s, run after STC passes)
TC=20+0.2 ELO0=0 ELO1=5 GAMES=10000 \
    tools/sprt/run_sprt.sh \
    tools/sprt/bin/GOOB-phase4-4bkt \
    tools/sprt/bin/GOOB-phase3-mirror
```

#### Step 6: If Accepted — Promote the 4-Bucket Network to Default

If Phase 4 passes SPRT, promote the weights and set `NNUE_KING_BUCKETS ?= 4` as default in `src/makefile`:

```bash
mv src/weights/quantised.bin src/weights/quantised_mirror_1bkt_old.bin
cp tools/nnue_project/checkpoints/exp_4bkt/quantised.bin src/weights/quantised.bin
sed -i 's/^NNUE_KING_BUCKETS ?= 1/NNUE_KING_BUCKETS ?= 4/' src/makefile
make -C src clean && make -C src -j8 && make -C src native -j8
```
