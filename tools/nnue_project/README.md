# NNUE training pipeline for GOOB

PyTorch pipeline that trains the network GOOB evaluates with, from Lichess
position evaluations or GOOB self-play, and exports it in the engine's
quantized `quantised.bin` format. The engine side is `src/nnue_loader.h`;
`tools/README.md` has the end-to-end workflows.

## Network

Schoenemann-0.5.0 / bullet style, `(768xK -> 1024)x2 -> 1x8` (`K = 4` factorized king buckets in production, or `1`):

- 768 base inputs per perspective: `colour * 384 + piece_type * 64 + (square ^ flip)`,
  with colours swapped and squares flipped vertically (`sq ^ 56`) for Black's
  perspective. When trained with `--mirror`, `flip = 7` if that perspective's
  own king is on files `e..h` (`0` on `a..d`), folding king files into `a..d`.
- When trained with `--king-buckets 4`, each perspective's feature index adds
  `bkt * 768` where `bkt = KING_BUCKET[rel_ksq]` (`0`=castled corner `a1,b1,g1,h1`;
  `1`=back-rank center `c1..f1`; `2`=2nd rank `a2..h2`; `3`=ranks `3..8`).
  Training uses factorized weights (`ft_shared[768, 1024] + ft_buckets[4, 768, 1024]`)
  that fold into `(4 * 768, 1024)` at export time.
- Feature transformer shared by both perspectives, SCReLU activation (`clamp(x, 0, 1)^2`).
- The two activations, side to move first, feed one of 8 output buckets
  chosen by piece count: `clamp((pieces - 2) / 4, 0, 7)`.
- Training target: `sigmoid(cp_stm / 400)`, optionally blended with the game
  result (`--wdl-lambda`). The engine multiplies the output by 400.
- Exported `quantised.bin` files end with a 48-byte `GOOBNNUE` v2 architecture
  trailer (`<8sIIIII20s`: magic `"GOOBNNUE"`, version `2`, flags `bit 0 = mirror`,
  `king_buckets`, `hidden_size`, `output_buckets`, 20 zero bytes) verified by
  `src/nnue_loader.h` and `check_net.py`.

## Layout

```
scripts/
  model.py              the network above + KING_BUCKET + factorized king buckets
  fen_utils.py          FEN <-> 68-byte record (shared by data prep and loading)
  nnue_dataset.py       memmapped records -> feature indices, buckets, targets (supports --mirror / --king-buckets / --flip-augment)
  prepare_data.py       Hugging Face Lichess/chess-position-evaluations -> data/train1.bin, val1.bin
  prepare_data_jsonl.py local lichess_db_eval.jsonl(.zst) -> data/train.bin, val.bin (quiet positions only)
  append_data.py        appends new, de-duplicated Hugging Face positions to an existing set
  clear_cache.py        repairs caches written by the original prepare_data.py (see its docstring)
  train.py              training loop (Adam/AdamW, cosine LR, weight clipping, --mirror, --king-buckets, --init-from-unbucketed, exact mid-epoch resume)
  export_weights.py     checkpoint -> quantised.bin (folds ft_shared + ft_buckets and writes GOOBNNUE v2 trailer)
  check_net.py          checks an exported net and prints bit-exact reference & mirror evals
  test_net.py           evaluates FENs with a saved checkpoint
  scale_test.py         quantization headroom of a checkpoint
data_selfplay/          self-play records from tools/selfplay_nnue.py (tracked)
data/, data_json/, checkpoints/   created by the scripts (not tracked)
```

## Record format

68 bytes per position (`fen_utils.RECORD_STRUCT = "<64sBhB"`): 64 square bytes
(0 empty, 1..6 white P..K, 7..12 black p..k, a1 = 0), side to move, int16
centipawns from White's point of view (clipped to +/-3000), and a flags byte
(bit 0: mate score, bits 1-2: game result 1 = White win, 2 = draw, 3 = Black win).

## Steps

Run from `scripts/`. These commands write datasets, checkpoints and network
files: check the output paths before running them so nothing you want to
keep is overwritten.

```bash
# 1. data (either source)
python prepare_data_jsonl.py --input ../data_json/lichess_db_eval.jsonl.zst --out-dir ../data
# Or append new unique positions from an updated dump without duplicates:
python prepare_data_jsonl.py --input ../data_json/lichess_db_eval.jsonl.zst --out-dir ../data --append --dedup --n-val 0
python prepare_data.py --n-train 8000000 --n-val 50000 --out-dir ../data

# 2. train (add --init <checkpoint> to fine-tune, or --init-from-unbucketed <1bkt_ckpt> to warm-start 4 buckets)
python train.py --train ../data/train.bin --val ../data/val.bin --epochs 20 --batch-size 8192 --mirror --king-buckets 4 --optimizer adamw --bucket-decay 1e-4

# 3. export and check
python export_weights.py --checkpoint ../checkpoints/nnue.pt --out ../checkpoints/quantised.bin
python check_net.py --net ../checkpoints/quantised.bin --checkpoint ../checkpoints/nnue.pt --pipeline
```

Keep `--clip 1.98` (the default): the engine's fast SCReLU kernel needs every
quantized output weight within +/-128, and the int16 accumulator must not
overflow. `export_weights.py` and `check_net.py` warn when a net exceeds
either limit; the engine then falls back to its slower exact kernel.

To use a new net, either point the `EvalFile` UCI option at it or rebuild the
engine with it as `src/weights/quantised.bin` (embedded at build time, with
matching `NNUE_MIRROR=0/1` and `NNUE_KING_BUCKETS=1/4`).
