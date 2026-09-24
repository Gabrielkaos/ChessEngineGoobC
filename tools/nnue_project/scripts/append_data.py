"""
Append new positions to an existing NNUE training dataset (train.bin / val.bin),
scraping off MultiPV lines and guaranteeing zero duplicate positions.

Features:
  - MultiPV scraping: collapses contiguous HF rows for each FEN and picks
    PV1 from the deepest evaluation, exactly matching prepare_data.py.
  - Full deduplication:
      1. Never appends positions that already exist in train.bin or val.bin.
      2. Never appends duplicate positions within the newly streamed data.
      3. Never leaks duplicate positions between the newly added train and val.
  - Fast indexing: computes and caches a 64-bit splitmix64 position hash index
    so subsequent runs load in <1 second instead of re-reading gigabytes.
  - Safe appending: opens output files in binary append mode ("ab"), flushes
    buffers in chunks, and guarantees clean 68-byte record alignment even on
    Ctrl+C or sudden interruption.

Usage:
    # Append 5,000,000 new train records to ../data_new/clean (default)
    python append_data.py --n-train 5000000

    # Explicit directory and custom validation ratio
    python append_data.py --data-dir ../data_new/clean --n-train 5000000 --n-val 80000

    # Skip first 100M rows on HF stream if already traversed
    python append_data.py --n-train 5000000 --skip-rows 100000000
"""

import argparse
import json
import os
import random
import struct
import sys
import time
import traceback
from collections import deque
from concurrent.futures import ProcessPoolExecutor

import numpy as np

from fen_utils import pack_record, RECORD_SIZE

try:
    from tqdm import tqdm
    HAVE_TQDM = True
except ImportError:
    HAVE_TQDM = False

FLUSH_BYTES = 4 * 1024 * 1024  # Flush to disk every ~4MB
DEFAULT_VAL_FRACTION = 0.016    # ~1.6%, matching existing ~123.8M train / ~1.97M val split
INDEX_CHUNK_SIZE = 10_000_000   # Read 10M records per chunk during initial hash indexing

_U64 = np.uint64
_U64_8_STRUCT = struct.Struct("<8Q")


# ---------------------------------------------------------------------------
# SplitMix64 hashing (vectorized for bulk indexing, scalar for worker processes)
# ---------------------------------------------------------------------------

def _mix_np(z):
    """Vectorized SplitMix64 finalizer for NumPy uint64 arrays."""
    z = z + _U64(0x9E3779B97F4A7C15)
    z ^= z >> _U64(30)
    z *= _U64(0xBF58476D1CE4E5B9)
    z ^= z >> _U64(27)
    z *= _U64(0x94D049BB133111EB)
    z ^= z >> _U64(31)
    return z


def position_hash_np(lanes, stm):
    """Compute 64-bit position hash for an array of (N, 8) uint64 lanes and (N,) stm."""
    h = stm.astype(np.uint64)
    for k in range(8):
        h = _mix_np(h ^ lanes[:, k])
    return h


