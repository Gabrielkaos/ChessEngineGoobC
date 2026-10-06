#!/usr/bin/env python3
"""
GOOB Chess Engine - Search Heuristic Trace & Diagnostics Tool
============================================================
Tracks, inspects, and diagnoses search heuristics to detect evaluation-search
misalignments (especially after NNUE transitions).

Usage Examples:
  # 1. Run the built-in 30-position benchmark at depth 8:
  python3 tools/trace_search.py --bench 8

  # 2. Run benchmark and save JSON report:
  python3 tools/trace_search.py --bench 8 --save-json trace_baseline.json

  # 3. Test a custom FEN file or single FEN:
  python3 tools/trace_search.py --fens test_suite.epd --depth 7
  python3 tools/trace_search.py --fen "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4" --depth 9

  # 4. Compare two trace runs side-by-side (e.g. before & after tuning constants in search.h):
  python3 tools/trace_search.py --compare trace_baseline.json trace_tuned.json
"""

import sys
import os
import subprocess
import argparse
import json
import time
from typing import Dict, Any, Optional, List, Tuple

# ANSI Colors
class Colors:
    RESET   = "\033[0m"
    BOLD    = "\033[1m"
    DIM     = "\033[2m"
    RED     = "\033[31m"
    GREEN   = "\033[32m"
    YELLOW  = "\033[33m"
    BLUE    = "\033[34m"
    MAGENTA = "\033[35m"
    CYAN    = "\033[36m"
    WHITE   = "\033[37m"
    BG_RED  = "\033[41m"
    BG_GRN  = "\033[42m"
    BG_YEL  = "\033[43m"

USE_COLOR = True

def c(color: str, text: str) -> str:
    return f"{color}{text}{Colors.RESET}" if USE_COLOR else text

