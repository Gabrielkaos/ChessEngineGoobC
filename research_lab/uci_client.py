"""
UCI Client and Engine Manager for Goob Chess Engine.
Handles UCI subprocess communication, options discovery, search info parsing,
and search heuristic trace capture.
"""

import os
import re
import sys
import json
import time
import math
import select
import asyncio
import subprocess
from typing import Dict, Any, Optional, List, Tuple, AsyncGenerator
import chess

from .models import (
    EngineInfo, PVLine, SearchIteration, AnalysisResult, TraceReport,
    CutoffDistribution, HeuristicMetric
)
from tools.trace_search import HEURISTIC_RULES

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def discover_engines() -> List[EngineInfo]:
    """Scans repository for available Goob builds and inspects their UCI capabilities."""
    candidates = [
        ("goob-native", "GOOB 2.2-BETA (Native)", os.path.join(ROOT_DIR, "src", "bin", "linux", "GOOB-2.2-BETA-native"), "release"),
        ("goob-trace", "GOOB 2.2-BETA (Trace Diagnostics)", os.path.join(ROOT_DIR, "src", "bin", "linux", "GOOB-2.2-BETA-native-trace"), "trace"),
        ("goob-tune", "GOOB 2.2-BETA (SPSA Tune Build)", os.path.join(ROOT_DIR, "src", "bin", "linux", "GOOB-2.2-BETA-native-tune"), "tune"),
        ("goob-2.1-linux", "GOOB 2.1 Linux x64 (Reference Baseline)", os.path.join(ROOT_DIR, "bin", "GOOB-2.1-linux-x64"), "legacy"),
        # Local reference engines
        ("stockfish-19", "Stockfish 19", os.path.join(ROOT_DIR, "Stockfish-sf_19", "stockfish-linux-x86-64-universal"), "reference"),
        ("ethereal-14", "Ethereal 14.00", os.path.join(ROOT_DIR, "Ethereal-14.00", "src", "Ethereal"), "reference"),
        ("berserk-14", "Berserk 14", os.path.join(ROOT_DIR, "berserk-14", "src", "berserk"), "reference"),
        ("c4ke-v3", "c4ke v3.0", os.path.join(ROOT_DIR, "c4ke-main", "c4ke-linux-x86-64-v3"), "reference"),
    ]

    engines = []
    for eng_id, name, path, build_type in candidates:
        if not os.path.exists(path):
            continue

        is_avail = os.access(path, os.X_OK)
        options = {}
        version_str = "Unknown"

        if is_avail:
            try:
                proc = subprocess.run(
                    [path],
                    input="uci\nquit\n",
                    capture_output=True,
                    text=True,
                    timeout=5
                )
                for line in proc.stdout.splitlines():
                    if line.startswith("id name "):
                        version_str = line[8:].strip()
                    elif line.startswith("option name "):
                        m = re.match(r"option name (.*?) type (.*?)(?: default (.*?))?(?: min (\d+))?(?: max (\d+))?$", line)
                        if m:
                            opt_name = m.group(1)
                            options[opt_name] = {
                                "type": m.group(2),
                                "default": m.group(3),
                                "min": int(m.group(4)) if m.group(4) else None,
                                "max": int(m.group(5)) if m.group(5) else None,
                            }
            except Exception as e:
                is_avail = False

        engines.append(EngineInfo(
            id=eng_id,
            name=name,
            path=path,
            build_type=build_type,
            version=version_str,
            is_available=is_avail,
            options=options
        ))

    return engines