def mix_scalar(z: int) -> int:
    """Scalar SplitMix64 finalizer."""
    z = (z + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    z ^= (z >> 30)
    z = (z * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
    z ^= (z >> 27)
    z = (z * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
    z ^= (z >> 31)
    return z


def compute_record_hash(rec: bytes) -> int:
    """Compute 64-bit position hash directly from a 68-byte record blob.
    
    Bits 0..63 are board squares (8 x uint64 lanes), byte 64 is stm.
    Produces identical results to position_hash_np bit-for-bit.
    """
    lanes = _U64_8_STRUCT.unpack(rec[:64])
    h = rec[64]
    for x in lanes:
        h = mix_scalar(h ^ x)
    return h


# ---------------------------------------------------------------------------
# MultiPV scraping and row filtering (matches prepare_data.py)
# ---------------------------------------------------------------------------

def best_eval_per_position(rows):
    """Collapse contiguous exploded rows for a FEN to one label per position.

    In the Lichess dataset:
      - Rows for the same FEN are contiguous.
      - Each evaluation block is keyed by (depth, knodes). The first row
        of each block is PV1 (the true position evaluation). Subsequent rows
        are MultiPV lines 2, 3, ... (evaluations of worse alternative moves).
      - Across multiple evaluation depths for the same FEN, this picks PV1
        from the deepest evaluation.

    Yields (fen, depth, cp, mate) once per position.
    """
    cur_fen = None
    best = None  # (depth_for_compare, depth, cp, mate)
    prev_key = None
    for row in rows:
        fen = row["fen"]
        if fen != cur_fen:
            if best is not None:
                yield cur_fen, best[1], best[2], best[3]
            cur_fen, best, prev_key = fen, None, None
        key = (row["depth"], row["knodes"])
        if key != prev_key:  # first row of a new evaluation = its PV1
            prev_key = key
            d = row["depth"] if row["depth"] is not None else -1
            if best is None or d > best[0]:
                best = (d, row["depth"], row["cp"], row["mate"])
    if best is not None:
        yield cur_fen, best[1], best[2], best[3]


# Cumulative start row index for each of the 20 shards in Lichess/chess-position-evaluations
# Total dataset: 957,860,115 rows across 20 parquet shards.
SHARD_BOUNDARIES = [
    0,           # Shard 00: 0
    54_372_489,  # Shard 01: 54.4M
    106_569_251, # Shard 02: 106.6M
    158_046_979, # Shard 03: 158.0M
    209_015_414, # Shard 04: 209.0M
    259_723_815, # Shard 05: 259.7M
    310_051_033, # Shard 06: 310.1M
    360_072_703, # Shard 07: 360.1M
    409_897_476, # Shard 08: 409.9M
    451_149_383, # Shard 09: 451.1M
    495_716_267, # Shard 10: 495.7M
    542_145_616, # Shard 11: 542.1M
    588_868_842, # Shard 12: 588.9M
    635_766_431, # Shard 13: 635.8M
    683_085_961, # Shard 14: 683.1M
    729_852_610, # Shard 15: 729.9M
    776_402_331, # Shard 16: 776.4M
    822_170_954, # Shard 17: 822.2M
    867_999_935, # Shard 18: 868.0M
    913_010_368, # Shard 19: 913.0M
]


def iter_filtered_rows(
    source="Lichess/chess-position-evaluations",
    min_depth: int = 20,
    max_abs_cp: int = 3000,
    keep_mate: bool = False,
    skip_rows: int = 0,
    start_shard: int = None,
    exact_skip: bool = False,
):
    """Stream and filter positions from Hugging Face or an iterable source.

    Fast Shard Skipping:
      The Lichess HF dataset contains 20 parquet shards totaling ~958M rows.
      Calling ds.skip(N) naively downloads and loops through all N rows one-by-one,
      which takes hours.
      Instead, this function jumps directly to the appropriate parquet shard file
      (e.g., shard 7 starts at row ~360,072,703) in seconds with 0s overhead
      for the skipped shards.
    """
    if isinstance(source, str):
        from datasets import load_dataset

        # Map skip_rows to the corresponding shard boundary if not explicitly specified
        if start_shard is None and skip_rows > 0 and source == "Lichess/chess-position-evaluations":
            valid_shards = [i for i, b in enumerate(SHARD_BOUNDARIES) if b <= skip_rows]
            start_shard = valid_shards[-1] if valid_shards else 0

        if start_shard is not None and source == "Lichess/chess-position-evaluations":
            start_shard = max(0, min(19, start_shard))
            shard_files = [f"data/data_{i:04d}.parquet" for i in range(start_shard, 20)]
            shard_start_row = SHARD_BOUNDARIES[start_shard]
            print(f"[Stream] Instant shard jump -> starting from Shard {start_shard:02d}/19 ({shard_files[0]})")
            print(f"[Stream] Instantly skipped {shard_start_row:,} rows (0s network overhead)!")
            ds = load_dataset(source, data_files={"train": shard_files}, split="train", streaming=True)

            if exact_skip and skip_rows > shard_start_row:
                rem = skip_rows - shard_start_row
                print(f"[Stream] Performing exact skip of remaining {rem:,} rows inside shard {start_shard}...")
                ds = ds.skip(rem)
            elif skip_rows > shard_start_row:
                print(f"[Stream] Snapped to Shard {start_shard:02d} boundary ({shard_start_row:,} rows). Overlaps will be automatically deduplicated.")
        else:
            ds = load_dataset(source, split="train", streaming=True)
            if skip_rows > 0:
                print(f"[Stream] Skipping first {skip_rows:,} raw rows on stream...")
                ds = ds.skip(skip_rows)

        try:
            ds = ds.select_columns(["fen", "depth", "knodes", "cp", "mate"])
        except Exception:
            pass
        row_stream = ds
    else:
        row_stream = source

    for fen, depth, cp, mate in best_eval_per_position(row_stream):
        if depth is not None and depth < min_depth:
            continue
        if mate is not None:
            if not keep_mate:
                continue
        else:
            if cp is None:
                continue
            if abs(cp) > max_abs_cp:
                continue
        yield fen, cp, mate


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
    """Worker function: packs records and computes position hash + RNG draw."""
    rng = random.Random(seed)
    res = []
    for fen, cp, mate in batch:
        rec = pack_record(fen, cp, mate)
        h = compute_record_hash(rec)
        res.append((rec, h, rng.random()))
    return res


# ---------------------------------------------------------------------------
# Hash Indexing & Cache Management
# ---------------------------------------------------------------------------

def _read_and_hash_file(path: str, chunk_size: int = INDEX_CHUNK_SIZE) -> np.ndarray:
    """Read a .bin file in chunks and compute position hashes."""
    if not os.path.exists(path) or os.path.getsize(path) == 0:
        return np.empty(0, dtype=np.uint64)

    size = os.path.getsize(path)
    if size % RECORD_SIZE != 0:
        rem = size % RECORD_SIZE
        print(f"[Warning] {path} size ({size}) is not a multiple of {RECORD_SIZE}. Ignoring trailing {rem} bytes.")
        size -= rem

    n_recs = size // RECORD_SIZE
    hashes = []
    t0 = time.time()
    with open(path, "rb") as f:
        for a in range(0, n_recs, chunk_size):
            b = min(n_recs, a + chunk_size)
            raw = f.read((b - a) * RECORD_SIZE)
            recs = np.frombuffer(raw, dtype=np.uint8).reshape(-1, RECORD_SIZE)
            lanes = np.ascontiguousarray(recs[:, :64]).view("<u8")
            stm = recs[:, 64].astype(np.int64)
            h = position_hash_np(lanes, stm)
            hashes.append(h)
            elapsed = time.time() - t0
            print(f"\r  Indexing {os.path.basename(path)}: {b:,}/{n_recs:,} records ({b/n_recs*100:.1f}%) in {elapsed:.1f}s", end="", flush=True)
    print()
    if hashes:
        return np.concatenate(hashes)
    return np.empty(0, dtype=np.uint64)


def load_or_build_hash_index(
    train_path: str,
    val_path: str,
    cache_dir: str = None,
    use_cache: bool = True,
    reindex: bool = False,
) -> np.ndarray:
    """Load cached sorted uint64 position hashes or compute them from train and val."""
    if cache_dir is None:
        cache_dir = os.path.dirname(train_path)

    index_path = os.path.join(cache_dir, ".pos_hashes.u64.bin")
    meta_path = os.path.join(cache_dir, ".pos_hashes_meta.json")

    t_train = os.path.getsize(train_path) if os.path.exists(train_path) else 0
    t_train_m = os.path.getmtime(train_path) if os.path.exists(train_path) else 0
    t_val = os.path.getsize(val_path) if os.path.exists(val_path) else 0
    t_val_m = os.path.getmtime(val_path) if os.path.exists(val_path) else 0

    if use_cache and not reindex and os.path.exists(index_path) and os.path.exists(meta_path):
        try:
            with open(meta_path, "r") as mf:
                meta = json.load(mf)
            if (
                meta.get("train_size") == t_train
                and abs(meta.get("train_mtime", 0) - t_train_m) < 1.0
                and meta.get("val_size") == t_val
                and abs(meta.get("val_mtime", 0) - t_val_m) < 1.0
                and os.path.getsize(index_path) == meta.get("count", 0) * 8
            ):
                t0 = time.time()
                hashes = np.fromfile(index_path, dtype=np.uint64)
                print(f"[Hash Index] Loaded {len(hashes):,} cached position hashes in {time.time()-t0:.2f}s")
                return hashes
        except Exception as e:
            print(f"[Hash Index] Cache invalid or unreadable ({e}), rebuilding index...")

    print("[Hash Index] Building position hash index from existing dataset (one-time setup)...")
    t0 = time.time()
    train_h = _read_and_hash_file(train_path)
    val_h = _read_and_hash_file(val_path)

    all_h = np.concatenate([train_h, val_h]) if (len(train_h) and len(val_h)) else (train_h if len(train_h) else val_h)
    print(f"[Hash Index] Sorting & deduplicating {len(all_h):,} total hashes...")
    t_sort = time.time()
    sorted_h = np.unique(all_h)
    print(f"[Hash Index] Complete: {len(sorted_h):,} unique positions indexed (sort took {time.time()-t_sort:.1f}s, total {time.time()-t0:.1f}s)")

    if use_cache and len(sorted_h) > 0:
        save_hash_index(cache_dir, sorted_h, train_path, val_path)

    return sorted_h


def save_hash_index(cache_dir: str, sorted_hashes: np.ndarray, train_path: str, val_path: str):
    """Save sorted hashes and metadata to cache_dir."""
    index_path = os.path.join(cache_dir, ".pos_hashes.u64.bin")
    meta_path = os.path.join(cache_dir, ".pos_hashes_meta.json")
    try:
        sorted_hashes.tofile(index_path)
        meta = {
            "train_path": os.path.basename(train_path),
            "train_size": os.path.getsize(train_path) if os.path.exists(train_path) else 0,
            "train_mtime": os.path.getmtime(train_path) if os.path.exists(train_path) else 0,
            "val_path": os.path.basename(val_path),
            "val_size": os.path.getsize(val_path) if os.path.exists(val_path) else 0,
            "val_mtime": os.path.getmtime(val_path) if os.path.exists(val_path) else 0,
            "count": len(sorted_hashes),
        }
        with open(meta_path, "w") as mf:
            json.dump(meta, mf, indent=2)
        print(f"[Hash Index] Saved index cache ({len(sorted_hashes):,} hashes) to {index_path}")
    except Exception as e:
        print(f"[Hash Index] Warning: failed to save index cache ({e})")


def resolve_data_paths(data_dir=None, train_path=None, val_path=None):
    """Smart resolution of dataset file paths."""
    if train_path and val_path:
        return train_path, val_path

    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = []
    if data_dir:
        candidates.append(data_dir)
        candidates.append(os.path.join(script_dir, data_dir))

    candidates.extend([
        os.path.join(script_dir, "../data_new/clean"),
        os.path.join(script_dir, "../data_new_clean"),
        os.path.join(script_dir, "../data/clean"),
        os.path.join(script_dir, "../data"),
        "../data_new/clean",
        "../data_new_clean",
        "data_new/clean",
        "data_new_clean",
    ])

    chosen_dir = None
    for c in candidates:
        norm = os.path.normpath(c)
        if os.path.isdir(norm) and os.path.exists(os.path.join(norm, "train.bin")):
            chosen_dir = norm
            break

    if chosen_dir is None:
        # Fall back to first candidate or default
        chosen_dir = os.path.normpath(candidates[0] if candidates else os.path.join(script_dir, "../data_new/clean"))

    resolved_train = train_path or os.path.join(chosen_dir, "train.bin")
    resolved_val = val_path or os.path.join(chosen_dir, "val.bin")
    return resolved_train, resolved_val


# ---------------------------------------------------------------------------
# Core Append Function
# ---------------------------------------------------------------------------

def append_data(
    n_train: int = 1_000_000,
    n_val: int = None,
    target_train: int = None,
    data_dir: str = None,
    train_path: str = None,
    val_path: str = None,
    min_depth: int = 20,
    max_abs_cp: int = 3000,
    keep_mate: bool = False,
    skip_rows: int = 0,
    start_shard: int = None,
    exact_skip: bool = False,
    seed: int = 0,
    workers: int = None,
    batch_size: int = 2000,
    max_in_flight: int = None,
    use_cache_index: bool = True,
    reindex: bool = False,
    progress_every: int = 50_000,
    source: str = "Lichess/chess-position-evaluations",
) -> dict:
    """Append new positions to train.bin and val.bin without duplicates.

    Returns a summary dictionary with counts and performance stats.
    """
    train_path, val_path = resolve_data_paths(data_dir, train_path, val_path)
    os.makedirs(os.path.dirname(train_path), exist_ok=True)
    os.makedirs(os.path.dirname(val_path), exist_ok=True)

    workers = workers or os.cpu_count() or 4
    if max_in_flight is None:
        max_in_flight = max(4, workers * 4)

    # Validate existing files and check 68-byte record alignment
    for p in (train_path, val_path):
        if os.path.exists(p):
            sz = os.path.getsize(p)
            rem = sz % RECORD_SIZE
            if rem != 0:
                print(f"[Warning] Truncating {rem} unaligned trailing bytes from {p}...")
                with open(p, "a+b") as fix_f:
                    fix_f.truncate(sz - rem)

    initial_train_records = (os.path.getsize(train_path) // RECORD_SIZE) if os.path.exists(train_path) else 0
    initial_val_records = (os.path.getsize(val_path) // RECORD_SIZE) if os.path.exists(val_path) else 0

    print("=" * 70)
    print("NNUE Dataset Appender (MultiPV scraping + Zero Duplicates)")
    print("=" * 70)
    print(f"Target Train Path:  {train_path}")
    print(f"  Existing records: {initial_train_records:,} ({(initial_train_records * RECORD_SIZE) / 1e9:.2f} GB)")
    print(f"Target Val Path:    {val_path}")
    print(f"  Existing records: {initial_val_records:,} ({(initial_val_records * RECORD_SIZE) / 1e6:.2f} MB)")

    # Adjust n_train if target_train is set
    if target_train is not None:
        needed = max(0, target_train - initial_train_records)
        print(f"Target train total: {target_train:,} -> need {needed:,} new records.")
        n_train = needed

    # Calculate n_val if not explicitly set
    if n_val is None:
        if initial_train_records > 0 and initial_val_records > 0:
            ratio = initial_val_records / initial_train_records
        else:
            ratio = DEFAULT_VAL_FRACTION
        n_val = max(1, int(n_train * ratio))

    print(f"To append:          +{n_train:,} train records, +{n_val:,} val records")
    print(f"Filter settings:    min_depth={min_depth}, max_abs_cp={max_abs_cp}, keep_mate={keep_mate}")
    print(f"Parallel workers:   {workers} processes (batch_size={batch_size:,})")
    if skip_rows == 0 and start_shard is None and initial_train_records > 1_000_000:
        print("-" * 70)
        print(f"[Notice] Streaming from row 0. Your existing {initial_train_records:,} records cover Shards 00..09.")
        print("         Positions in those shards will be detected and skipped as duplicates.")
        print("         Tip: use --start-shard 10 to jump directly to fresh, unexplored data!")
    print("=" * 70)

    if n_train <= 0 and n_val <= 0:
        print("Nothing to append. Target reached.")
        return {
            "train_path": train_path,
            "val_path": val_path,
            "initial_train": initial_train_records,
            "initial_val": initial_val_records,
            "added_train": 0,
            "added_val": 0,
            "final_train": initial_train_records,
            "final_val": initial_val_records,
        }

    # Load / build position hash index
    existing_hashes = load_or_build_hash_index(
        train_path=train_path,
        val_path=val_path,
        cache_dir=os.path.dirname(train_path),
        use_cache=use_cache_index,
        reindex=reindex,
    )

    new_hashes_set = set()
    newly_added_hashes = []

    # Calculate validation fraction with 10% headroom
    total_target = n_train + n_val
    val_fraction = max(0.002, min(0.5, (n_val / total_target) * 1.1)) if total_target > 0 else DEFAULT_VAL_FRACTION

    row_iter = iter_filtered_rows(
        source=source,
        min_depth=min_depth,
        max_abs_cp=max_abs_cp,
        keep_mate=keep_mate,
        skip_rows=skip_rows,
        start_shard=start_shard,
        exact_skip=exact_skip,
    )
    batch_iter = batched(row_iter, batch_size)

    n_train_written = 0
    n_val_written = 0
    n_scanned = 0
    n_dup_existing = 0
    n_dup_stream = 0
    start_time = time.time()

    pbar = tqdm(total=n_train, unit="rec", desc="Append Train") if HAVE_TQDM else None

    # Open files in binary append mode ("ab")
    with ProcessPoolExecutor(max_workers=workers) as ex, \
         open(train_path, "ab") as f_train, \
         open(val_path, "ab") as f_val:

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
            futures.append(ex.submit(process_batch, batch, seed + next_batch_id))
            next_batch_id += 1

        # Prime pipeline with just a few batches to start processing immediately (<2s),
        # then keep the pipeline refilled dynamically inside the loop.
        initial_prime = min(workers, 4)
        for _ in range(initial_prime):
            submit_next()

        train_buf = bytearray()
        val_buf = bytearray()
        done = False

        try:
            while futures and not done:
                # Keep pipeline filled up to max_in_flight
                while len(futures) < max_in_flight and not stream_exhausted and not done:
                    submit_next()

                fut = futures.popleft()
                batch_res = fut.result()
                if not batch_res:
                    continue

                # Vectorized search against existing_hashes for fast batch check
                b_hashes = np.fromiter((item[1] for item in batch_res), dtype=np.uint64, count=len(batch_res))
                if len(existing_hashes) > 0:
                    idx = np.searchsorted(existing_hashes, b_hashes)
                    idx = np.minimum(idx, len(existing_hashes) - 1)
                    in_existing = (existing_hashes[idx] == b_hashes)
                else:
                    in_existing = np.zeros(len(b_hashes), dtype=bool)

                for i, (rec, h, r) in enumerate(batch_res):
                    n_scanned += 1
                    # 1. Skip if already in existing dataset
                    if in_existing[i]:
                        n_dup_existing += 1
                        continue

                    # 2. Skip if already accepted during this streaming run
                    if h in new_hashes_set:
                        n_dup_stream += 1
                        continue

                    new_hashes_set.add(h)
                    newly_added_hashes.append(h)

                    # 3. Route to val or train
                    if n_val_written < n_val and r < val_fraction:
                        val_buf += rec
                        n_val_written += 1
                    elif n_train_written < n_train:
                        train_buf += rec
                        n_train_written += 1
                        if pbar is not None:
                            pbar.update(1)

                    if n_train_written >= n_train and n_val_written >= n_val:
                        done = True
                        break

                if len(train_buf) >= FLUSH_BYTES:
                    f_train.write(train_buf)
                    train_buf.clear()
                if len(val_buf) >= FLUSH_BYTES:
                    f_val.write(val_buf)
                    val_buf.clear()

                # Always update progress postfix every batch so the user sees live activity
                if pbar is not None:
                    pbar.set_postfix(
                        val=f"+{n_val_written:,}/{n_val:,}",
                        dup_exist=f"{n_dup_existing:,}",
                        dup_stream=f"{n_dup_stream:,}",
                        scanned=f"{n_scanned:,}",
                        refresh=True,
                    )
                elif n_scanned % progress_every == 0:
                    el = time.time() - start_time
                    rate = n_train_written / el if el > 0 else 0
                    rem = n_train - n_train_written
                    eta = (rem / rate) / 60 if rate > 0 else 0
                    print(
                        f"scanned={n_scanned:,} "
                        f"train=+{n_train_written:,}/{n_train:,} "
                        f"val=+{n_val_written:,}/{n_val:,} "
                        f"dup_exist={n_dup_existing:,} "
                        f"dup_stream={n_dup_stream:,} "
                        f"rate={rate:,.0f} rec/s "
                        f"eta={eta:.1f}m",
                        flush=True,
                    )

        except (KeyboardInterrupt, Exception) as e:
            if not isinstance(e, KeyboardInterrupt):
                traceback.print_exc()
            print(f"\n[Safeguard] Interrupted ({type(e).__name__}). Saving collected records...")
        finally:
            if train_buf:
                f_train.write(train_buf)
                train_buf.clear()
            if val_buf:
                f_val.write(val_buf)
                val_buf.clear()

            f_train.flush()
            f_val.flush()

            # Ensure strict 68-byte record alignment
            for p in (train_path, val_path):
                if os.path.exists(p):
                    sz = os.path.getsize(p)
                    rem = sz % RECORD_SIZE
                    if rem != 0:
                        with open(p, "a+b") as fix_f:
                            fix_f.truncate(sz - rem)
                        print(f"[Safeguard] Truncated {rem} trailing bytes from {p} for clean record alignment.")

            if futures:
                ex.shutdown(wait=False, cancel_futures=True)

            if pbar is not None:
                pbar.close()

    elapsed = time.time() - start_time
    final_train_records = (os.path.getsize(train_path) // RECORD_SIZE) if os.path.exists(train_path) else 0
    final_val_records = (os.path.getsize(val_path) // RECORD_SIZE) if os.path.exists(val_path) else 0
    actual_train_added = final_train_records - initial_train_records
    actual_val_added = final_val_records - initial_val_records

    # Update hash index cache if new records were added
    if use_cache_index and len(newly_added_hashes) > 0:
        print("[Hash Index] Updating position hash cache with newly appended records...")
        new_arr = np.asarray(newly_added_hashes[:actual_train_added + actual_val_added], dtype=np.uint64)
        if len(existing_hashes) > 0 and len(new_arr) > 0:
            new_unique = np.unique(new_arr)
            idx = np.searchsorted(existing_hashes, new_unique)
            idx_clamped = np.minimum(idx, len(existing_hashes) - 1)
            mask = (existing_hashes[idx_clamped] != new_unique)
            if mask.any():
                updated_h = np.insert(existing_hashes, idx[mask], new_unique[mask])
            else:
                updated_h = existing_hashes
        else:
            updated_h = np.unique(new_arr)
        save_hash_index(os.path.dirname(train_path), updated_h, train_path, val_path)

    print("\n" + "=" * 70)
    print("Append Run Summary")
    print("=" * 70)
    print(f"Train records:     {initial_train_records:,} -> {final_train_records:,} (+{actual_train_added:,})")
    print(f"Val records:       {initial_val_records:,} -> {final_val_records:,} (+{actual_val_added:,})")
    print(f"Total positions:   {final_train_records + final_val_records:,}")
    print(f"Scanned positions: {n_scanned:,}")
    print(f"Duplicates pruned: {n_dup_existing:,} from existing dataset, {n_dup_stream:,} from stream")
    print(f"Total time:        {elapsed/60:.2f} min ({actual_train_added / max(0.001, elapsed):,.0f} train rec/s)")
    print("=" * 70)

    return {
        "train_path": train_path,
        "val_path": val_path,
        "initial_train": initial_train_records,
        "initial_val": initial_val_records,
        "added_train": actual_train_added,
        "added_val": actual_val_added,
        "final_train": final_train_records,
        "final_val": final_val_records,
        "duplicates_skipped_existing": n_dup_existing,
        "duplicates_skipped_new": n_dup_stream,
        "elapsed_seconds": elapsed,
    }


def main():
    ap = argparse.ArgumentParser(
        description="Append positions to NNUE dataset with MultiPV scraping and zero duplicates."
    )
    ap.add_argument("--data-dir", default=None,
                    help="Directory containing train.bin and val.bin (default: auto-detected, e.g. ../data_new/clean).")
    ap.add_argument("--train", default=None, help="Explicit path to train.bin.")
    ap.add_argument("--val", default=None, help="Explicit path to val.bin.")
    ap.add_argument("--n-train", type=int, default=1_000_000,
                    help="Number of new training records to append (default: 1,000,000).")
    ap.add_argument("--n-val", type=int, default=None,
                    help="Number of new validation records to append (default: auto ~1.6%%).")
    ap.add_argument("--target-train", type=int, default=None,
                    help="Target total training records (appends max(0, target - current)).")
    ap.add_argument("--min-depth", type=int, default=20,
                    help="Minimum evaluation depth (default: 20).")
    ap.add_argument("--max-abs-cp", type=int, default=3000,
                    help="Maximum centipawn evaluation (default: 3000).")
    ap.add_argument("--keep-mate", action="store_true",
                    help="Include forced mate positions.")
    ap.add_argument("--skip-rows", type=int, default=0,
                    help="Number of raw rows to skip on the Hugging Face stream (default: 0). Auto-maps to instant shard jumping!")
    ap.add_argument("--start-shard", type=int, default=None,
                    help="Directly start from Parquet shard 0..19 (instantly skips earlier shards with 0s network overhead).")
    ap.add_argument("--exact-skip", action="store_true",
                    help="Perform exact row-by-row skipping inside the shard instead of snapping to the shard boundary.")
    ap.add_argument("--seed", type=int, default=0, help="Random seed.")
    ap.add_argument("--workers", type=int, default=os.cpu_count(),
                    help="Number of worker processes.")
    ap.add_argument("--batch-size", type=int, default=2000,
                    help="Rows per batch for worker processes.")
    ap.add_argument("--max-in-flight", type=int, default=None,
                    help="Max batches queued at once (default: 4x workers).")
    ap.add_argument("--no-cache-index", action="store_true",
                    help="Do not save or load hash index cache file.")
    ap.add_argument("--reindex", action="store_true",
                    help="Force re-indexing the existing dataset from scratch.")
    ap.add_argument("--progress-every", type=int, default=50_000,
                    help="Status line interval if tqdm is missing.")
    ap.add_argument("--source", default="Lichess/chess-position-evaluations",
                    help="Dataset source (Hugging Face repo or custom iterable).")

    args = ap.parse_args()

    append_data(
        n_train=args.n_train,
        n_val=args.n_val,
        target_train=args.target_train,
        data_dir=args.data_dir,
        train_path=args.train,
        val_path=args.val,
        min_depth=args.min_depth,
        max_abs_cp=args.max_abs_cp,
        keep_mate=args.keep_mate,
        skip_rows=args.skip_rows,
        start_shard=args.start_shard,
        exact_skip=args.exact_skip,
        seed=args.seed,
        workers=args.workers,
        batch_size=args.batch_size,
        max_in_flight=args.max_in_flight,
        use_cache_index=not args.no_cache_index,
        reindex=args.reindex,
        progress_every=args.progress_every,
        source=args.source,
    )


if __name__ == "__main__":
    main()
