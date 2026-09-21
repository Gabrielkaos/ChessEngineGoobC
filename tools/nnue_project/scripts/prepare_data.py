"""
Stream Lichess/chess-position-evaluations from Hugging Face and write a
compact fixed-record binary cache for fast NNUE training.

This needs internet access to huggingface.co, so run it on your own
machine (not inside a sandboxed tool environment):

    pip install datasets tqdm
    python prepare_data.py --n-train 8000000 --n-val 50000

It writes:
    data/train.bin
    data/val.bin

Each record is 68 bytes (see fen_utils.py for the exact layout).

Perf design
-----------
Reading rows out of the HF streaming dataset is inherently single-threaded
(it's one network-backed generator) -- that part can't be parallelized.
What *can* be parallelized is the CPU-bound work per row: FEN parsing and
struct packing. So:

  - The main process just pulls rows off the stream, filters them
    (cheap), and groups them into batches.
  - Batches are handed to a ProcessPoolExecutor. Workers do the actual
    fen->bytes parsing (the expensive part) and also decide, using a
    worker-local RNG, which records *would* go to val vs train -- this
    keeps the workers independent instead of needing a shared counter.
  - The main process keeps a bounded number of batches in flight (not
    executor.map, which submits every batch up front and would force
    the entire streaming read to finish before any work is returned --
    killing the overlap between network I/O and CPU work). This way
    downloading batch N+1 happens while batch N is being parsed on
    another core.
  - Results are appended into growable bytearrays and flushed to disk
    in large chunks instead of one write() per record.
"""

import argparse
import os
import random
import time
from collections import deque
from concurrent.futures import ProcessPoolExecutor

from fen_utils import pack_record, RECORD_SIZE

try:
    from tqdm import tqdm
    HAVE_TQDM = True
except ImportError:
    HAVE_TQDM = False

VAL_FRACTION = 0.006  # matches the original sampling rate for the val split
FLUSH_BYTES = 4 * 1024 * 1024  # flush each output file every ~4MB


def iter_filtered_rows(min_depth: int, max_abs_cp: int, keep_mate: bool):
    from datasets import load_dataset

    ds = load_dataset(
        "Lichess/chess-position-evaluations", split="train", streaming=True
    )

    for row in ds:
        if row["depth"] is not None and row["depth"] < min_depth:
            continue
        cp = row["cp"]
        mate = row["mate"]
        if mate is not None:
            if not keep_mate:
                continue
        else:
            if cp is None:
                continue
            if abs(cp) > max_abs_cp:
                # Very lopsided positions add little signal for training a
                # small eval net and can dominate the loss; skip them.
                continue
        yield row["fen"], cp, mate


def batched(iterable, n):
    batch = []
    for item in iterable:
        batch.append(item)
        if len(batch) >= n:
            yield batch
            batch = []
    if batch:
        yield batch


def process_batch(batch, seed):
    """Worker function: pack every row in the batch and tag each record
    with a worker-local random draw the main process uses to route it to
    train or val. Runs in a separate process."""
    rng = random.Random(seed)
    return [(pack_record(fen, cp, mate), rng.random()) for fen, cp, mate in batch]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default="../data")
    ap.add_argument("--n-train", type=int, default=8_000_000)
    ap.add_argument("--n-val", type=int, default=50_000)
    ap.add_argument("--min-depth", type=int, default=20)
    ap.add_argument("--max-abs-cp", type=int, default=3000)
    ap.add_argument("--keep-mate", action="store_true",
                     help="Include forced-mate rows (stored as +/-3000cp).")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--workers", type=int, default=os.cpu_count(),
                     help="Number of worker processes for FEN parsing/packing.")
    ap.add_argument("--batch-size", type=int, default=2000,
                     help="Rows per unit of work handed to a worker process.")
    ap.add_argument("--max-in-flight", type=int, default=None,
                     help="Max batches queued/running at once "
                          "(default: 4x --workers).")
    ap.add_argument("--progress-every", type=int, default=50_000,
                     help="If tqdm isn't installed, print a status line "
                          "every N rows scanned.")
    args = ap.parse_args()

    if args.max_in_flight is None:
        args.max_in_flight = max(4, args.workers * 4)

    os.makedirs(args.out_dir, exist_ok=True)
    train_path = f"{args.out_dir}/train1.bin"
    val_path = f"{args.out_dir}/val1.bin"

    n_train_written = 0
    n_val_written = 0
    n_scanned = 0
    start = time.time()

    pbar = tqdm(total=args.n_train, unit="rec", desc="train") if HAVE_TQDM else None

    row_iter = iter_filtered_rows(args.min_depth, args.max_abs_cp, args.keep_mate)
    batch_iter = batched(row_iter, args.batch_size)

    with ProcessPoolExecutor(max_workers=args.workers) as ex, \
         open(train_path, "wb") as f_train, \
         open(val_path, "wb") as f_val:

        futures = deque()
        next_batch_id = 0
        stream_exhausted = False

        def submit_next():
            nonlocal next_batch_id, stream_exhausted
            if stream_exhausted:
                return
            try:
                batch = next(batch_iter)
            except StopIteration:
                stream_exhausted = True
                return
            futures.append(
                ex.submit(process_batch, batch, args.seed + next_batch_id)
            )
            next_batch_id += 1

        # prime the pipeline
        for _ in range(args.max_in_flight):
            submit_next()

        train_buf = bytearray()
        val_buf = bytearray()
        done = False

        while futures and not done:
            fut = futures.popleft()
            for record, r in fut.result():
                n_scanned += 1
                if n_val_written < args.n_val and r < VAL_FRACTION:
                    val_buf += record
                    n_val_written += 1
                elif n_train_written < args.n_train:
                    train_buf += record
                    n_train_written += 1
                    if pbar is not None:
                        pbar.update(1)

                if n_train_written >= args.n_train and n_val_written >= args.n_val:
                    done = True
                    break

            if len(train_buf) >= FLUSH_BYTES:
                f_train.write(train_buf)
                train_buf.clear()
            if len(val_buf) >= FLUSH_BYTES:
                f_val.write(val_buf)
                val_buf.clear()

            if pbar is not None:
                if n_train_written % 1000 == 0 or done:
                    pbar.set_postfix(
                        val=f"{n_val_written}/{args.n_val}",
                        scanned=n_scanned,
                        refresh=False,
                    )
            elif n_scanned % args.progress_every == 0:
                elapsed = time.time() - start
                rate = n_train_written / elapsed if elapsed > 0 else 0
                remaining = args.n_train - n_train_written
                eta_s = remaining / rate if rate > 0 else float("inf")
                print(
                    f"scanned={n_scanned:,} "
                    f"train={n_train_written:,}/{args.n_train:,} "
                    f"val={n_val_written:,}/{args.n_val:,} "
                    f"rate={rate:,.0f} rec/s "
                    f"eta={eta_s/60:,.1f} min",
                    flush=True,
                )

            if not done:
                submit_next()

        # final flush
        if train_buf:
            f_train.write(train_buf)
        if val_buf:
            f_val.write(val_buf)

        # if we stopped early, drop any still-pending/running work
        if futures:
            ex.shutdown(wait=False, cancel_futures=True)

    if pbar is not None:
        pbar.close()

    elapsed = time.time() - start
    print(f"Wrote {n_train_written} train records to {train_path}")
    print(f"Wrote {n_val_written} val records to {val_path}")
    print(f"Record size: {RECORD_SIZE} bytes")
    print(f"Total time: {elapsed/60:.1f} min "
          f"({n_train_written/elapsed:,.0f} train rec/s)")


if __name__ == "__main__":
    main()