def parse_uci_info_line(line: str, root_fen: str) -> Optional[PVLine]:
    """Parses a standard UCI info line into a PVLine model."""
    if not line.startswith("info ") or " pv " not in line:
        return None

    tokens = line.split()
    idx = 1
    depth = 0
    seldepth = 0
    multipv = 1
    score_type = "cp"
    score_val = 0
    nodes = 0
    time_ms = 0
    nps = 0
    pv_uci = []

    while idx < len(tokens):
        tok = tokens[idx]
        if tok == "depth" and idx + 1 < len(tokens):
            depth = int(tokens[idx + 1])
            idx += 2
        elif tok == "seldepth" and idx + 1 < len(tokens):
            seldepth = int(tokens[idx + 1])
            idx += 2
        elif tok == "multipv" and idx + 1 < len(tokens):
            multipv = int(tokens[idx + 1])
            idx += 2
        elif tok == "score" and idx + 2 < len(tokens):
            score_type = tokens[idx + 1]
            score_val = int(tokens[idx + 2])
            idx += 3
        elif tok == "nodes" and idx + 1 < len(tokens):
            nodes = int(tokens[idx + 1])
            idx += 2
        elif tok == "time" and idx + 1 < len(tokens):
            time_ms = int(tokens[idx + 1])
            idx += 2
        elif tok == "nps" and idx + 1 < len(tokens):
            nps = int(tokens[idx + 1])
            idx += 2
        elif tok == "pv":
            pv_uci = tokens[idx + 1:]
            break
        else:
            idx += 1

    if not pv_uci:
        return None

    if nps == 0 and time_ms > 0:
        nps = int((nodes * 1000) / time_ms)

    # Format score string
    if score_type == "cp":
        score_formatted = f"{score_val / 100.0:+.2f}"
    else:
        score_formatted = f"#{score_val:+d}"

    # Calculate Win/Draw/Loss probability based on lichess/chess-programming model
    # P(W) = 1 / (1 + 10^(-score_cp / 400))
    wdl = None
    if score_type == "cp":
        cp = score_val
        win_prob = 1.0 / (1.0 + math.exp(-cp / 150.0))
        # Draw probability peaks at cp=0 (~40-50%) and decays
        draw_prob = 0.45 * math.exp(-(cp ** 2) / (2 * (180 ** 2)))
        loss_prob = max(0.0, 1.0 - win_prob - draw_prob)
        # re-normalize
        total = win_prob + draw_prob + loss_prob
        wdl = {
            "win": round(win_prob / total, 3),
            "draw": round(draw_prob / total, 3),
            "loss": round(loss_prob / total, 3),
        }
    elif score_type == "mate":
        if score_val > 0:
            wdl = {"win": 1.0, "draw": 0.0, "loss": 0.0}
        else:
            wdl = {"win": 0.0, "draw": 0.0, "loss": 1.0}

    # Convert PV moves to SAN using python-chess
    pv_san = []
    try:
        b = chess.Board(root_fen)
        for uci_move in pv_uci:
            try:
                m = chess.Move.from_uci(uci_move)
                if m in b.legal_moves:
                    pv_san.append(b.san(m))
                    b.push(m)
                else:
                    pv_san.append(uci_move)
            except Exception:
                pv_san.append(uci_move)
    except Exception:
        pv_san = pv_uci[:]

    return PVLine(
        multipv=multipv,
        depth=depth,
        seldepth=seldepth,
        score_type=score_type,
        score_value=score_val,
        score_formatted=score_formatted,
        wdl=wdl,
        nodes=nodes,
        nps=nps,
        time_ms=time_ms,
        pv_uci=pv_uci,
        pv_san=pv_san
    )


def extract_trace_metrics(raw_json: Dict[str, Any]) -> TraceReport:
    """Evaluates raw JSON output from Goob's trace engine against heuristic health rules."""
    if not raw_json.get("trace_enabled", False):
        return TraceReport(trace_enabled=False, error=raw_json.get("error", "Trace not enabled"))

    heuristics = raw_json.get("heuristics", {})
    cutoffs_raw = raw_json.get("cutoffs", {})

    cutoffs = CutoffDistribution(
        total=cutoffs_raw.get("total", 0),
        first_move=cutoffs_raw.get("first_move", 0),
        tt_move=cutoffs_raw.get("tt_move", 0),
        killer=cutoffs_raw.get("killer", 0),
        counter=cutoffs_raw.get("counter", 0),
        followup=cutoffs_raw.get("followup", 0),
        quiet=cutoffs_raw.get("quiet", 0),
        noisy=cutoffs_raw.get("noisy", 0),
    )

    diagnostic_items = []
    for rule in HEURISTIC_RULES:
        key = rule["key"]
        sub_key = rule.get("sub_key", "rate")
        h_data = heuristics.get(key, {})
        if not h_data:
            continue

        considered = h_data.get("considered") or h_data.get("attempted")
        triggered = h_data.get("triggered") or h_data.get("cutoffs") or h_data.get("reduced") or h_data.get("researched")
        val = h_data.get(sub_key, 0.0)

        # rate might be given as ratio 0..1; convert to percentage if < 1.0
        pct = (val * 100.0) if val <= 1.0 and val > 0 else val

        status = "normal"
        advice = "Operating within normal parameters."
        min_p = rule.get("min_pct", 0.0)
        max_p = rule.get("max_pct", 100.0)

        if considered and considered > 50:  # meaningful sample
            if pct < min_p:
                status = "low"
                advice = rule.get("advice_low", "Rate is below expected threshold.")
            elif pct > max_p:
                status = "high"
                advice = rule.get("advice_high", "Rate is above expected threshold.")

        diagnostic_items.append(HeuristicMetric(
            name=rule["name"],
            considered=considered,
            triggered=triggered,
            rate=round(pct, 2) if pct is not None else None,
            status=status,
            advice=advice,
            constants=rule.get("constants")
        ))

    return TraceReport(
        trace_enabled=True,
        mode=raw_json.get("mode", "current"),
        searches_count=raw_json.get("searches_count", 1),
        ab_nodes=raw_json.get("ab_nodes", 0),
        qs_nodes=raw_json.get("qs_nodes", 0),
        total_nodes=raw_json.get("total_nodes", 0),
        eval_calls=raw_json.get("eval_calls", 0),
        nnue_refresh_mg=raw_json.get("nnue_refresh_mg", 0),
        nnue_refresh_eg=raw_json.get("nnue_refresh_eg", 0),
        tt_hits=raw_json.get("tt_hits", 0),
        tt_cutoffs=raw_json.get("tt_cutoffs", 0),
        tt_research_cutoffs=raw_json.get("tt_research_cutoffs", 0),
        syzygy_probes=raw_json.get("syzygy_probes", 0),
        syzygy_hits=raw_json.get("syzygy_hits", 0),
        cutoffs=cutoffs,
        heuristics=heuristics,
        diagnostic_items=diagnostic_items
    )