# Heuristic Diagnostic Rules
# (key, display_name, min_expected_rate, max_expected_rate, governing_constant, location, nnue_note)
HEURISTIC_RULES = [

    {
        "key": "beta_pruning",
        "name": "Beta Pruning (RFP)",
        "sub_key": "rate",
        "min_pct": 20.0,
        "max_pct": 45.0,
        "constants": "BetaMargin(75), BetaPruningDepth(8)",
        "file": "src/search.h:51-52",
        "advice_low": (
            "RFP trigger rate is low. NNUE score scaling might be tighter than HCE. "
            "Consider lowering BetaMargin (currently 75) or testing dynamic margins."
        ),
        "advice_high": (
            "RFP trigger rate is unusually high (>45%). It may be cutting too aggressively. "
            "Consider increasing BetaMargin (currently 75) to avoid tactical blindness."
        )
    },
    {
        "key": "nmp",
        "name": "Null Move Pruning",
        "sub_key": "rate",
        "min_pct": 45.0,
        "max_pct": 75.0,
        "constants": "defaultNullMoveDepth(2), NMPVerifyDepth(16)",
        "file": "src/search.h:40,67",
        "advice_low": "NMP cutoff rate is low. Check verification conditions or static eval scaling.",
        "advice_high": "NMP cutoff rate is very high (>75%). Ensure zugzwang and tactical safety checks are intact."
    },
    {
        "key": "razoring",
        "name": "Razoring",
        "sub_key": "rate",
        "min_pct": 1.5,
        "max_pct": 10.0,
        "constants": "RazoringDepth(2), RazorMarginBase(240), RazorMarginCoeff(160)",
        "file": "src/search.h:55-57",
        "advice_low": (
            "Razoring trigger rate is <1.5%. The margin is RazorMarginBase + RazorMarginCoeff * depth^2: "
            "400 cp at depth 1 and 880 cp at depth 2. Smaller values razor more often."
        ),
        "advice_high": "Razoring rate is very high. Ensure it is not razoring tactical positions."
    },
    {
        "key": "probcut",
        "name": "ProbCut",
        "sub_key": "rate",
        "min_pct": 15.0,
        "max_pct": 45.0,
        "constants": "probCutDepth(5), probCutMargin(80)",
        "file": "src/search.h:27,29",
        "advice_low": (
            "ProbCut cutoff rate is low or 0. If search depth < 5, ProbCut never activates. "
            "If searching depth >= 8 and rate is low, probCutMargin(80) may need adjustment for NNUE scale."
        ),
        "advice_high": "ProbCut is cutting off very frequently (>45%). Verify that tactical sacrifices are not missed."
    },
    {
        "key": "futility_skip",
        "name": "Futility (skipQuiets)",
        "sub_key": "rate",
        "min_pct": 3.0,
        "max_pct": 20.0,
        "constants": "FutilityMargin(65), FutilityMarginNoHistory(110), Depth(8)",
        "file": "src/search.h:32-34",
        "advice_low": (
            "Futility skipQuiets trigger rate is very low. It fires when eval + 65 * lmrDepth + 110 <= alpha "
            "(FutilityMargin, FutilityMarginNoHistory); smaller margins skip quiets more often."
        ),
        "advice_high": "Futility skipQuiets is skipping too often (>20%). Watch for tactical blunders."
    },
    {
        "key": "futility_move",
        "name": "Futility (per-move)",
        "sub_key": "rate",
        "min_pct": 45.0,
        "max_pct": 75.0,
        "constants": "FutilityMargin(65), FutilityPruningHistoryLimit",
        "file": "src/search.h:32,35",
        "advice_low": "Per-move futility pruning is low; check FutilityPruningHistoryLimit bounds.",
        "advice_high": "Per-move futility pruning is very high (>75%). It may be discarding subtle quiet winning moves."
    },
    {
        "key": "lmp",
        "name": "Late Move Pruning",
        "sub_key": "rate",
        "min_pct": 8.0,
        "max_pct": 30.0,
        "constants": "LateMovePruningDepth(8), LateMovePruningCounts table",
        "file": "src/search.h:41-45",
        "advice_low": "LMP rate is low. Check if counts in LateMovePruningCounts table are too generous.",
        "advice_high": "LMP rate is >30%. Too many quiet moves are pruned early; verify count table."
    },
    {
        "key": "countermove_prune",
        "name": "CounterMove Pruning",
        "sub_key": "rate",
        "min_pct": 2.0,
        "max_pct": 15.0,
        "constants": "CounterMovePruningDepth[3,2], CounterMoveHistoryLimit[0, -1000]",
        "file": "src/search.h:36-37",
        "advice_low": "CounterMove pruning rate is very low. Adjust CounterMoveHistoryLimit.",
        "advice_high": "CounterMove pruning rate is high (>15%)."
    },
    {
        "key": "followup_prune",
        "name": "FollowUpMove Pruning",
        "sub_key": "rate",
        "min_pct": 1.0,
        "max_pct": 10.0,
        "constants": "FollowUpMovePruningDepth[3,2], FollowUpMoveHistoryLimit[-500, -1500]",
        "file": "src/search.h:38-39",
        "advice_low": (
            "FollowUpMove pruning rarely triggers. It prunes when the follow-up history is below "
            "FollowUpMoveHistoryLimit (-500 / -1500); limits closer to 0 prune more."
        ),
        "advice_high": "FollowUpMove pruning is high (>10%)."
    },
    {
        "key": "see_quiet",
        "name": "SEE Quiet Pruning",
        "sub_key": "rate",
        "min_pct": 5.0,
        "max_pct": 30.0,
        "constants": "SEEPruningDepth(9), SEEQuietMargin(-64)",
        "file": "src/search.h:13-14",
        "advice_low": "SEE quiet pruning rate is low. Check SEEQuietMargin scaling.",
        "advice_high": "SEE quiet pruning rate is high (>30%)."
    },
    {
        "key": "see_noisy",
        "name": "SEE Noisy Pruning",
        "sub_key": "rate",
        "min_pct": 65.0,
        "max_pct": 95.0,
        "constants": "SEEPruningDepth(9), SEENoisyMargin(-19)",
        "file": "src/search.h:13,15",
        "advice_low": "SEE noisy pruning is low. Good noisy moves might be getting caught.",
        "advice_high": (
            "SEE noisy pruning is triggering on >95% of attempts. Note that good noisy moves "
            "are already exempt in STAGE_GOOD_NOISY, so this primarily prunes STAGE_BAD_NOISY moves."
        )
    },
    {
        "key": "lmr_quiet",
        "name": "Quiet LMR",
        "sub_key": "rate",
        "min_pct": 65.0,
        "max_pct": 90.0,
        "constants": "LMRTable, AllNodeScale(276), AllNodeBase(268)",
        "file": "src/search.h:62-63",
        "advice_low": "Quiet LMR rate is low (<65%). Search will be slow and branchy.",
        "advice_high": "Quiet LMR rate is >90%. Very aggressive reductions; watch for search instability."
    },
    {
        "key": "lmr_researches",
        "name": "LMR Re-searches",
        "sub_key": "rate",
        "min_pct": 1.5,
        "max_pct": 6.0,
        "constants": "LMRTable reductions",
        "file": "src/search.c:727-827",
        "advice_low": "LMR re-search rate is <1.5%. LMR might be slightly too timid (could reduce more).",
        "advice_high": "LMR re-search rate is >6%. Reductions are too severe, causing costly re-searches."
    },
    {
        "key": "qs.delta",
        "name": "QS Delta Pruning",
        "sub_key": "delta_rate",
        "min_pct": 1.5,
        "max_pct": 15.0,
        "constants": "DeltaMarginQ(110)",
        "file": "src/search.h:18",
        "advice_low": (
            "QS Delta pruning rate is <1.5%. DeltaMarginQ is 110; a smaller margin prunes more."
        ),
        "advice_high": "QS Delta pruning rate is high (>15%). Ensure queen promotions and sacrifices aren't pruned."
    },
    {
        "key": "qs.see",
        "name": "QS SEE Pruning",
        "sub_key": "see_rate",
        "min_pct": 40.0,
        "max_pct": 80.0,
        "constants": "QSSeeMargin(110)",
        "file": "src/search.h:20",
        "advice_low": "QS SEE pruning rate is low (<40%).",
        "advice_high": "QS SEE pruning rate is very high (>80%). Check QSSeeMargin threshold."
    },
]


