"""
Repair a record cache written by the ORIGINAL prepare_data.py, without
downloading the dataset again.

What was wrong with it
  The Hugging Face dataset has one row per (position, evaluation, PV line).
  The original script kept every row, so most positions appear 2-5 times in a
  row: once with the best move's score (PV1) and again with the scores of
  worse moves (PV2..PV5), which are labels for the wrong position value.
  Rows were also routed to train/val one at a time, so most val positions
  have siblings in train (the val loss was measuring memorisation).

What this script does
  1. Collapses each run of adjacent records with the same board and side to
     move into one record, keeping the score that is best for the side to
     move. MultiPV lines are sorted best-first, so within one evaluation that
     is exactly PV1; across several evaluations of the same position it is
     the best of their PV1 scores.
  2. Deduplicates val, and gives each val position the best score found among
     its val and train siblings.
  3. Removes every val position from train, so validation is held out again.
  4. Optionally (--shuffle) writes train in a random order on disk, for
     training with train.py --shuffle-batches when the file is larger than RAM.

What it cannot fix
  Rows the original script filtered out are gone. If a position's PV1 was a
  forced mate (dropped without --keep-mate) but a worse line had a normal
  score, only that worse line survived, and its label stays wrong. The same
  happens when PV1 was past --max-abs-cp but other lines were not; that case
  is mild, since those labels are near 0 or 1 after the sigmoid anyway.
  Regenerating with the fixed prepare_data.py is the only complete fix.

Usage (inputs are never modified):
    python clean_cache.py --train ../data/train1.bin --val ../data/val1.bin \\
        --out-dir ../data/clean [--shuffle]
Writes <out-dir>/train.bin and <out-dir>/val.bin. It streams the train file
in chunks: memory use is about 1 GB plus the val set, and it needs disk space
for the cleaned train file (plus one temporary bucket with --shuffle).
"""

import argparse
import os
import sys
import time

import numpy as np

RECORD_SIZE = 68
_U64 = np.uint64


def _mix(z):
    """splitmix64 finalizer, vectorised."""
    z = z + _U64(0x9E3779B97F4A7C15)
    z ^= z >> _U64(30)
    z *= _U64(0xBF58476D1CE4E5B9)
    z ^= z >> _U64(27)
    z *= _U64(0x94D049BB133111EB)
    z ^= z >> _U64(31)
    return z


def decode(recs):
    """-> (lanes (N,8) uint64 board, stm (N,), score (N,) int64, cp_stm (N,))

    score = cp_stm * 2 + mate_flag, so comparing scores compares the eval
    from the side to move's point of view, and the flag travels with it."""
    lanes = np.ascontiguousarray(recs[:, :64]).view("<u8")
    stm = recs[:, 64].astype(np.int64)
    cp_white = np.ascontiguousarray(recs[:, 65:67]).view("<i2").reshape(-1).astype(np.int64)
    cp_stm = np.where(stm == 0, cp_white, -cp_white)
    score = cp_stm * 2 + (recs[:, 67] & 1)
    return lanes, stm, score, cp_stm


def position_hash(lanes, stm):
    h = stm.astype(np.uint64)
    for k in range(8):
        h = _mix(h ^ lanes[:, k])
    return h


def set_score(recs, score):
    """Write score (cp_stm*2 + flag) back into the records' eval and flag bytes."""
    cp_stm = score >> 1
    flag = (score & 1).astype(np.uint8)
    cp_white = np.where(recs[:, 64] == 0, cp_stm, -cp_stm).astype("<i2")
    recs[:, 65:67] = cp_white.view(np.uint8).reshape(-1, 2)
    recs[:, 67] = (recs[:, 67] & 0xFE) | flag


def collapse_runs(recs):
    """Collapse runs of adjacent identical positions (board + stm).

    Returns (kept_records, kept_hash, kept_score, n_runs, abs_err_sum, n_err_50)
    where the error stats measure how far each input row's label was from the
    label kept for its position."""
    lanes, stm, score, cp_stm = decode(recs)
    n = len(recs)
    new_run = np.ones(n, dtype=bool)
    if n > 1:
        new_run[1:] = np.any(lanes[1:] != lanes[:-1], axis=1) | (stm[1:] != stm[:-1])
    starts = np.flatnonzero(new_run)
    run_id = np.cumsum(new_run) - 1
    # Best score first inside each run; run_id is already sorted, so after this
    # sort run r still begins at starts[r].
    order = np.lexsort((-score, run_id))
    best = order[starts]
    err = np.abs(cp_stm - cp_stm[best][run_id])
    h = position_hash(lanes[best], stm[best])
    return recs[best], h, score[best], len(starts), int(err.sum()), int((err > 50).sum())


