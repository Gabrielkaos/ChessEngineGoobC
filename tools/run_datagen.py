#!/usr/bin/env python3
"""
Multi-process coordinator for native C self-play data generation (Phase 2).

Spawns one datagen process per CPU core, with unique seeds and output files,
monitoring overall throughput and progress.

Usage:
    python3 tools/run_datagen.py [options]

Examples:
    # Generate 100,000 positions across 8 workers at 5,000 nodes/move:
    python3 tools/run_datagen.py --workers 8 --positions 100000 --nodes 5000

    # Generate 5,000 games using all available CPU cores:
    python3 tools/run_datagen.py --games 5000 --out-dir tools/nnue_project/data_gen
"""

import argparse
import os
import signal
import subprocess
import sys
import time

def parse_args():
    parser = argparse.ArgumentParser(
        description="Parallel runner for GOOB native self-play data generator (tools/datagen)."
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=os.cpu_count() or 4,
        help="Number of concurrent worker processes (default: number of logical CPUs).",
    )
    parser.add_argument(
        "--positions",
        type=int,
        default=0,
        help="Total target positions across all workers (0 = unlimited).",
    )
    parser.add_argument(
        "--games",
        type=int,
        default=0,
        help="Total target games across all workers (0 = unlimited).",
    )
    parser.add_argument(
        "--nodes",
        type=int,
        default=5000,
        help="Search nodes per move (default: 5000).",
    )
    parser.add_argument(
        "--out-dir",
        type=str,
        default="tools/nnue_project/data_gen",
        help="Output directory for generated .bin files (default: tools/nnue_project/data_gen).",
    )
    parser.add_argument(
        "--prefix",
        type=str,
        default="gen_w",
        help="Prefix for worker output filenames (default: 'gen_w').",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=1,
        help="Base random seed (default: 1; worker i gets seed + i * 10007).",
    )
    parser.add_argument(
        "--bin",
        type=str,
        default="tools/datagen",
        help="Path to compiled datagen executable (default: tools/datagen).",
    )
    parser.add_argument(
        "--book",
        type=str,
        default="tools/book.epd",
        help="Opening book in EPD/FEN format (default: tools/book.epd).",
    )
    parser.add_argument(
        "--resign-eval",
        type=int,
        default=1500,
        help="Resign evaluation threshold in cp (default: 1500).",
    )
    parser.add_argument(
        "--draw-eval",
        type=int,
        default=10,
        help="Draw adjudication evaluation threshold in cp (default: 10).",
    )
    parser.add_argument(
        "--max-ply",
        type=int,
        default=400,
        help="Maximum plies per game before draw adjudication (default: 400).",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    datagen_bin = os.path.join(repo_root, args.bin) if not os.path.isabs(args.bin) else args.bin

    if not os.path.isfile(datagen_bin) or not os.access(datagen_bin, os.X_OK):
        print(f"Error: datagen executable not found at '{datagen_bin}'.")
        print("Build it from repository root:")
        print("  make -C src datagen")
        print("Or compile manually:")
        print("  gcc -O3 -march=native -pthread -DUSE_PEXT -DNNUE_MIRROR=1 -DNNUE_KING_BUCKETS=4 \\")
        print("      -DEVALFILE='\"src/weights/quantised.bin\"' -Isrc \\")
        print("      tools/datagen.c $(ls src/*.c | grep -v src/main.c) -lm -o tools/datagen")
        sys.exit(1)

    out_dir = os.path.join(repo_root, args.out_dir) if not os.path.isabs(args.out_dir) else args.out_dir
    os.makedirs(out_dir, exist_ok=True)

    pos_per_worker = (args.positions + args.workers - 1) // args.workers if args.positions > 0 else 0
    games_per_worker = (args.games + args.workers - 1) // args.workers if args.games > 0 else 0

    print("================================================================================")
    print("GOOB Multi-Process Datagen Runner:")
    print(f"  Executable       : {datagen_bin}")
    print(f"  Workers          : {args.workers}")
    print(f"  Nodes / Move     : {args.nodes}")
    print(f"  Total Positions  : {args.positions:,}" if args.positions else "  Total Positions  : Unlimited")
    print(f"  Total Games      : {args.games:,}" if args.games else "  Total Games      : Unlimited")
    print(f"  Output Directory : {out_dir}")
    print("================================================================================")

    processes = []
    worker_files = []

    def handle_sigint(signum, frame):
        print("\nTerminating worker processes...")
        for p, _, _ in processes:
            if p.poll() is None:
                p.terminate()
        for p, _, _ in processes:
            p.wait()
        print("All processes terminated.")
        sys.exit(0)

    signal.signal(signal.SIGINT, handle_sigint)
    signal.signal(signal.SIGTERM, handle_sigint)

    timestamp = int(time.time())
    for w in range(args.workers):
        worker_seed = args.seed + w * 10007
        filename = f"{args.prefix}{w}_{timestamp}.bin"
        out_file = os.path.join(out_dir, filename)
        worker_files.append(out_file)

        cmd = [
            datagen_bin,
            "--out", out_file,
            "--nodes", str(args.nodes),
            "--seed", str(worker_seed),
            "--book", args.book,
            "--resign-eval", str(args.resign_eval),
            "--draw-eval", str(args.draw_eval),
            "--max-ply", str(args.max_ply),
        ]
        if pos_per_worker > 0:
            cmd.extend(["--positions", str(pos_per_worker)])
        if games_per_worker > 0:
            cmd.extend(["--games", str(games_per_worker)])

        log_file = os.path.join(out_dir, f"{args.prefix}{w}_{timestamp}.log")
        log_fp = open(log_file, "w")

        p = subprocess.Popen(
            cmd,
            stdout=log_fp,
            stderr=subprocess.STDOUT,
            cwd=repo_root,
        )
        processes.append((p, log_fp, out_file))
        print(f"  Spawned worker {w:2d} (PID {p.pid:6d}) -> {filename} (seed {worker_seed})")

    t_start = time.time()
    print("\nWorkers running. Monitoring progress (Ctrl+C to stop)...")

    RECORD_SIZE = 68
    while True:
        alive = sum(1 for p, _, _ in processes if p.poll() is None)
        total_bytes = 0
        for _, _, fpath in processes:
            if os.path.exists(fpath):
                total_bytes += os.path.getsize(fpath)

        current_positions = total_bytes // RECORD_SIZE
        elapsed = time.time() - t_start
        rate = current_positions / elapsed if elapsed > 0 else 0.0

        pct_str = ""
        if args.positions > 0:
            pct = (current_positions / args.positions) * 100.0
            pct_str = f" | {pct:5.1f}% target"

        sys.stdout.write(
            f"\rActive workers: {alive:2d}/{args.workers:2d} | Positions: {current_positions:10,d}{pct_str} | Elapsed: {elapsed:6.1f}s | Rate: {rate:7.1f} pos/s"
        )
        sys.stdout.flush()

        if alive == 0:
            break
        time.sleep(1.0)

    print("\n\nAll workers completed.")
    for p, log_fp, _ in processes:
        p.wait()
        log_fp.close()

    total_bytes = 0
    for f in worker_files:
        if os.path.exists(f):
            total_bytes += os.path.getsize(f)

    final_positions = total_bytes // RECORD_SIZE
    total_time = time.time() - t_start
    final_rate = final_positions / total_time if total_time > 0 else 0.0

    print("================================================================================")
    print("Datagen Summary:")
    print(f"  Total Positions Generated: {final_positions:,}")
    print(f"  Total Data Size          : {total_bytes / (1024 * 1024):.2f} MB")
    print(f"  Total Elapsed Time       : {total_time:.2f} s")
    print(f"  Aggregate Generation Rate: {final_rate:.1f} pos/s")
    print(f"  Generated Files Directory: {out_dir}")
    print("================================================================================")


if __name__ == "__main__":
    main()
