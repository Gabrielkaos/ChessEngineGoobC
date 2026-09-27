"""
Prepare binary NNUE training dataset (train.bin / val.bin) directly from a local
Lichess evaluations JSONL file (e.g., lichess_db_eval.jsonl.zst).

This replaces the slow Hugging Face network stream with blazing-fast local
decompression and multi-process parsing.

Features:
  - Supports compressed archives (.zst via zstd CLI or zstandard, .gz) or raw .jsonl.
  - Position evaluation:
      * Selects the evaluation with the highest depth (tie-broken by knodes).
      * Extracts the first principal variation (PV1) which is Stockfish's top move.
      * Evaluations are verified White-relative, matching GoobC's NNUE format.
  - Multi-process pipeline:
      * Main process streams lines from zstd and distributes batches to worker pool.
      * Workers parse JSON, extract highest-depth PV1, filter, and pack to 68-byte records.
      * Main process partitions into train/val sets and flushes to disk in large buffers.
  - Safe writing & error handling:
      * Graceful SIGINT/Ctrl+C handling with truncation safeguard to 68-byte alignment.
      * Resume support via --skip-lines and --append.

Usage examples:
    # Generate 8M train records and 50k val records (default):
    python prepare_data_jsonl.py

    # Custom paths and record counts:
    python prepare_data_jsonl.py --input data_json/lichess_db_eval.jsonl.zst \
        --out-dir ../data --train-name train1.bin --val-name val1.bin \
        --n-train 10000000 --n-val 100000 --min-depth 22

    # Process all positions in the file (unlimited train):
    python prepare_data_jsonl.py --n-train 0 --n-val 100000
"""

import argparse
import gzip
import json
import os
import random
import shutil
import struct
import subprocess
import sys
import time
import traceback
from collections import deque
from concurrent.futures import ProcessPoolExecutor

from fen_utils import pack_record, RECORD_SIZE

try:
    from tqdm import tqdm
    HAVE_TQDM = True
except ImportError:
    HAVE_TQDM = False

VAL_FRACTION_DEFAULT = 0.006  # fallback ~0.6% validation fraction
FLUSH_BYTES = 8 * 1024 * 1024  # Flush output files in 8 MB chunks


def resolve_path(path: str) -> str:
    """Resolve path relative to CWD or project root."""
    if not path or path == "-":
        return path
    if os.path.exists(path):
        return os.path.abspath(path)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    proj_root = os.path.abspath(os.path.join(script_dir, ".."))
    candidate = os.path.join(proj_root, path)
    if os.path.exists(candidate):
        return candidate
    return os.path.abspath(path)


def open_input_stream(input_path: str):
    """Open a line stream from .zst, .gz, raw .jsonl, or stdin.
    
    Yields (proc, line_iterator) where proc is an optional subprocess to close.
    """
    if input_path == "-":
        return None, sys.stdin

    if not os.path.exists(input_path):
        raise FileNotFoundError(f"Input file not found: {input_path}")

    lower = input_path.lower()
    if lower.endswith(".zst"):
        # Prefer fast multi-threaded system zstd if available
        zstd_bin = shutil.which("zstd") or shutil.which("zstdcat")
        if zstd_bin:
            cmd = [zstd_bin, "-dc", "-T0", input_path]
            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                bufsize=4 * 1024 * 1024,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
            return proc, proc.stdout
        else:
            try:
                import zstandard as zstd
                dctx = zstd.ZstdDecompressor()
                fh = open(input_path, "rb")
                reader = dctx.stream_reader(fh)
                import io
                text_stream = io.TextIOWrapper(reader, encoding="utf-8", errors="replace")
                return fh, text_stream
            except ImportError:
                raise RuntimeError(
                    "Input is a .zst file but neither 'zstd' command nor python 'zstandard' is available. "
                    "Install zstd via: sudo apt install zstd"
                )

    elif lower.endswith(".gz"):
        fh = gzip.open(input_path, "rt", encoding="utf-8", errors="replace")
        return fh, fh
    else:
        fh = open(input_path, "r", encoding="utf-8", errors="replace")
        return fh, fh