def resolve_engine_path(user_path: Optional[str] = None) -> str:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir = os.path.abspath(os.path.join(script_dir, ".."))

    if user_path:
        if os.path.isfile(user_path):
            return os.path.abspath(user_path)
        cand = os.path.join(root_dir, user_path)
        if os.path.isfile(cand):
            return cand

    candidates = [
        os.path.join(root_dir, "src", "bin", "linux", "GOOB-2.2-BETA-native-trace"),
        os.path.join(root_dir, "src", "bin", "linux", "GOOB-2.2-BETA-native"),
        os.path.join(root_dir, "src", "bin", "windows", "GOOB-2.2-BETA-native-trace.exe"),
        os.path.join(root_dir, "src", "bin", "windows", "GOOB-2.2-BETA-native.exe"),
        os.path.join(root_dir, "src", "bin", "linux", "GOOB-2.2-BETA-universal-trace"),
        os.path.join(root_dir, "src", "bin", "linux", "GOOB-2.2-BETA-universal"),
        os.path.join(root_dir, "src", "bin", "windows", "GOOB-2.2-BETA-universal.exe"),
        os.path.join(root_dir, "bin", "linux", "GOOB-2.2-BETA-native-trace"),
        os.path.join(root_dir, "bin", "linux", "GOOB-2.2-BETA-native"),
    ]

    for cand in candidates:
        if os.path.isfile(cand):
            return cand

    return os.path.join(root_dir, "src", "bin", "linux", "GOOB-2.2-BETA-native")