class UCIEngineProcess:
    """Manages an active UCI engine subprocess for position search and analysis."""

    def __init__(self, binary_path: str):
        self.binary_path = binary_path
        self.process: Optional[subprocess.Popen] = None
        self.is_running = False

    def start(self):
        if not os.path.exists(self.binary_path) or not os.access(self.binary_path, os.X_OK):
            raise FileNotFoundError(f"Engine binary not found or not executable: {self.binary_path}")

        self.process = subprocess.Popen(
            [self.binary_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )
        self.is_running = True

        # Initial handshake
        self.send("uci")
        while True:
            line = self.readline()
            if not line or "uciok" in line:
                break

        self.send("isready")
        while True:
            line = self.readline()
            if not line or "readyok" in line:
                break

    def send(self, cmd: str):
        if self.process and self.process.stdin:
            self.process.stdin.write(cmd + "\n")
            self.process.stdin.flush()

    def readline(self) -> str:
        if not self.process or not self.process.stdout:
            return ""
        return self.process.stdout.readline()

    def stop_search(self):
        """Sends 'stop' and drains until 'bestmove'."""
        if self.is_running:
            self.send("stop")
            while True:
                line = self.readline()
                if not line or line.startswith("bestmove"):
                    break

    def quit(self):
        if self.process:
            try:
                self.send("quit")
                self.process.communicate(timeout=2)
            except Exception:
                self.process.kill()
            self.process = None
            self.is_running = False

    def query_trace(self) -> Optional[Dict[str, Any]]:
        """Queries Goob's trace engine for heuristic stats in JSON."""
        self.send("trace current json")
        self.send("isready")
        collected_lines = []
        while True:
            line = self.readline()
            if not line:
                break
            line_str = line.strip()
            if line_str == "readyok":
                break
            collected_lines.append(line_str)

        full_text = "\n".join(collected_lines).strip()
        start = full_text.find("{")
        end = full_text.rfind("}")
        if start != -1 and end != -1 and end > start:
            try:
                return json.loads(full_text[start:end+1])
            except json.JSONDecodeError:
                return None
        return None

    def analyze(self, fen: str, depth: Optional[int] = 12, movetime_ms: Optional[int] = None,
                multipv: int = 1, hash_mb: int = 64, threads: int = 1) -> AnalysisResult:
        """Executes a synchronous analysis on the given FEN and returns an AnalysisResult."""
        if not self.is_running:
            self.start()

        self.send(f"setoption name Hash value {hash_mb}")
        self.send(f"setoption name Threads value {threads}")
        self.send(f"setoption name MultiPV value {multipv}")
        self.send("isready")
        while True:
            line = self.readline()
            if "readyok" in line:
                break

        self.send("ucinewgame")
        self.send(f"position fen {fen}")

        go_cmd = "go"
        if depth:
            go_cmd += f" depth {depth}"
        elif movetime_ms:
            go_cmd += f" movetime {movetime_ms}"
        else:
            go_cmd += " depth 10"

        self.send(go_cmd)

        pv_lines_by_multipv: Dict[int, PVLine] = {}
        iterations: List[SearchIteration] = []
        best_move_uci = ""
        seen_depths = set()

        while True:
            line = self.readline()
            if not line:
                break
            line = line.strip()
            if line.startswith("bestmove"):
                parts = line.split()
                if len(parts) > 1:
                    best_move_uci = parts[1]
                break

            parsed_pv = parse_uci_info_line(line, fen)
            if parsed_pv:
                pv_lines_by_multipv[parsed_pv.multipv] = parsed_pv

                # Record iteration progress for PV 1
                if parsed_pv.multipv == 1 and parsed_pv.depth not in seen_depths:
                    seen_depths.add(parsed_pv.depth)
                    best_san = parsed_pv.pv_san[0] if parsed_pv.pv_san else None
                    iterations.append(SearchIteration(
                        depth=parsed_pv.depth,
                        seldepth=parsed_pv.seldepth,
                        score_cp=parsed_pv.score_value if parsed_pv.score_type == "cp" else None,
                        score_mate=parsed_pv.score_value if parsed_pv.score_type == "mate" else None,
                        nodes=parsed_pv.nodes,
                        nps=parsed_pv.nps,
                        time_ms=parsed_pv.time_ms,
                        best_move_uci=parsed_pv.pv_uci[0] if parsed_pv.pv_uci else "",
                        best_move_san=best_san,
                        pv=parsed_pv.pv_san
                    ))

        # Query trace metrics if available
        raw_trace = self.query_trace()
        trace_report = extract_trace_metrics(raw_trace) if raw_trace else None

        # Convert bestmove to SAN
        best_move_san = best_move_uci
        try:
            b = chess.Board(fen)
            m = chess.Move.from_uci(best_move_uci)
            if m in b.legal_moves:
                best_move_san = b.san(m)
        except Exception:
            pass

        primary_line = pv_lines_by_multipv.get(1)
        score_type = primary_line.score_type if primary_line else "cp"
        score_val = primary_line.score_value if primary_line else 0
        score_fmt = primary_line.score_formatted if primary_line else "+0.00"
        final_depth = primary_line.depth if primary_line else (depth or 0)

        lines_list = [pv_lines_by_multipv[k] for k in sorted(pv_lines_by_multipv.keys())]

        return AnalysisResult(
            fen=fen,
            engine_id=os.path.basename(self.binary_path),
            engine_name=os.path.basename(self.binary_path),
            depth=final_depth,
            best_move_uci=best_move_uci,
            best_move_san=best_move_san,
            score_type=score_type,
            score_value=score_val,
            score_formatted=score_fmt,
            lines=lines_list,
            iterations=iterations,
            trace=trace_report,
            created_at=time.strftime("%Y-%m-%dT%H:%M:%S")
        )


class AsyncUCIEngine:
    """Asynchronous, non-blocking UCI engine controller for real-time WebSocket streaming."""

    def __init__(self, binary_path: str):
        self.binary_path = binary_path
        self.proc: Optional[asyncio.subprocess.Process] = None

    async def start(self):
        if not os.path.exists(self.binary_path) or not os.access(self.binary_path, os.X_OK):
            raise FileNotFoundError(f"Engine binary not found or not executable: {self.binary_path}")

        self.proc = await asyncio.create_subprocess_exec(
            self.binary_path,
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )

        await self.send("uci")
        while True:
            line = await self.readline(timeout=3.0)
            if not line or "uciok" in line:
                break

        await self.send("isready")
        while True:
            line = await self.readline(timeout=3.0)
            if not line or "readyok" in line:
                break

    async def send(self, cmd: str):
        if self.proc and self.proc.stdin and self.proc.returncode is None:
            self.proc.stdin.write((cmd + "\n").encode())
            try:
                await self.proc.stdin.drain()
            except (ConnectionResetError, BrokenPipeError):
                pass

    async def readline(self, timeout: float = 10.0) -> str:
        if not self.proc or not self.proc.stdout or self.proc.returncode is not None:
            return ""
        try:
            line_bytes = await asyncio.wait_for(self.proc.stdout.readline(), timeout=timeout)
            return line_bytes.decode(errors="replace")
        except (asyncio.TimeoutError, ConnectionResetError, BrokenPipeError):
            return ""

    async def stop_search(self):
        """Sends 'stop' to the engine. Does not read stdout so that the streaming task receives bestmove."""
        if self.proc and self.proc.returncode is None:
            await self.send("stop")

    async def quit(self):
        if self.proc:
            if self.proc.returncode is None:
                try:
                    await self.send("quit")
                    await asyncio.wait_for(self.proc.wait(), timeout=0.8)
                except Exception:
                    try:
                        self.proc.terminate()
                        await asyncio.wait_for(self.proc.wait(), timeout=0.8)
                    except Exception:
                        try:
                            self.proc.kill()
                            await self.proc.wait()
                        except Exception:
                            pass
            if self.proc and self.proc.stdin:
                try:
                    self.proc.stdin.close()
                except Exception:
                    pass
            self.proc = None

    async def query_trace(self, timeout: float = 1.0) -> Optional[Dict[str, Any]]:
        if not self.proc or self.proc.returncode is not None:
            return None
        await self.send("trace current json")
        await self.send("isready")
        collected_lines = []
        while True:
            line = await self.readline(timeout=timeout)
            if not line:
                break
            line_str = line.strip()
            if line_str == "readyok":
                break
            collected_lines.append(line_str)

        full_text = "\n".join(collected_lines).strip()
        start = full_text.find("{")
        end = full_text.rfind("}")
        if start != -1 and end != -1 and end > start:
            try:
                return json.loads(full_text[start:end+1])
            except json.JSONDecodeError:
                return None
        return None
