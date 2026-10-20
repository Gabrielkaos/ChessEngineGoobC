#!/usr/bin/env python3
"""
Mixes native self-play generated data with Lichess base training data (Phase 2).

Produces blended datasets for Phase 2 mix experiments:
  D1: ~25% self-play data + ~75% base data
  D2: ~50% self-play data + ~50% base data
  D3: D1 dataset trained with --wdl-lambda 0.5

Extracts an optional held-out generated validation set (val_gen.bin) to monitor
MSE on self-play positions during training alongside Lichess val.bin.

Usage:
    python3 mix_data.py --gen-data <file_or_dir> --base-data <train.bin> --out <mixed_train.bin> [options]

Examples:
    # Build D1 dataset (25% generated share) with a 50k held-out generated val set:
    python3 tools/nnue_project/scripts/mix_data.py \\
        --gen-data tools/nnue_project/data_gen \\
        --base-data tools/nnue_project/data/train.bin \\
        --gen-share 0.25 \\
        --val-out tools/nnue_project/checkpoints/exp_datagen/val_gen.bin \\
        --val-count 50000 \\
        --out tools/nnue_project/checkpoints/exp_datagen/d1_train.bin
"""

import argparse
import glob
import os
import random
import shutil
import struct
import sys
import tempfile
import numpy as np

RECORD_SIZE = 68
_STRUCT = struct.Struct("<64sBhB")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Mix native self-play data with base training data for NNUE training."
    )
    parser.add_argument(
        "--gen-data",
        type=str,
        required=True,
        help="Path to generated .bin file or directory containing generated .bin files.",
    )
    parser.add_argument(
        "--base-data",
        type=str,
        required=True,
        help="Path to base dataset (e.g. tools/nnue_project/data/train.bin).",
    )
    parser.add_argument(
        "--out",
        type=str,
        required=True,
        help="Path to output mixed binary dataset (e.g. tools/nnue_project/checkpoints/exp_datagen/d1_train.bin).",
    )
    parser.add_argument(
        "--gen-share",
        type=float,
        default=0.25,
        help="Target share of generated data in the final dataset (default: 0.25 = 25%%).",
    )
    parser.add_argument(
        "--val-out",
        type=str,
        default=None,
        help="Optional path to write a held-out generated validation set (e.g. val_gen.bin).",
    )
    parser.add_argument(
        "--val-count",
        type=int,
        default=50000,
        help="Number of generated positions to hold out for validation (default: 50,000).",
    )
    parser.add_argument(
        "--max-total",
        type=int,
        default=0,
        help="Maximum total records in output dataset (0 = compute from gen-share and available data).",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="Random seed for sampling and shuffling (default: 42).",
    )
    parser.add_argument(
        "--no-shuffle",
        action="store_true",
        help="Disable final shuffle of output records (default: shuffle enabled).",
    )
    return parser.parse_args()


def get_gen_files(path):
    if os.path.isfile(path):
        return [path]
    if os.path.isdir(path):
        files = sorted(glob.glob(os.path.join(path, "*.bin")))
        return files
    files = sorted(glob.glob(path))
    if files:
        return files
    raise FileNotFoundError(f"No binary files found matching: {path}")


def verify_protection(out_path, base_path):
    out_abs = os.path.abspath(out_path)
    base_abs = os.path.abspath(base_path)
    if out_abs == base_abs:
        raise ValueError(f"FATAL: Destination '{out_path}' matches base dataset! Never overwrite base data.")
    for forbidden in ["quantised.bin", "nnue.pt", "data/train.bin", "data/val.bin", "data_selfplay"]:
        if forbidden in out_abs:
            raise ValueError(f"FATAL: Destination '{out_path}' violates NNUE protection rules ({forbidden}).")
    if os.path.exists(out_abs):
        raise ValueError(f"FATAL: Output file '{out_path}' already exists! NNUE protection refuses to overwrite.")