class EngineRunner:
    def __init__(self, engine_path: Optional[str] = None, verbose: bool = False):
        self.engine_path = resolve_engine_path(engine_path)
        self.verbose = verbose
        self.process: Optional[subprocess.Popen] = None

    def start(self):
        if not os.path.isfile(self.engine_path):
            raise FileNotFoundError(
                f"Engine binary not found at '{self.engine_path}'.\n"
                f"Please compile the engine with TRACE enabled first:\n"
                f"  make trace             (from project root)\n"
                f"or:\n"
                f"  make TRACE=1 native    (from src/)"
            )
        if not os.access(self.engine_path, os.X_OK):
            raise PermissionError(f"Engine binary '{self.engine_path}' is not executable.")

        self.process = subprocess.Popen(
            [self.engine_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1
        )

        # Initialize UCI
        self.send("uci")
        while True:
            line = self.readline()
            if "uciok" in line:
                break
        self.send("isready")
        while True:
            line = self.readline()
            if "readyok" in line:
                break

    def send(self, command: str):
        if self.process and self.process.stdin:
            self.process.stdin.write(command + "\n")
            self.process.stdin.flush()

    def readline(self) -> str:
        if self.process and self.process.stdout:
            line = self.process.stdout.readline()
            if self.verbose and line.strip():
                print(c(Colors.DIM, f"[ENGINE] {line.strip()}"))
            return line
        return ""

    def stop(self):
        if self.process:
            try:
                self.send("quit")
                self.process.communicate(timeout=2)
            except Exception:
                self.process.kill()
            self.process = None

    def run_bench(self, depth: int) -> Tuple[Dict[str, Any], str]:
        """Runs built-in benchmark, prints progress, returns trace JSON and bench summary."""
        self.send(f"trace bench {depth}")
        bench_summary_line = ""
        seen_cutoffs = False
        while True:
            line = self.readline()
            if not line:
                break
            stripped = line.strip()
            if stripped.startswith("Position ["):
                print(c(Colors.CYAN, f"  -> {stripped}"))
            elif stripped.startswith("Benchmark Summary:"):
                bench_summary_line = stripped
                print(c(Colors.BOLD + Colors.GREEN, f"\n{stripped}\n"))
            elif "CUTOFF DISTRIBUTION" in stripped:
                seen_cutoffs = True
            elif seen_cutoffs and stripped.startswith("======="):
                break

        # Extract JSON
        json_data = self.get_trace_json()
        return json_data, bench_summary_line

    def get_trace_json(self) -> Dict[str, Any]:
        """Requests 'trace json' and parses response."""
        self.send("trace json")
        json_lines = []
        capturing = False
        brace_count = 0
        timeout = 5.0
        start_time = time.time()

        while time.time() - start_time < timeout:
            line = self.readline()
            if not line:
                continue
            stripped = line.strip()
            if not capturing and stripped.startswith("{"):
                capturing = True
            if capturing:
                json_lines.append(stripped)
                brace_count += stripped.count("{") - stripped.count("}")
                if brace_count == 0:
                    break

        raw_json = "\n".join(json_lines)
        try:
            data = json.loads(raw_json)
            if not data.get("trace_enabled", False):
                raise RuntimeError(
                    "Engine returned 'trace_enabled: false'.\n"
                    "The engine binary was compiled WITHOUT TRACE instrumentation (e.g., 'make native' was run).\n"
                    "To enable search heuristic tracking, recompile with:\n"
                    "  make trace             (from project root)\n"
                    "or:\n"
                    "  make TRACE=1 native    (from src/)"
                )
            return data
        except json.JSONDecodeError as e:
            raise RuntimeError(f"Failed to parse trace JSON from engine: {e}\nRaw output was:\n{raw_json}")

    def run_fens(self, fens: List[str], depth: Optional[int], movetime: Optional[int],
                 threads: int, hash_mb: int) -> Dict[str, Any]:
        """Searches a list of FENs and gathers cumulative trace statistics."""
        self.send(f"setoption name Threads value {threads}")
        self.send(f"setoption name Hash value {hash_mb}")
        self.send("isready")
        while "readyok" not in self.readline():
            pass

        self.send("trace reset")

        total = len(fens)
        t_start = time.time()
        for idx, fen in enumerate(fens, 1):
            fen = fen.strip()
            if not fen or fen.startswith("#"):
                continue
            # Handle EPD format with opcode
            fen_clean = fen.split(";")[0].strip()

            self.send(f"position fen {fen_clean}")
            if depth:
                self.send(f"go depth {depth}")
            elif movetime:
                self.send(f"go movetime {movetime}")
            else:
                self.send("go depth 8")

            while True:
                line = self.readline()
                if line.startswith("bestmove"):
                    break

            if idx % 5 == 0 or idx == total:
                print(c(Colors.CYAN, f"  -> Searched {idx}/{total} positions..."))

        elapsed = time.time() - t_start
        print(c(Colors.GREEN, f"\nCompleted search on {total} positions in {elapsed:.2f}s.\n"))
        return self.get_trace_json()


def format_rate(rate_val: float) -> str:
    pct = rate_val * 100.0 if rate_val <= 1.0 else rate_val
    return f"{pct:6.2f}%"

def get_status_badge(rate: float, min_val: float, max_val: float) -> Tuple[str, str]:
    """Returns (status_text, color)"""
    if rate < 0.05:
        return "DEAD / 0%", Colors.RED + Colors.BOLD
    if rate < min_val:
        return "LOW", Colors.YELLOW
    if rate > max_val:
        return "HIGH", Colors.MAGENTA
    return "HEALTHY", Colors.GREEN


def print_trace_report(data: Dict[str, Any], title: str = "SEARCH HEURISTIC TRACE REPORT"):
    h = data.get("heuristics", {})
    c_data = data.get("cutoffs", {})

    print(c(Colors.BOLD + Colors.CYAN, "=" * 90))
    print(c(Colors.BOLD + Colors.CYAN, f"                  {title}"))
    print(c(Colors.BOLD + Colors.CYAN, "=" * 90))

    ab_nodes = data.get("ab_nodes", 0)
    qs_nodes = data.get("qs_nodes", 0)
    tot_nodes = data.get("total_nodes", ab_nodes + qs_nodes)
    eval_calls = data.get("eval_calls", 0)
    tt_hits = data.get("tt_hits", 0)
    tt_cutoffs = data.get("tt_cutoffs", 0)
    searches = data.get("searches_count", 1)

    print(f"  Searches: {searches:,} | AB Nodes: {ab_nodes:,} | QS Nodes: {qs_nodes:,} | Total: {tot_nodes:,}")
    tt_hit_pct = (tt_hits * 100.0 / ab_nodes) if ab_nodes else 0.0
    tt_cut_pct = (tt_cutoffs * 100.0 / tt_hits) if tt_hits else 0.0
    print(f"  Static Evals: {eval_calls:,} | TT Hits: {tt_hits:,} ({tt_hit_pct:.1f}%) | TT Cutoffs: {tt_cutoffs:,} ({tt_cut_pct:.1f}%)")

    # Table Header
    print(c(Colors.BOLD, "\n" + "-" * 90))
    print(c(Colors.BOLD, f"  {'Heuristic':<24} {'Considered':>12} {'Triggered':>12} {'Rate':>9}   {'Status':<10} {'Governing Constants':<20}"))
    print(c(Colors.BOLD, "-" * 90))

    def print_row(name: str, considered: int, triggered: int, rate_pct: float,
                  min_exp: float, max_exp: float, constants: str):
        status, color = get_status_badge(rate_pct, min_exp, max_exp)
        cons_str = f"{considered:,}" if considered > 0 else "-"
        trig_str = f"{triggered:,}"
        rate_str = f"{rate_pct:6.2f}%"
        print(f"  {name:<24} {cons_str:>12} {trig_str:>12} {rate_str:>9}   {c(color, f'[{status:<7}]')} {constants}")

    # 1. Interior Pruning
    print(c(Colors.BLUE + Colors.BOLD, "\n  [INTERIOR-NODE PRUNING & REDUCTIONS]"))
    bp = h.get("beta_pruning", {})
    print_row("Beta Pruning (RFP)", bp.get("considered", 0), bp.get("triggered", 0), bp.get("rate", 0)*100, 20, 45, "BetaMargin(75), Depth(8)")


    nmp = h.get("nmp", {})
    nmp_rate = nmp.get("rate", 0) * 100
    nmp_trig = nmp.get("direct_cutoffs", 0) + nmp.get("verification_passed", 0)
    print_row("Null Move Pruning", nmp.get("attempted", 0), nmp_trig, nmp_rate, 45, 75, "defaultNullMoveDepth(2)")

    rz = h.get("razoring", {})
    print_row("Razoring", rz.get("attempted", 0), rz.get("cutoffs", 0), rz.get("rate", 0)*100, 1.5, 10, "Base(240), Coeff(160)")

    pc = h.get("probcut", {})
    print_row("ProbCut", pc.get("attempted", 0), pc.get("cutoffs", 0), pc.get("rate", 0)*100, 15, 45, "Margin(80), Depth(5)")

    iir = h.get("iir", {})
    print_row("IIR Reductions", iir.get("attempted", 0), iir.get("reduced", 0), iir.get("rate", 0)*100, 2, 10, "IIRDepth(6)")

    # 2. Move Loop Pruning
    print(c(Colors.BLUE + Colors.BOLD, "\n  [MOVE LOOP PRUNING]"))
    f_skip = h.get("futility_skip", {})
    print_row("Futility (skipQuiets)", f_skip.get("considered", 0), f_skip.get("triggered", 0), f_skip.get("rate", 0)*100, 3, 20, "Margin(65), NoHist(110)")

    f_move = h.get("futility_move", {})
    print_row("Futility (per-move)", f_move.get("considered", 0), f_move.get("triggered", 0), f_move.get("rate", 0)*100, 45, 75, "Margin(65), HistLimits")

    lmp = h.get("lmp", {})
    print_row("Late Move Pruning", lmp.get("considered", 0), lmp.get("triggered", 0), lmp.get("rate", 0)*100, 8, 30, "LMPDepth(8), CountTable")

    cmp = h.get("countermove_prune", {})
    print_row("CounterMove Pruning", cmp.get("considered", 0), cmp.get("triggered", 0), cmp.get("rate", 0)*100, 2, 15, "Depth[3,2], Limit[0,-1000]")

    fup = h.get("followup_prune", {})
    print_row("FollowUpMove Pruning", fup.get("considered", 0), fup.get("triggered", 0), fup.get("rate", 0)*100, 1, 10, "Depth[3,2], Limit[-500,-1500]")

    see_q = h.get("see_quiet", {})
    print_row("SEE Quiet Pruning", see_q.get("considered", 0), see_q.get("triggered", 0), see_q.get("rate", 0)*100, 5, 30, "SEEQuietMargin(-64)")

    see_n = h.get("see_noisy", {})
    print_row("SEE Noisy Pruning", see_n.get("considered", 0), see_n.get("triggered", 0), see_n.get("rate", 0)*100, 65, 95, "SEENoisyMargin(-19)")

    # 3. Extensions & Reductions
    print(c(Colors.BLUE + Colors.BOLD, "\n  [EXTENSIONS & REDUCTIONS]"))
    sing = h.get("singular", {})
    s_att = sing.get("attempted", 0)
    s_ext = sing.get("single_ext", 0) + sing.get("double_ext", 0)
    s_rate = (s_ext * 100.0 / s_att) if s_att else 0.0
    print_row("Singular Extensions", s_att, s_ext, s_rate, 40, 85, "DoubleExtMargin(120)")

    lmr_q = h.get("lmr_quiet", {})
    print_row("Quiet LMR", lmr_q.get("considered", 0), lmr_q.get("reduced", 0), lmr_q.get("rate", 0)*100, 65, 90, "LMRTable, AllNodeScale")

    lmr_n = h.get("lmr_noisy", {})
    print_row("Noisy LMR", lmr_n.get("considered", 0), lmr_n.get("reduced", 0), lmr_n.get("rate", 0)*100, 50, 75, "LMRTable")

    lmr_res = h.get("lmr_researches", {})
    print_row("LMR Re-searches", lmr_res.get("reduced_searches", 0), lmr_res.get("researched", 0), lmr_res.get("rate", 0)*100, 1.5, 6.0, "Fail-high re-searches")

    # 4. Quiescence Search
    print(c(Colors.BLUE + Colors.BOLD, "\n  [QUIESCENCE SEARCH]"))
    qs = h.get("qs", {})
    sp_evals = qs.get("stand_pat_evals", 0)
    sp_cuts = qs.get("stand_pat_cutoffs", 0)
    sp_rate = (sp_cuts * 100.0 / sp_evals) if sp_evals else 0.0
    print_row("QS Stand-pat Cutoff", sp_evals, sp_cuts, sp_rate, 35, 60, "eval >= beta")

    delta_att = qs.get("delta_attempted", 0)
    delta_prun = qs.get("delta_pruned", 0)
    delta_rate = (delta_prun * 100.0 / delta_att) if delta_att else 0.0
    print_row("QS Delta Pruning", delta_att, delta_prun, delta_rate, 1.5, 15, "DeltaMarginQ(110)")

    qs_see_att = qs.get("see_attempted", 0)
    qs_see_prun = qs.get("see_pruned", 0)
    qs_see_rate = (qs_see_prun * 100.0 / qs_see_att) if qs_see_att else 0.0
    print_row("QS SEE Pruning", qs_see_att, qs_see_prun, qs_see_rate, 40, 80, "QSSeeMargin(110)")

    # 5. Move Ordering & Cutoff Distribution
    tot_cuts = c_data.get("total", 0)
    print(c(Colors.BLUE + Colors.BOLD, "\n  [MOVE ORDERING & CUTOFF DISTRIBUTION]"))
    if tot_cuts > 0:
        c1st = c_data.get("first_move", 0)
        c1st_pct = c1st * 100.0 / tot_cuts
        c_tt = c_data.get("tt_move", 0)
        c_tt_pct = c_tt * 100.0 / tot_cuts
        c_kill = c_data.get("killer", 0)
        c_kill_pct = c_kill * 100.0 / tot_cuts
        c_cnt = c_data.get("counter", 0)
        c_cnt_pct = c_cnt * 100.0 / tot_cuts
        c_flw = c_data.get("followup", 0)
        c_flw_pct = c_flw * 100.0 / tot_cuts
        c_qui = c_data.get("quiet", 0)
        c_qui_pct = c_qui * 100.0 / tot_cuts
        c_nos = c_data.get("noisy", 0)
        c_nos_pct = c_nos * 100.0 / tot_cuts

        badge1, col1 = ("EXCELLENT", Colors.GREEN) if c1st_pct >= 85 else (("HEALTHY", Colors.CYAN) if c1st_pct >= 75 else ("POOR", Colors.RED))
        print(f"  Total Beta Cutoffs (Fail-Highs): {tot_cuts:,}")
        print(f"    1st Move Cutoff Rate:     {c(col1, f'{c1st_pct:6.2f}%')}  [{badge1}] (Target: >80%)")
        print(f"    TT Move Cutoffs:          {c_tt_pct:6.2f}% ({c_tt:,})")
        print(f"    Killer Move Cutoffs:      {c_kill_pct:6.2f}% ({c_kill:,})")
        print(f"    Counter Move Cutoffs:     {c_cnt_pct:6.2f}% ({c_cnt:,})")
        print(f"    Followup Move Cutoffs:    {c_flw_pct:6.2f}% ({c_flw:,})")
        print(f"    Other Quiet Cutoffs:      {c_qui_pct:6.2f}% ({c_qui:,})")
        print(f"    Tactical/Noisy Cutoffs:   {c_nos_pct:6.2f}% ({c_nos:,})")

    # Diagnostics & Hand-Tuning Advice
    print_diagnostics(data)


def print_diagnostics(data: Dict[str, Any]):
    """Analyzes heuristics against known NNUE tuning challenges and prints actionable guidance."""
    h = data.get("heuristics", {})
    qs = h.get("qs", {})

    warnings: List[Dict[str, Any]] = []

    for rule in HEURISTIC_RULES:
        key = rule["key"]
        sub_key = rule["sub_key"]
        min_p = rule["min_pct"]
        max_p = rule["max_pct"]

        rate = 0.0
        if "." in key:
            parent, child = key.split(".")
            parent_dict = h.get(parent, {})
            if child == "delta":
                att = parent_dict.get("delta_attempted", 0)
                prun = parent_dict.get("delta_pruned", 0)
                rate = (prun * 100.0 / att) if att else 0.0
            elif child == "see":
                att = parent_dict.get("see_attempted", 0)
                prun = parent_dict.get("see_pruned", 0)
                rate = (prun * 100.0 / att) if att else 0.0
        else:
            obj = h.get(key, {})
            rate_raw = obj.get(sub_key, 0.0)
            rate = rate_raw * 100.0 if rate_raw <= 1.0 else rate_raw

        if rate < 0.05:
            warnings.append({
                "severity": "CRITICAL / DEAD",
                "color": Colors.RED + Colors.BOLD,
                "name": rule["name"],
                "rate": rate,
                "constants": rule["constants"],
                "file": rule["file"],
                "message": rule["advice_low"]
            })
        elif rate < min_p:
            warnings.append({
                "severity": "UNDER-TRIGGERING",
                "color": Colors.YELLOW,
                "name": rule["name"],
                "rate": rate,
                "constants": rule["constants"],
                "file": rule["file"],
                "message": rule["advice_low"]
            })
        elif rate > max_p:
            warnings.append({
                "severity": "POTENTIAL OVER-PRUNING",
                "color": Colors.MAGENTA,
                "name": rule["name"],
                "rate": rate,
                "constants": rule["constants"],
                "file": rule["file"],
                "message": rule["advice_high"]
            })

    print(c(Colors.BOLD + Colors.CYAN, "\n" + "=" * 90))
    print(c(Colors.BOLD + Colors.CYAN, "             DIAGNOSTICS & NNUE CONSTANTS TUNING ADVICE"))
    print(c(Colors.BOLD + Colors.CYAN, "=" * 90))

    if not warnings:
        print(c(Colors.GREEN + Colors.BOLD, "\n  [ALL HEURISTICS HEALTHY] No critical misalignments detected!"))
        print("=" * 90 + "\n")
        return

    print(f"\nFound {len(warnings)} heuristic(s) with potential NNUE evaluation misalignment:\n")

    for idx, w in enumerate(warnings, 1):
        sev_tag = c(w["color"], f"[{w['severity']}]")
        print(f"  {idx}. {sev_tag} {c(Colors.BOLD, w['name'])} (Trigger Rate: {w['rate']:.2f}%)")
        print(f"     Constants: {c(Colors.CYAN, w['constants'])} in {c(Colors.WHITE, w['file'])}")
        print(f"     Analysis:  {w['message']}")
        print()

    print(c(Colors.DIM, "Tip: To adjust any heuristic, edit the constant in src/search.h and recompile with 'make trace'."))
    print("=" * 90 + "\n")


def compare_traces(file1: str, file2: str):
    """Loads two JSON trace files and prints a side-by-side comparison table."""
    with open(file1, "r") as f:
        d1 = json.load(f)
    with open(file2, "r") as f:
        d2 = json.load(f)

    h1 = d1.get("heuristics", {})
    h2 = d2.get("heuristics", {})

    print(c(Colors.BOLD + Colors.CYAN, "\n" + "=" * 95))
    print(c(Colors.BOLD + Colors.CYAN, f"                   SEARCH TRACE COMPARISON"))
    print(c(Colors.BOLD + Colors.CYAN, f"   Run 1: {os.path.basename(file1)}  vs  Run 2: {os.path.basename(file2)}"))
    print(c(Colors.BOLD + Colors.CYAN, "=" * 95))

    n1, n2 = d1.get("total_nodes", 0), d2.get("total_nodes", 0)
    node_delta = ((n2 - n1) * 100.0 / n1) if n1 else 0.0
    col_n = Colors.GREEN if node_delta < 0 else Colors.YELLOW
    print(f"  Total Nodes:  Run 1: {n1:,}   Run 2: {n2:,}   Delta: {c(col_n, f'{node_delta:+.2f}%')}")

    print(c(Colors.BOLD, "\n  " + "-" * 91))
    print(c(Colors.BOLD, f"  {'Heuristic':<26} {'Run 1 Rate':>12} {'Run 2 Rate':>12} {'Diff':>10}   {'Status':<16}"))
    print(c(Colors.BOLD, "  " + "-" * 91))

    comparisons = [
        ("Beta Pruning (RFP)", "beta_pruning", "rate"),
        ("Null Move Pruning", "nmp", "rate"),
        ("Razoring", "razoring", "rate"),
        ("ProbCut", "probcut", "rate"),
        ("IIR Reductions", "iir", "rate"),
        ("Futility (skipQuiets)", "futility_skip", "rate"),
        ("Futility (per-move)", "futility_move", "rate"),
        ("Late Move Pruning", "lmp", "rate"),
        ("CounterMove Pruning", "countermove_prune", "rate"),
        ("FollowUpMove Pruning", "followup_prune", "rate"),
        ("SEE Quiet Pruning", "see_quiet", "rate"),
        ("SEE Noisy Pruning", "see_noisy", "rate"),
        ("Quiet LMR", "lmr_quiet", "rate"),
        ("Noisy LMR", "lmr_noisy", "rate"),
        ("LMR Re-searches", "lmr_researches", "rate"),
    ]

    for name, key, subkey in comparisons:
        r1 = h1.get(key, {}).get(subkey, 0.0) * 100.0
        r2 = h2.get(key, {}).get(subkey, 0.0) * 100.0
        diff = r2 - r1
        diff_str = f"{diff:+6.2f}%"

        if abs(diff) < 0.2:
            col_d = Colors.DIM
            status = "UNCHANGED"
        elif diff > 0:
            col_d = Colors.CYAN
            status = "INCREASED"
        else:
            col_d = Colors.YELLOW
            status = "DECREASED"

        # Highlight if it fixed a dead heuristic (0% -> >0.5%)
        if r1 < 0.05 and r2 >= 0.5:
            col_d = Colors.GREEN + Colors.BOLD
            status = "ACTIVATED!"

        print(f"  {name:<26} {r1:11.2f}% {r2:11.2f}% {c(col_d, f'{diff_str:>10}')}   {c(col_d, status)}")

    # Move Ordering 1st move
    c1 = d1.get("cutoffs", {})
    c2 = d2.get("cutoffs", {})
    tot1, tot2 = c1.get("total", 0), c2.get("total", 0)
    if tot1 > 0 and tot2 > 0:
        mo1 = c1.get("first_move", 0) * 100.0 / tot1
        mo2 = c2.get("first_move", 0) * 100.0 / tot2
        mo_diff = mo2 - mo1
        col_mo = Colors.GREEN if mo_diff >= 0 else Colors.RED
        print(c(Colors.BOLD, "  " + "-" * 91))
        print(f"  {'1st Move Cutoff Rate':<26} {mo1:11.2f}% {mo2:11.2f}% {c(col_mo, f'{mo_diff:+10.2f}%')}   {c(col_mo, 'MOVE ORDERING')}")

    print("=" * 95 + "\n")


def main():
    parser = argparse.ArgumentParser(
        description="GOOB Chess Engine - Search Heuristic Trace & Diagnostics Tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )

    parser.add_argument("--engine", default=None,
                        help="Path to GOOB binary compiled with 'make trace' (default: auto-detected in src/bin/)")
    parser.add_argument("--bench", nargs="?", const=8, type=int,
                        help="Run standard 30-position benchmark at given depth (default: 8)")
    parser.add_argument("--fens", type=str,
                        help="Path to .epd or .fen file containing positions to search")
    parser.add_argument("--fen", type=str,
                        help="Single FEN string to search")
    parser.add_argument("--depth", type=int, default=8,
                        help="Search depth for --fens or --fen (default: 8)")
    parser.add_argument("--movetime", type=int,
                        help="Search time in milliseconds per move (alternative to --depth)")
    parser.add_argument("--threads", type=int, default=1,
                        help="Number of search threads (default: 1)")
    parser.add_argument("--hash", type=int, default=64,
                        help="Transposition table hash size in MB (default: 64)")
    parser.add_argument("--save-json", type=str,
                        help="Save trace statistics to a JSON file")
    parser.add_argument("--compare", nargs=2, metavar=("FILE1", "FILE2"),
                        help="Compare two saved JSON trace reports side-by-side")
    parser.add_argument("--no-color", action="store_true",
                        help="Disable ANSI colors in terminal output")
    parser.add_argument("--verbose", action="store_true",
                        help="Print raw engine output lines")

    args = parser.parse_args()

    global USE_COLOR
    if args.no_color or not sys.stdout.isatty():
        USE_COLOR = False

    # Handle comparison mode first
    if args.compare:
        compare_traces(args.compare[0], args.compare[1])
        return

    # If neither bench nor fen(s) is specified, default to --bench 8
    if not args.bench and not args.fens and not args.fen:
        args.bench = 8

    runner = EngineRunner(args.engine, verbose=args.verbose)
    try:
        runner.start()

        if args.bench is not None:
            depth = args.bench
            print(c(Colors.BOLD + Colors.CYAN, f"\n=== Running GOOB Benchmark (30 positions to depth {depth}) ==="))
            trace_data, bench_summary = runner.run_bench(depth)
            print_trace_report(trace_data, title=f"GOOB TRACE REPORT (BENCHMARK DEPTH {depth})")
        elif args.fen:
            print(c(Colors.BOLD + Colors.CYAN, f"\n=== Searching Single Position (depth {args.depth}) ==="))
            trace_data = runner.run_fens([args.fen], depth=args.depth, movetime=args.movetime,
                                         threads=args.threads, hash_mb=args.hash)
            print_trace_report(trace_data, title=f"GOOB TRACE REPORT (SINGLE FEN)")
        elif args.fens:
            if not os.path.isfile(args.fens):
                print(c(Colors.RED, f"Error: FEN file '{args.fens}' not found."))
                sys.exit(1)
            with open(args.fens, "r") as f:
                fens = [line.strip() for line in f if line.strip() and not line.startswith("#")]
            print(c(Colors.BOLD + Colors.CYAN, f"\n=== Searching {len(fens)} positions from '{args.fens}' ==="))
            trace_data = runner.run_fens(fens, depth=args.depth, movetime=args.movetime,
                                         threads=args.threads, hash_mb=args.hash)
            print_trace_report(trace_data, title=f"GOOB TRACE REPORT ({len(fens)} POSITIONS)")

        if args.save_json:
            with open(args.save_json, "w") as f:
                json.dump(trace_data, f, indent=2)
            print(c(Colors.GREEN + Colors.BOLD, f"Saved trace statistics to '{args.save_json}'.\n"))

    finally:
        runner.stop()


if __name__ == "__main__":
    main()