def process_batch_lines(lines: list, min_depth: int, max_abs_cp: int, keep_mate: bool) -> tuple:
    """Worker function: parses JSON lines, selects highest depth PV1, filters, and packs records.
    
    Returns:
        (packed_blob: bytes, valid_count: int, parsed_count: int)
    """
    records = []
    parsed_count = len(lines)

    for line in lines:
        if not line:
            continue
        try:
            data = json.loads(line)
        except Exception:
            continue

        evals = data.get("evals")
        if not evals:
            continue

        # Select evaluation with the highest depth, breaking ties by knodes
        best_eval = max(
            (ev for ev in evals if ev.get("pvs")),
            key=lambda ev: (ev.get("depth") or -1, ev.get("knodes") or 0),
            default=None,
        )
        if best_eval is None:
            continue

        pvs = best_eval.get("pvs")
        if not pvs:
            continue

        depth = best_eval.get("depth", 0)
        if depth is not None and depth < min_depth:
            continue

        pv1 = pvs[0]
        mate = pv1.get("mate")
        cp = pv1.get("cp")

        if mate is not None:
            if not keep_mate:
                continue
        else:
            if cp is None:
                continue
            if abs(cp) > max_abs_cp:
                # Extreme centipawn values add little signal and skew training loss
                continue

        fen = data.get("fen")
        if not fen:
            continue

        try:
            rec = pack_record(fen, cp, mate)
            records.append(rec)
        except Exception:
            continue

    blob = b"".join(records)
    return blob, len(records), parsed_count


def batched_stream(line_iter, batch_size: int, skip_lines: int = 0, max_lines: int = None):
    """Yield batches of lines from an iterator with skip and limit support."""
    batch = []
    lines_read = 0

    # Skip initial lines if requested
    if skip_lines > 0:
        for _ in range(skip_lines):
            line = line_iter.readline()
            if not line:
                return
            lines_read += 1

    while True:
        if max_lines is not None and lines_read >= (skip_lines + max_lines):
            break
        line = line_iter.readline()
        if not line:
            break
        lines_read += 1
        batch.append(line)
        if len(batch) >= batch_size:
            yield batch
            batch = []

    if batch:
        yield batch