def main():
    args = parse_args()

    verify_protection(args.out, args.base_data)
    if args.val_out:
        verify_protection(args.val_out, args.base_data)

    gen_files = get_gen_files(args.gen_data)
    if not gen_files:
        print(f"Error: No generated data files found at '{args.gen_data}'.")
        sys.exit(1)

    total_gen_bytes = sum(os.path.getsize(f) for f in gen_files)
    total_gen_records = total_gen_bytes // RECORD_SIZE
    print(f"Found {len(gen_files)} generated file(s) with {total_gen_records:,} total records.")

    if not os.path.isfile(args.base_data):
        print(f"Error: Base dataset not found at '{args.base_data}'.")
        sys.exit(1)

    base_bytes = os.path.getsize(args.base_data)
    total_base_records = base_bytes // RECORD_SIZE
    print(f"Base dataset '{args.base_data}': {total_base_records:,} records.")

    rng = np.random.default_rng(args.seed)

    # 1. Hold out validation records if requested
    val_records = []
    available_gen_records = total_gen_records
    val_count = min(args.val_count, total_gen_records // 10) if args.val_out else 0

    if val_count > 0:
        print(f"Holding out {val_count:,} generated records for validation -> '{args.val_out}'")
        available_gen_records -= val_count

    # 2. Determine sample counts for training
    gen_share = args.gen_share
    if gen_share <= 0.0 or gen_share >= 1.0:
        print(f"Error: --gen-share must be between 0.0 and 1.0, got {gen_share}")
        sys.exit(1)

    if args.max_total > 0:
        n_gen_needed = int(args.max_total * gen_share)
        n_base_needed = args.max_total - n_gen_needed
    else:
        # Scale to use all available generated data
        n_gen_needed = available_gen_records
        n_base_needed = int(n_gen_needed * (1.0 - gen_share) / gen_share)
        if n_base_needed > total_base_records:
            n_base_needed = total_base_records
            n_gen_needed = int(n_base_needed * gen_share / (1.0 - gen_share))

    n_gen_needed = min(n_gen_needed, available_gen_records)
    n_base_needed = min(n_base_needed, total_base_records)
    total_output = n_gen_needed + n_base_needed
    actual_gen_share = n_gen_needed / total_output if total_output > 0 else 0.0

    print("================================================================================")
    print("Dataset Mixing Plan:")
    print(f"  Generated Records to Sample: {n_gen_needed:,} ({actual_gen_share*100:.1f}%)")
    print(f"  Base Records to Sample     : {n_base_needed:,} ({(1-actual_gen_share)*100:.1f}%)")
    print(f"  Total Output Records       : {total_output:,} ({(total_output * RECORD_SIZE) / (1024*1024):.2f} MB)")
    print(f"  Target File                : {args.out}")
    print("================================================================================")

    # Read generated records across files
    print("Reading generated records...")
    all_gen_records = []
    for fpath in gen_files:
        fsize = os.path.getsize(fpath)
        n_recs = fsize // RECORD_SIZE
        if n_recs == 0:
            continue
        mm = np.memmap(fpath, dtype=np.uint8, mode="r", shape=(n_recs, RECORD_SIZE))
        all_gen_records.append(mm)

    gen_concat = np.concatenate(all_gen_records, axis=0)
    perm_gen = rng.permutation(len(gen_concat))

    if val_count > 0 and args.val_out:
        os.makedirs(os.path.dirname(os.path.abspath(args.val_out)), exist_ok=True)
        val_indices = perm_gen[:val_count]
        train_gen_indices = perm_gen[val_count:val_count + n_gen_needed]
        val_data = gen_concat[val_indices]
        val_data.tofile(args.val_out)
        print(f"Wrote {val_count:,} validation records to '{args.val_out}'.")
    else:
        train_gen_indices = perm_gen[:n_gen_needed]

    sampled_gen = gen_concat[train_gen_indices]

    # Sample base records
    print(f"Reading base dataset ({n_base_needed:,} records)...")
    base_mm = np.memmap(args.base_data, dtype=np.uint8, mode="r", shape=(total_base_records, RECORD_SIZE))
    base_indices = None
    if n_base_needed < total_base_records:
        base_indices = rng.choice(total_base_records, size=n_base_needed, replace=False)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    print(f"Writing mixed dataset to '{args.out}'...")

    mate_count = 0
    w_count = 0
    d_count = 0
    b_count = 0
    no_res_count = 0

    def update_stats(chunk):
        nonlocal mate_count, w_count, d_count, b_count, no_res_count
        flags = chunk[:, 67]
        mate_count += int(np.sum(flags & 1))
        res_codes = (flags >> 1) & 0x03
        w_count += int(np.sum(res_codes == 1))
        d_count += int(np.sum(res_codes == 2))
        b_count += int(np.sum(res_codes == 3))
        no_res_count += int(np.sum(res_codes == 0))

    CHUNK = 500_000

    if args.no_shuffle:
        print("Streaming records directly to output file (no-shuffle)...")
        with open(args.out, "wb") as out_fp:
            for s in range(0, n_gen_needed, CHUNK):
                chunk = sampled_gen[s:s + CHUNK]
                update_stats(chunk)
                out_fp.write(chunk.tobytes())
            for s in range(0, n_base_needed, CHUNK):
                e = min(n_base_needed, s + CHUNK)
                chunk = base_mm[s:e] if base_indices is None else base_mm[base_indices[s:e]]
                update_stats(chunk)
                out_fp.write(chunk.tobytes())
    elif total_output <= 5_000_000:
        print("Combining and shuffling records in memory...")
        sampled_base = base_mm[:n_base_needed] if base_indices is None else base_mm[base_indices]
        combined = np.concatenate([sampled_gen, sampled_base], axis=0)
        combined = combined[rng.permutation(len(combined))]
        update_stats(combined)
        combined.tofile(args.out)
    else:
        # Large dataset: use disk-bucketed shuffle to prevent OOM
        n_buckets = max(8, min(64, int(np.ceil((total_output * RECORD_SIZE) / (256 * 1024 * 1024)))))
        bucket_dir = tempfile.mkdtemp(prefix="mix_buckets_", dir=os.path.dirname(os.path.abspath(args.out)))
        print(f"Distributing records across {n_buckets} temporary disk buckets ({bucket_dir})...")
        bucket_fps = [open(os.path.join(bucket_dir, f"bucket_{b}.bin"), "wb") for b in range(n_buckets)]
        try:
            for s in range(0, n_gen_needed, CHUNK):
                chunk = sampled_gen[s:s + CHUNK]
                b_ids = rng.integers(0, n_buckets, size=len(chunk))
                order = np.argsort(b_ids, kind="stable")
                bounds = np.searchsorted(b_ids[order], np.arange(n_buckets + 1))
                for b in range(n_buckets):
                    if bounds[b + 1] > bounds[b]:
                        bucket_fps[b].write(chunk[order[bounds[b]:bounds[b + 1]]].tobytes())

            for s in range(0, n_base_needed, CHUNK):
                e = min(n_base_needed, s + CHUNK)
                chunk = base_mm[s:e] if base_indices is None else base_mm[base_indices[s:e]]
                b_ids = rng.integers(0, n_buckets, size=len(chunk))
                order = np.argsort(b_ids, kind="stable")
                bounds = np.searchsorted(b_ids[order], np.arange(n_buckets + 1))
                for b in range(n_buckets):
                    if bounds[b + 1] > bounds[b]:
                        bucket_fps[b].write(chunk[order[bounds[b]:bounds[b + 1]]].tobytes())

            for fp in bucket_fps:
                fp.close()

            print(f"Compiling shuffled dataset from disk buckets into '{args.out}'...")
            with open(args.out, "wb") as out_fp:
                for b in range(n_buckets):
                    b_path = os.path.join(bucket_dir, f"bucket_{b}.bin")
                    if os.path.exists(b_path):
                        b_data = np.fromfile(b_path, dtype=np.uint8).reshape(-1, RECORD_SIZE)
                        if len(b_data) > 0:
                            b_data = b_data[rng.permutation(len(b_data))]
                            update_stats(b_data)
                            out_fp.write(b_data.tobytes())
                        del b_data
                        os.remove(b_path)
        finally:
            shutil.rmtree(bucket_dir, ignore_errors=True)

    print("================================================================================")
    print("Mixed Dataset Created Successfully:")
    print(f"  Total Records  : {total_output:,}")
    print(f"  Base Records   : {no_res_count:,} ({(no_res_count/total_output)*100:.1f}%)" if total_output > 0 else "  Base Records: 0")
    print(f"  Gen Records    : {total_output-no_res_count:,} ({((total_output-no_res_count)/total_output)*100:.1f}%)" if total_output > 0 else "  Gen Records: 0")
    print(f"  W / D / L Gen  : {w_count:,} / {d_count:,} / {b_count:,}")
    print(f"  Mate Evaluated : {mate_count:,}")
    print(f"  Output Path    : {args.out}")
    print("================================================================================")


if __name__ == "__main__":
    main()