def load_val(path):
    recs = np.fromfile(path, dtype=np.uint8)
    if recs.size % RECORD_SIZE:
        sys.exit(f"{path}: size is not a multiple of {RECORD_SIZE} bytes")
    recs = recs.reshape(-1, RECORD_SIZE)
    lanes, stm, score, _ = decode(recs)
    h = position_hash(lanes, stm)
    order = np.lexsort((-score, h))
    hs = h[order]
    first = np.ones(len(hs), dtype=bool)
    first[1:] = hs[1:] != hs[:-1]
    keep = order[first]
    return len(recs), recs[keep].copy(), h[keep], score[keep].copy()  # sorted by hash


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--train", required=True)
    ap.add_argument("--val", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--shuffle", action="store_true",
                    help="write train in random order (for a file larger than RAM)")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--chunk", type=int, default=2_000_000, help="records per chunk")
    ap.add_argument("--bucket-mb", type=int, default=512, help="--shuffle bucket size")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    out_train = os.path.join(args.out_dir, "train.bin")
    out_val = os.path.join(args.out_dir, "val.bin")
    for src in (args.train, args.val):
        for dst in (out_train, out_val):
            if os.path.exists(dst) and os.path.samefile(src, dst):
                sys.exit(f"refusing to overwrite the input {src}")

    t0 = time.time()
    n_val_in, val_recs, val_h, val_score = load_val(args.val)
    val_score_orig = val_score.copy()
    print(f"val:   {n_val_in:,} records -> {len(val_recs):,} positions")

    size = os.path.getsize(args.train)
    if size % RECORD_SIZE:
        sys.exit(f"{args.train}: size is not a multiple of {RECORD_SIZE} bytes")
    n_train = size // RECORD_SIZE
    src = np.memmap(args.train, dtype=np.uint8, mode="r", shape=(n_train, RECORD_SIZE))

    rng = np.random.default_rng(args.seed)
    if args.shuffle:
        n_buckets = max(1, -(-size // (args.bucket_mb << 20)))
        bucket_paths = [f"{out_train}.bucket{i:03d}" for i in range(n_buckets)]
        buckets = [open(p, "wb") for p in bucket_paths]
    else:
        out = open(out_train, "wb")

    stats = dict(runs=0, kept=0, leaked=0, err=0, err50=0)

    def emit(block):
        kept, h, score, runs, err, err50 = collapse_runs(block)
        stats["runs"] += runs
        stats["err"] += err
        stats["err50"] += err50
        # Positions that are also in val: improve the val label, drop from train.
        pos = np.minimum(np.searchsorted(val_h, h), len(val_h) - 1)
        leak = val_h[pos] == h if len(val_h) else np.zeros(len(h), bool)
        np.maximum.at(val_score, pos[leak], score[leak])
        kept = kept[~leak]
        stats["leaked"] += int(leak.sum())
        stats["kept"] += len(kept)
        if args.shuffle:
            b = rng.integers(0, n_buckets, len(kept))
            order = np.argsort(b, kind="stable")
            bounds = np.searchsorted(b[order], np.arange(n_buckets + 1))
            for i in range(n_buckets):
                if bounds[i + 1] > bounds[i]:
                    buckets[i].write(kept[order[bounds[i]:bounds[i + 1]]].tobytes())
        else:
            out.write(kept.tobytes())

    carry = np.empty((0, RECORD_SIZE), dtype=np.uint8)
    for a in range(0, n_train, args.chunk):
        b = min(n_train, a + args.chunk)
        block = np.concatenate([carry, np.asarray(src[a:b])])
        if b < n_train:
            # Hold back the last run: it may continue in the next chunk.
            last = block[-1, :65]
            cut = len(block) - 1
            while cut > 0 and np.array_equal(block[cut - 1, :65], last):
                cut -= 1
            carry = block[cut:]
            block = block[:cut]
        else:
            carry = carry[:0]
        if len(block):
            emit(block)
        done = b / n_train
        el = time.time() - t0
        print(f"\rtrain: {b:,}/{n_train:,} records ({100 * done:.1f}%)  "
              f"{el / 60:.1f} min elapsed, ~{el / done * (1 - done) / 60:.1f} min left",
              end="", flush=True)
    print()

    if args.shuffle:
        for f in buckets:
            f.close()
        with open(out_train, "wb") as out:
            for i, p in enumerate(bucket_paths):
                recs = np.fromfile(p, dtype=np.uint8).reshape(-1, RECORD_SIZE)
                out.write(recs[rng.permutation(len(recs))].tobytes())
                del recs
                os.remove(p)
                print(f"\rshuffle: bucket {i + 1}/{len(bucket_paths)}", end="", flush=True)
        print()
    else:
        out.close()

    changed = val_score != val_score_orig
    set_score(val_recs, val_score)
    val_recs.tofile(out_val)

    n_rows = n_train
    print(f"train: {n_rows:,} records -> {stats['runs']:,} positions "
          f"({n_rows / max(1, stats['runs']):.2f} records per position)")
    print(f"       original labels were off by {stats['err'] / max(1, n_rows):.1f} cp on average; "
          f"{100 * stats['err50'] / max(1, n_rows):.1f}% of records by more than 50 cp")
    print(f"       {stats['leaked']:,} positions also in val removed from train")
    print(f"val:   {int(changed.sum()):,} of {len(val_recs):,} labels improved using train siblings")
    print(f"wrote {stats['kept']:,} train positions to {out_train}"
          f"{' (shuffled)' if args.shuffle else ''}")
    print(f"wrote {len(val_recs):,} val positions to {out_val}")
    print(f"done in {(time.time() - t0) / 60:.1f} min")


if __name__ == "__main__":
    main()