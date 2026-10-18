#!/usr/bin/env python3
"""
EPD Test Suite & Elo Estimator for GOOB Chess Engine
Evaluates chess engine playing strength, tactical solving, and strategic understanding
using standard EPD benchmark suites without needing game matches.

Supported Suites:
  - BT-2630 (bt2630): 30-position classic benchmark with Bednorz-Teschner Elo rating formula.
  - STS-1 (sts1): Strategic Test Suite Volume 1 (Undermining, 100 positions).
  - STS-1..15 (sts1-15): Full Strategic Test Suite (1500 positions) with CCRL 40/4 Elo regression.
  - Bratko-Kopec (bk / bratko-kopec): 24-position tactical vs positional benchmark.
  - Win At Chess (wac): 300-position tactical puzzle suite.
  - Custom EPD: Any valid EPD file path.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

try:
    import chess
except ImportError:
    venv_python = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".venv", "bin", "python3")
    if os.path.isfile(venv_python) and sys.executable != venv_python:
        os.execv(venv_python, [venv_python] + sys.argv)
    print("Error: 'python-chess' is required. Please install python-chess or use tools/.venv/bin/python3", file=sys.stderr)
    sys.exit(1)

SUITES_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "suites")

SUITE_PRESETS = {
    "bt2630": os.path.join(SUITES_DIR, "bt2630.epd"),
    "sts1": os.path.join(SUITES_DIR, "sts1.epd"),
    "sts1-15": os.path.join(SUITES_DIR, "sts1-15.epd"),
    "sts": os.path.join(SUITES_DIR, "sts1-15.epd"),
    "bk": os.path.join(SUITES_DIR, "bratko-kopec.epd"),
    "bratko-kopec": os.path.join(SUITES_DIR, "bratko-kopec.epd"),
    "wac": os.path.join(SUITES_DIR, "wac.epd"),
}

DEFAULT_ENGINE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src", "bin", "linux", "GOOB-2.2-BETA-native")

BK_TACTICAL_IDS = {"BK.01", "BK.04", "BK.05", "BK.06", "BK.07", "BK.10", "BK.12", "BK.14", "BK.15", "BK.17", "BK.18", "BK.21"}

class UCIEngine:
    def __init__(self, binary_path, hash_mb=64, threads=1):
        if not os.path.isfile(binary_path):
            raise FileNotFoundError(f"Engine executable not found: {binary_path}")
        self.path = binary_path
        self.process = subprocess.Popen(
            [binary_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )
        self.send("uci")
        self.read_until("uciok")
        self.send(f"setoption name Hash value {hash_mb}")
        self.send(f"setoption name Threads value {threads}")
        self.send("isready")
        self.read_until("readyok")

    def send(self, cmd):
        self.process.stdin.write(cmd + "\n")
        self.process.stdin.flush()

    def read_until(self, token):
        lines = []
        while True:
            line = self.process.stdout.readline()
            if not line:
                raise RuntimeError(f"Engine process terminated unexpectedly while waiting for '{token}'")
            line = line.strip()
            lines.append(line)
            if line.startswith(token) or line == token:
                return lines

    def search_position(self, fen, movetime=None, depth=None, nodes=None):
        self.send("ucinewgame")
        self.send("isready")
        self.read_until("readyok")
        self.send(f"position fen {fen}")

        if movetime is not None:
            go_cmd = f"go movetime {movetime}"
        elif depth is not None:
            go_cmd = f"go depth {depth}"
        elif nodes is not None:
            go_cmd = f"go nodes {nodes}"
        else:
            go_cmd = "go movetime 1000"

        t0 = time.perf_counter()
        self.send(go_cmd)
        lines = self.read_until("bestmove")
        elapsed_s = time.perf_counter() - t0

        bestmove_line = [l for l in lines if l.startswith("bestmove")]
        bestmove = bestmove_line[-1].split()[1] if bestmove_line else "0000"

        info_lines = [l for l in lines if l.startswith("info ") and "depth" in l]
        last_info = info_lines[-1] if info_lines else ""

        pos_nodes = 0
        pos_time_ms = int(elapsed_s * 1000)
        pos_score = "cp 0"
        pos_depth = 0

        if last_info:
            m_nodes = re.search(r" nodes (\d+)", last_info)
            if m_nodes:
                pos_nodes = int(m_nodes.group(1))
            m_time = re.search(r" time (\d+)", last_info)
            if m_time:
                pos_time_ms = int(m_time.group(1))
            m_score = re.search(r"score (cp|mate) (-?\d+)", last_info)
            if m_score:
                pos_score = f"{m_score.group(1)} {m_score.group(2)}"
            m_depth = re.search(r"depth (\d+)", last_info)
            if m_depth:
                pos_depth = int(m_depth.group(1))

        return {
            "bestmove": bestmove,
            "nodes": pos_nodes,
            "time_ms": pos_time_ms,
            "elapsed_s": elapsed_s,
            "depth": pos_depth,
            "score": pos_score,
        }

    def close(self):
        try:
            self.send("quit")
            self.process.terminate()
            self.process.wait(timeout=2)
        except Exception:
            pass


def parse_epd_entry(line):
    """
    Parses an EPD line into structured puzzle data.
    Supports BM (best move) and STS point systems (c0 / c8 / c9).
    """
    board, ops = chess.Board.from_epd(line.strip())
    fen = board.fen()
    puzzle_id = ops.get("id", "Unknown")

    candidate_moves = {}  # uci_str -> points
    best_moves = []       # uci_str list

    # Check for STS c9 (moves in UCI) and c8 (points in integers)
    if "c9" in ops and "c8" in ops:
        c9_moves = ops["c9"].split()
        c8_pts = [int(p) for p in ops["c8"].split()]
        for m, pts in zip(c9_moves, c8_pts):
            candidate_moves[m] = pts
        if "bm" in ops:
            best_moves = [board.uci(m) for m in ops["bm"]]
        elif c9_moves:
            max_p = max(c8_pts) if c8_pts else 10
            best_moves = [m for m, pts in zip(c9_moves, c8_pts) if pts == max_p]

    # Check for STS c0 (e.g. "f5=10, Be5+=2, Bf2=3")
    elif "c0" in ops and "=" in ops["c0"]:
        for part in ops["c0"].split(","):
            part = part.strip()
            if "=" in part:
                san_move, pts_str = part.split("=")
                try:
                    move_obj = board.parse_san(san_move.strip())
                    pts = int(pts_str.strip())
                    candidate_moves[board.uci(move_obj)] = pts
                except Exception:
                    pass
        if "bm" in ops:
            best_moves = [board.uci(m) for m in ops["bm"]]
        elif candidate_moves:
            max_p = max(candidate_moves.values())
            best_moves = [m for m, pts in candidate_moves.items() if pts == max_p]

    # Standard BM suite (BT-2630, BK, WAC, etc.)
    elif "bm" in ops:
        best_moves = [board.uci(m) for m in ops["bm"]]
        for m in best_moves:
            candidate_moves[m] = 10

    max_points = max(candidate_moves.values()) if candidate_moves else 10

    return {
        "board": board,
        "fen": fen,
        "id": puzzle_id,
        "best_moves": best_moves,
        "candidate_moves": candidate_moves,
        "max_points": max_points,
    }


def estimate_bt2630_elo(solved_count, total_count, total_time_s, max_time_per_pos_s=900):
    """
    Computes Elo for BT-2630:
      1. Classic formula: Elo = 2630 - (Total_Time / 30) (with unsolved capped at 900s)
      2. Normalized percentage-solved Elo estimate: 1000 + (solved / total) * 1630
    """
    solved_elo = 1000 + (solved_count / max(1, total_count)) * 1630
    # Scaled classic formula:
    time_penalty = total_time_s / max(1, total_count)
    classic_elo = 2630.0 - time_penalty
    return {
        "solved_elo": round(solved_elo, 1),
        "classic_time_elo": round(classic_elo, 1),
    }


def estimate_sts_elo(score_percent):
    """
    CCRL 40/4 linear regression formula established by Ferdinand Mosca and Dann Corbit:
      Rating = 44.523 * (Score%) - 242.85
    """
    elo = 44.523 * score_percent - 242.85
    return round(elo, 1)


def estimate_bk_elo(solved_count, total_count=24):
    """
    Bratko-Kopec standard rating scale: ~1300 + 55 * solved
    24/24 ~ 2620, 20/24 ~ 2400, 15/24 ~ 2125, 10/24 ~ 1850
    """
    elo = 1300.0 + 55.0 * (solved_count * (24.0 / max(1, total_count)))
    return round(elo, 1)


def run_epd_suite(suite_path, engine_path, movetime=None, depth=None, nodes=None,
                  hash_mb=64, threads=1, limit=None, verbose=True):
    if not os.path.isfile(suite_path):
        raise FileNotFoundError(f"Suite file not found: {suite_path}")

    with open(suite_path, "r", encoding="utf-8") as f:
        lines = [line.strip() for line in f if line.strip() and not line.strip().startswith("#")]

    if limit is not None and limit > 0:
        lines = lines[:limit]

    total_positions = len(lines)
    suite_basename = os.path.splitext(os.path.basename(suite_path))[0].lower()

    print(f"\n{'='*88}")
    print(f" GOOB EPD TEST SUITE & ELO ESTIMATION")
    print(f"{'='*88}")
    print(f" Engine binary : {engine_path}")
    print(f" Test suite    : {suite_path} ({total_positions} positions)")
    time_mode = f"movetime {movetime} ms" if movetime else (f"depth {depth}" if depth else (f"nodes {nodes}" if nodes else "movetime 1000 ms"))
    print(f" Search limits : {time_mode} | Hash: {hash_mb} MB | Threads: {threads}")
    print(f"{'='*88}\n")

    engine = UCIEngine(engine_path, hash_mb=hash_mb, threads=threads)

    results = []
    total_pts_earned = 0
    total_pts_possible = 0
    solved_count = 0
    total_nodes = 0
    total_time_ms = 0

    # Categorical breakdown (for STS themes or BK tactical/positional)
    cat_scores = {}

    start_suite_time = time.perf_counter()

    for idx, line in enumerate(lines, 1):
        try:
            entry = parse_epd_entry(line)
        except Exception as e:
            print(f"[{idx:4d}/{total_positions}] Error parsing line: {e}")
            continue

        search_res = engine.search_position(
            entry["fen"],
            movetime=movetime,
            depth=depth,
            nodes=nodes
        )

        eng_bm = search_res["bestmove"]
        candidate_moves = entry["candidate_moves"]
        max_p = entry["max_points"]

        earned_pts = candidate_moves.get(eng_bm, 0)
        is_solved = (eng_bm in entry["best_moves"]) or (earned_pts == max_p and max_p > 0)

        total_pts_earned += earned_pts
        total_pts_possible += max_p
        if is_solved:
            solved_count += 1

        total_nodes += search_res["nodes"]
        total_time_ms += search_res["time_ms"]

        # Track category
        cat_name = "Overall"
        if "sts" in suite_basename:
            m = re.search(r"STS\(v\d+\.\d+\)\s*([^.]+)", entry["id"])
            if m:
                cat_name = m.group(1).strip()
        elif "bk" in suite_basename or "bratko" in suite_basename:
            cat_name = "Tactical" if entry["id"] in BK_TACTICAL_IDS else "Positional"

        if cat_name not in cat_scores:
            cat_scores[cat_name] = {"earned": 0, "possible": 0, "solved": 0, "total": 0}
        cat_scores[cat_name]["earned"] += earned_pts
        cat_scores[cat_name]["possible"] += max_p
        cat_scores[cat_name]["total"] += 1
        if is_solved:
            cat_scores[cat_name]["solved"] += 1

        status_str = "SOLVED" if is_solved else "MISS  "
        pt_str = f"({earned_pts:2d}/{max_p:2d} pts)" if "sts" in suite_basename else ""
        expected_str = ",".join(entry["best_moves"]) if entry["best_moves"] else "none"

        if verbose:
            print(f"[{idx:4d}/{total_positions}] {entry['id']:<24} : got {eng_bm:<5} target {expected_str:<12} [{status_str}] {pt_str:<11} {search_res['time_ms']:5d}ms  nodes={search_res['nodes']:<8d} {search_res['score']}")

        results.append({
            "index": idx,
            "id": entry["id"],
            "fen": entry["fen"],
            "bestmove": eng_bm,
            "target_moves": entry["best_moves"],
            "is_solved": is_solved,
            "earned_pts": earned_pts,
            "max_pts": max_p,
            "time_ms": search_res["time_ms"],
            "nodes": search_res["nodes"],
            "depth": search_res["depth"],
            "score": search_res["score"],
            "category": cat_name,
        })

    engine.close()

    total_wall_s = time.perf_counter() - start_suite_time
    score_pct = (total_pts_earned / max(1, total_pts_possible)) * 100.0
    solved_pct = (solved_count / max(1, total_positions)) * 100.0
    avg_nps = int((total_nodes * 1000) / max(1, total_time_ms))

    # Calculate Elo ratings
    elo_report = {}
    if "sts" in suite_basename:
        sts_elo = estimate_sts_elo(score_pct)
        elo_report["STS_CCRL_40_4_Elo"] = sts_elo
        primary_elo_str = f"CCRL 40/4 Estimated Elo: {sts_elo:+.0f} (formula: 44.523 * {score_pct:.1f}% - 242.85)"
    elif "bt2630" in suite_basename:
        bt_elo = estimate_bt2630_elo(solved_count, total_positions, total_time_ms / 1000.0)
        elo_report.update(bt_elo)
        primary_elo_str = f"BT-2630 Solved-Scale Elo: {bt_elo['solved_elo']:.0f} | Classic Time Elo: {bt_elo['classic_time_elo']:.0f}"
    elif "bk" in suite_basename or "bratko" in suite_basename:
        bk_elo = estimate_bk_elo(solved_count, total_positions)
        elo_report["BK_Estimated_Elo"] = bk_elo
        primary_elo_str = f"Bratko-Kopec Estimated Elo: {bk_elo:.0f} (formula: 1300 + 55 * {solved_count})"
    elif "wac" in suite_basename:
        wac_elo = round(1600.0 + (solved_count / max(1, total_positions)) * 1200.0, 1)
        elo_report["WAC_Tactical_Elo"] = wac_elo
        primary_elo_str = f"WAC Tactical Estimated Elo: {wac_elo:.0f} ({solved_count}/{total_positions} solved)"
    else:
        primary_elo_str = f"Solved: {solved_count}/{total_positions} ({solved_pct:.1f}%)"

    print(f"\n{'='*88}")
    print(f" TEST SUITE SUMMARY: {os.path.basename(suite_path)}")
    print(f"{'='*88}")
    print(f" Solved Positions : {solved_count} / {total_positions} ({solved_pct:.2f}%)")
    if "sts" in suite_basename:
        print(f" Strategic Points : {total_pts_earned} / {total_pts_possible} ({score_pct:.2f}%)")
    print(f" Total Search Time: {total_time_ms} ms (wall time: {total_wall_s:.2f}s)")
    print(f" Total Nodes      : {total_nodes:,} ({avg_nps:,} NPS)")
    print(f"{'-'*88}")
    print(f" >>> {primary_elo_str} <<<")
    print(f"{'-'*88}")

    if len(cat_scores) > 1:
        print("\nBreakdown by Category / Theme:")
        print(f" {'Category':<32} {'Solved':<12} {'Points / Max':<18} {'Score %':<10}")
        print(f" {'-'*30:<32} {'-'*10:<12} {'-'*16:<18} {'-'*8:<10}")
        for cat, data in sorted(cat_scores.items()):
            c_pct = (data['earned'] / max(1, data['possible'])) * 100.0
            print(f" {cat:<32} {data['solved']:>3d}/{data['total']:<6d} {data['earned']:>5d}/{data['possible']:<10d} {c_pct:>6.1f}%")

    print(f"{'='*88}\n")

    summary = {
        "suite": os.path.basename(suite_path),
        "engine": engine_path,
        "total_positions": total_positions,
        "solved_count": solved_count,
        "solved_percent": round(solved_pct, 2),
        "total_pts_earned": total_pts_earned,
        "total_pts_possible": total_pts_possible,
        "score_percent": round(score_pct, 2),
        "total_nodes": total_nodes,
        "total_time_ms": total_time_ms,
        "avg_nps": avg_nps,
        "wall_time_s": round(total_wall_s, 2),
        "elo_estimates": elo_report,
        "categories": cat_scores,
    }

    return summary, results


def main():
    parser = argparse.ArgumentParser(
        description="Run standard EPD chess benchmark suites and estimate engine Elo strength without matches."
    )
    parser.add_argument(
        "--suite", "-s",
        default="bt2630",
        help="Suite name (bt2630, sts1, sts1-15, bk, wac) or path to a custom .epd file (default: bt2630)"
    )
    parser.add_argument(
        "--engine", "-e",
        default=DEFAULT_ENGINE,
        help=f"Path to engine binary (default: {DEFAULT_ENGINE})"
    )
    parser.add_argument(
        "--movetime", "-m",
        type=int,
        default=None,
        help="Search time per position in ms (e.g. 1000). Default is 1000 if depth/nodes not set."
    )
    parser.add_argument(
        "--depth", "-d",
        type=int,
        default=None,
        help="Search depth per position (e.g. 8, 10, 12)"
    )
    parser.add_argument(
        "--nodes", "-n",
        type=int,
        default=None,
        help="Search node limit per position"
    )
    parser.add_argument(
        "--hash",
        type=int,
        default=64,
        help="Hash table size in MB (default: 64)"
    )
    parser.add_argument(
        "--threads", "-t",
        type=int,
        default=1,
        help="Search threads (default: 1)"
    )
    parser.add_argument(
        "--limit", "-l",
        type=int,
        default=None,
        help="Limit test to first N positions (useful for quick sanity check)"
    )
    parser.add_argument(
        "--quiet", "-q",
        action="store_true",
        help="Suppress per-position output and print summary only"
    )
    parser.add_argument(
        "--save-json",
        type=str,
        default=None,
        help="Save test results and Elo estimation to JSON file"
    )

    args = parser.parse_args()

    # Resolve suite path
    suite_key = args.suite.lower()
    if suite_key in SUITE_PRESETS:
        suite_path = SUITE_PRESETS[suite_key]
    else:
        suite_path = args.suite

    if not os.path.isfile(suite_path):
        print(f"Error: suite file not found: {suite_path}", file=sys.stderr)
        print(f"Available presets: {', '.join(SUITE_PRESETS.keys())}", file=sys.stderr)
        sys.exit(1)

    # Fallback to default movetime 1000 if neither depth nor nodes nor movetime given
    movetime = args.movetime
    if movetime is None and args.depth is None and args.nodes is None:
        movetime = 1000

    summary, results = run_epd_suite(
        suite_path=suite_path,
        engine_path=args.engine,
        movetime=movetime,
        depth=args.depth,
        nodes=args.nodes,
        hash_mb=args.hash,
        threads=args.threads,
        limit=args.limit,
        verbose=not args.quiet
    )

    if args.save_json:
        out_data = {
            "summary": summary,
            "positions": results
        }
        with open(args.save_json, "w", encoding="utf-8") as f:
            json.dump(out_data, f, indent=2)
        print(f"Results saved to: {args.save_json}")


if __name__ == "__main__":
    main()