def main():
    parser = argparse.ArgumentParser(
        description="Prepare 68-byte NNUE binary dataset from Lichess JSONL evaluations."
    )
    parser.add_argument(
        "--input", "-i",
        default="data_json/lichess_db_eval.jsonl.zst",
        help="Path to evaluations JSONL file (.jsonl.zst, .jsonl, .gz, or '-' for stdin).",
    )
    parser.add_argument(
        "--out-dir", "-o",
        default="data",
        help="Output directory for binary files (default: data).",
    )
    parser.add_argument(
        "--train-name",
        default="train.bin",
        help="Filename for train split (default: train.bin).",
    )
    parser.add_argument(
        "--val-name",
        default="val.bin",
        help="Filename for val split (default: val.bin).",
    )
    parser.add_argument(
        "--n-train",
        type=int,
        default=None,
        help="Optional max train positions to produce (default: stream until end of file).",
    )
    parser.add_argument(
        "--n-val",
        type=int,
        default=None,
        help="Optional max val positions to produce (default: stream until end of file at --val-fraction).",
    )
    parser.add_argument(
        "--val-fraction",
        type=float,
        default=0.05,
        help="Fraction of dataset to allocate to validation (default: 0.05 = 5%).",
    )
    parser.add_argument(
        "--min-depth",
        type=int,
        default=20,
        help="Minimum engine search depth to accept (default: 20).",
    )
    parser.add_argument(
        "--max-abs-cp",
        type=int,
        default=3000,
        help="Maximum absolute centipawn evaluation before clipping/skipping (default: 3000).",
    )
    parser.add_argument(
        "--keep-mate",
        action="store_true",
        help="Include forced mate positions (stored as saturated +/-3000cp). Default: False.",
    )
    parser.add_argument(
        "--append",
        action="store_true",
        help="Append to existing train/val files instead of overwriting.",
    )
    parser.add_argument(
        "--skip-lines",
        type=int,
        default=0,
        help="Number of input JSON lines to skip before processing.",
    )
    parser.add_argument(
        "--max-lines",
        type=int,
        default=None,
        help="Maximum input lines to scan.",
    )
    parser.add_argument(
        "--workers", "-w",
        type=int,
        default=os.cpu_count() or 4,
        help="Number of worker processes (default: number of CPU cores).",
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        default=5000,
        help="Number of lines per worker batch (default: 5000).",
    )
    parser.add_argument(
        "--max-in-flight",
        type=int,
        default=None,
        help="Maximum batches queued/running concurrently (default: 4x workers).",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="Random seed for train/val split (default: 42).",
    )
    parser.add_argument(
        "--progress-every",
        type=int,
        default=50_000,
        help="Status print interval when tqdm is not available (default: 50,000).",
    )

    args = parser.parse_args()

    input_file = resolve_path(args.input)
    out_dir = resolve_path(args.out_dir)
    os.makedirs(out_dir, exist_ok=True)

    train_path = os.path.join(out_dir, args.train_name)
    val_path = os.path.join(out_dir, args.val_name)

    if args.max_in_flight is None:
        args.max_in_flight = max(4, args.workers * 4)

    rng = random.Random(args.seed)

    # Compute validation fraction
    val_fraction = args.val_fraction

    open_mode = "ab" if args.append else "wb"
    existing_train = (os.path.getsize(train_path) // RECORD_SIZE) if (args.append and os.path.exists(train_path)) else 0
    existing_val = (os.path.getsize(val_path) // RECORD_SIZE) if (args.append and os.path.exists(val_path)) else 0

    train_target_str = f"{args.n_train:,} records" if (args.n_train and args.n_train > 0) else f"End of file ({(1.0 - val_fraction):.0%})"
    val_target_str = f"{args.n_val:,} records" if (args.n_val and args.n_val > 0) else f"End of file ({val_fraction:.0%})"

    print("=" * 70)
    print("GoobC NNUE - Lichess JSONL Dataset Preparation")
    print("=" * 70)
    print(f"Input file      : {input_file}")
    print(f"Output train    : {train_path} {'(append)' if args.append else '(overwrite)'}")
    print(f"Output val      : {val_path} {'(append)' if args.append else '(overwrite)'}")
    print(f"Target train    : {train_target_str}")
    print(f"Target val      : {val_target_str}")
    print(f"Validation split: {val_fraction:.1%} val / {(1.0 - val_fraction):.1%} train")
    print(f"Filters         : min_depth={args.min_depth}, max_abs_cp={args.max_abs_cp}, keep_mate={args.keep_mate}")
    print(f"Parallelism     : {args.workers} workers, batch_size={args.batch_size:,}, in_flight={args.max_in_flight}")
    if args.append and (existing_train > 0 or existing_val > 0):
        print(f"Existing records: {existing_train:,} train, {existing_val:,} val")
    print("=" * 70, flush=True)

    stream_proc, line_stream = open_input_stream(input_file)
    batch_gen = batched_stream(
        line_stream,
        batch_size=args.batch_size,
        skip_lines=args.skip_lines,
        max_lines=args.max_lines,
    )

    n_train_written = 0
    n_val_written = 0
    n_lines_scanned = 0
    n_records_valid = 0
    start_time = time.time()

    pbar = tqdm(total=args.n_train if (args.n_train and args.n_train > 0) else None, unit="rec", desc="train") if HAVE_TQDM else None

    train_buf = bytearray()
    val_buf = bytearray()
    done = False

    try:
        with ProcessPoolExecutor(max_workers=args.workers) as ex, \
             open(train_path, open_mode) as f_train, \
             open(val_path, open_mode) as f_val:

            futures = deque()
            stream_exhausted = False

            def submit_next():
                nonlocal stream_exhausted
                if stream_exhausted:
                    return
                try:
                    batch = next(batch_gen)
                    fut = ex.submit(
                        process_batch_lines,
                        batch,
                        args.min_depth,
                        args.max_abs_cp,
                        args.keep_mate,
                    )
                    futures.append(fut)
                except StopIteration:
                    stream_exhausted = True

            # Prime executor pipeline
            for _ in range(args.max_in_flight):
                submit_next()

            while futures and not done:
                fut = futures.popleft()
                blob, valid_n, parsed_n = fut.result()
                n_lines_scanned += parsed_n
                n_records_valid += valid_n

                # Route records from blob (each record is exactly RECORD_SIZE = 68 bytes)
                for i in range(0, len(blob), RECORD_SIZE):
                    rec = blob[i : i + RECORD_SIZE]
                    r = rng.random()

                    wants_val = (r < val_fraction)
                    can_write_val = (args.n_val is None or args.n_val <= 0 or n_val_written < args.n_val)
                    can_write_train = (args.n_train is None or args.n_train <= 0 or n_train_written < args.n_train)

                    if wants_val and can_write_val:
                        val_buf += rec
                        n_val_written += 1
                    elif can_write_train:
                        train_buf += rec
                        n_train_written += 1
                        if pbar is not None:
                            pbar.update(1)
                    elif can_write_val:
                        val_buf += rec
                        n_val_written += 1

                    if (args.n_train is not None and args.n_train > 0 and n_train_written >= args.n_train) and \
                       (args.n_val is not None and args.n_val > 0 and n_val_written >= args.n_val):
                        done = True
                        break

                # Flush to disk when buffer reaches FLUSH_BYTES
                if len(train_buf) >= FLUSH_BYTES:
                    f_train.write(train_buf)
                    train_buf.clear()
                if len(val_buf) >= FLUSH_BYTES:
                    f_val.write(val_buf)
                    val_buf.clear()

                # Progress reporting
                if pbar is not None:
                    if n_train_written % 1000 == 0 or done:
                        tot = n_train_written + n_val_written
                        val_pct = (n_val_written / tot * 100) if tot > 0 else 0
                        pbar.set_postfix(
                            val=f"{n_val_written:,} ({val_pct:.1f}%)",
                            scanned=f"{n_lines_scanned:,}",
                            refresh=False,
                        )
                elif n_lines_scanned % args.progress_every < args.batch_size:
                    elapsed = time.time() - start_time
                    rate = n_train_written / elapsed if elapsed > 0 else 0
                    tot = n_train_written + n_val_written
                    val_pct = (n_val_written / tot * 100) if tot > 0 else 0
                    if args.n_train and args.n_train > 0:
                        rem = max(0, args.n_train - n_train_written)
                        eta_s = rem / rate if rate > 0 else 0
                        eta_str = f"eta={eta_s/60:.1f}m"
                    else:
                        eta_str = f"{val_pct:.1f}% val"
                    print(
                        f"scanned={n_lines_scanned:,} | "
                        f"valid={n_records_valid:,} | "
                        f"train={n_train_written:,} | "
                        f"val={n_val_written:,} ({val_pct:.1f}%) | "
                        f"rate={rate:,.0f} rec/s | {eta_str}",
                        flush=True,
                    )

                if not done:
                    submit_next()

    except (KeyboardInterrupt, Exception) as e:
        if not isinstance(e, KeyboardInterrupt):
            traceback.print_exc()
        print(f"\n[Safeguard] Interrupted or error ({type(e).__name__}: {e}). Flushing all records safely...")
    finally:
        # Clean up subprocess if open
        if stream_proc is not None:
            try:
                stream_proc.terminate()
                stream_proc.wait(timeout=1.0)
            except Exception:
                pass

        # Final write of remaining records in buffers
        with open(train_path, "ab") as f_train, open(val_path, "ab") as f_val:
            if train_buf:
                f_train.write(train_buf)
                train_buf.clear()
            if val_buf:
                f_val.write(val_buf)
                val_buf.clear()
            f_train.flush()
            f_val.flush()

        # Enforce strict 68-byte alignment in case of abrupt exit
        for path in (train_path, val_path):
            if os.path.exists(path):
                fsize = os.path.getsize(path)
                rem = fsize % RECORD_SIZE
                if rem != 0:
                    with open(path, "a+b") as fix_f:
                        fix_f.truncate(fsize - rem)
                    print(f"[Safeguard] Truncated {rem} trailing bytes from {path} for clean record alignment.")

        if pbar is not None:
            pbar.close()

    elapsed = time.time() - start_time
    total_train = (os.path.getsize(train_path) // RECORD_SIZE) if os.path.exists(train_path) else 0
    total_val = (os.path.getsize(val_path) // RECORD_SIZE) if os.path.exists(val_path) else 0
    tot_written = n_train_written + n_val_written
    train_pct = (n_train_written / tot_written * 100) if tot_written > 0 else 0
    val_pct = (n_val_written / tot_written * 100) if tot_written > 0 else 0

    print("\n" + "=" * 70)
    print("Dataset Preparation Completed")
    print("=" * 70)
    print(f"Total lines scanned: {n_lines_scanned:,}")
    print(f"Valid positions    : {n_records_valid:,} ({(n_records_valid / max(1, n_lines_scanned)):.1%})")
    print(f"New train written  : {n_train_written:,} ({train_pct:.1f}%) -> {train_path}")
    print(f"New val written    : {n_val_written:,} ({val_pct:.1f}%) -> {val_path}")
    print(f"Total records      : {total_train:,} train, {total_val:,} val")
    print(f"Record layout      : {RECORD_SIZE} bytes per position")
    print(f"Elapsed time       : {elapsed:.2f}s ({elapsed/60:.2f} min)")
    if elapsed > 0:
        print(f"Overall throughput : {n_lines_scanned/elapsed:,.0f} lines/s ({n_train_written/elapsed:,.0f} train rec/s)")
    print("=" * 70)


if __name__ == "__main__":
    main()
