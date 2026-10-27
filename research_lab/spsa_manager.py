"""
SPSA Tuning Manager for Goob Chess Engine.
Manages search parameter tuning experiments, interfaces with tools/spsa/spsa.py,
reads run state, parses iteration histories, and monitors active tuning processes.
"""

import os
import re
import csv
import json
import signal
import subprocess
import threading
import time
from typing import Dict, Any, Optional, List, Tuple
from datetime import datetime

from .models import SPSAParameter, SPSARunConfig, SPSARunSummary
from .database import save_experiment, update_experiment_status, get_experiment

VALID_RUN_NAME_RE = re.compile(r"^[A-Za-z0-9_.-]+$")


def _valid_run_name(name: str) -> bool:
    return bool(VALID_RUN_NAME_RE.match(name)) and ".." not in name and name not in (".", "..")

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SPSA_PY = os.path.join(ROOT_DIR, "tools", "spsa", "spsa.py")
RUNS_DIR = os.path.join(ROOT_DIR, "tools", "spsa", "runs")
TUNE_BIN = os.path.join(ROOT_DIR, "src", "bin", "linux", "GOOB-2.2-BETA-native-tune")
TUNE_H = os.path.join(ROOT_DIR, "src", "tune.h")


class SPSAManager:
    """Manages SPSA tuning experiments and process lifecycle."""

    def __init__(self):
        self.active_processes: Dict[str, subprocess.Popen] = {}
        self.log_buffers: Dict[str, List[str]] = {}
        self.lock = threading.Lock()
        os.makedirs(RUNS_DIR, exist_ok=True)

    def list_runs(self) -> List[str]:
        """Lists all existing SPSA run names in tools/spsa/runs/."""
        if not os.path.exists(RUNS_DIR):
            return []
        runs = []
        for d in os.listdir(RUNS_DIR):
            path = os.path.join(RUNS_DIR, d)
            if os.path.isdir(path) and os.path.exists(os.path.join(path, "state.json")):
                runs.append(d)
        runs.sort()
        return runs

    def get_run_summary(self, name: str, sample_history: int = 100) -> Optional[SPSARunSummary]:
        """Loads state.json and samples history.csv for an SPSA run."""
        if not _valid_run_name(name):
            return None
        run_path = os.path.join(RUNS_DIR, name)
        state_file = os.path.join(run_path, "state.json")
        if not os.path.exists(state_file):
            return None

        with open(state_file, "r") as f:
            state = json.load(f)

        config = state.get("config", {})
        iter_done = state.get("iter", 0)
        iter_total = config.get("iterations", 0)
        pairs = config.get("pairs", 2)
        games_played = iter_done * pairs * 2
        tc = config.get("tc", "5+0.05")
        time_sec = state.get("time_sec", 0.0)

        # Parse parameters
        params_list = []
        for p in state.get("params", []):
            theta = p.get("theta", 0.0)
            def_val = p.get("default", 0)
            tuned_val = round(theta)
            params_list.append(SPSAParameter(
                name=p.get("name", ""),
                default=def_val,
                tuned=tuned_val,
                theta=round(theta, 2),
                change=tuned_val - def_val,
                min_val=p.get("min", 0),
                max_val=p.get("max", 0),
                c_end=p.get("c_end"),
                r_end=p.get("r_end")
            ))

        # Check if process is active
        status = "idle"
        with self.lock:
            if name in self.active_processes:
                status = "running"
            elif iter_done >= iter_total and iter_total > 0:
                status = "completed"

        # Read history.csv
        history_file = os.path.join(run_path, "history.csv")
        history_points = []
        if os.path.exists(history_file):
            try:
                with open(history_file, "r") as f:
                    reader = csv.reader(f)
                    header = next(reader, None)
                    all_rows = list(reader)
                    step = max(1, len(all_rows) // sample_history) if all_rows else 1
                    sampled_rows = all_rows[::step]
                    if all_rows and (not sampled_rows or sampled_rows[-1] != all_rows[-1]):
                        sampled_rows.append(all_rows[-1])

                    if header:
                        for row in sampled_rows:
                            if len(row) == len(header):
                                pt = {}
                                for h, v in zip(header, row):
                                    try:
                                        pt[h] = float(v) if "." in v else int(v)
                                    except ValueError:
                                        pt[h] = v
                                history_points.append(pt)
            except Exception:
                pass

        return SPSARunSummary(
            name=name,
            status=status,
            iterations_completed=iter_done,
            iterations_total=iter_total,
            games_played=games_played,
            tc=tc,
            elapsed_hours=round(time_sec / 3600.0, 2),
            params=params_list,
            recent_history=history_points
        )

    def get_cutechess_options(self, name: str) -> str:
        """Generates cutechess-cli option string for the current tuned parameters."""
        summary = self.get_run_summary(name)
        if not summary:
            return ""
        opts = [f"option.{p.name}={p.tuned}" for p in summary.params]
        return " ".join(opts)

    def start_run(self, config: SPSARunConfig) -> bool:
        """Launches or resumes an SPSA tuning run in the background."""
        if not _valid_run_name(config.name):
            raise ValueError(f"Invalid SPSA run name: {config.name}")
        with self.lock:
            if config.name in self.active_processes:
                return False

        engine_path = config.engine_path or TUNE_BIN
        if not os.path.exists(engine_path) or not os.access(engine_path, os.X_OK):
            raise FileNotFoundError(f"Tune engine not found or not executable: {engine_path}")

        book_path = os.path.abspath(config.book)
        if not os.path.exists(book_path):
            book_path = os.path.join(ROOT_DIR, "tools", "book.epd")

        cmd = [
            sys.executable,
            SPSA_PY,
            "run",
            config.name,
            "--engine", engine_path,
            "--iterations", str(config.iterations),
            "--pairs", str(config.pairs),
            "--tc", config.tc,
            "--workers", str(config.workers),
            "--hash", str(config.hash_mb),
            "--book", book_path
        ]

        if config.include:
            cmd.extend(["--include", config.include])
        if config.exclude:
            cmd.extend(["--exclude", config.exclude])

        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            preexec_fn=os.setsid
        )

        with self.lock:
            self.active_processes[config.name] = proc
            self.log_buffers[config.name] = []

        thread = threading.Thread(
            target=self._monitor_process,
            args=(config.name, proc),
            daemon=True
        )
        thread.start()

        # Save experiment entry
        save_experiment({
            "id": f"spsa_{config.name}",
            "title": f"SPSA Tuning: {config.name}",
            "exp_type": "spsa",
            "status": "running",
            "created_at": datetime.now().isoformat(),
            "binary_name": os.path.basename(engine_path),
            "settings": config.model_dump(),
            "notes": f"SPSA run with {config.workers} workers, tc={config.tc}"
        })

        return True

    def _monitor_process(self, name: str, proc: subprocess.Popen):
        while True:
            line = proc.stdout.readline()
            if not line and proc.poll() is not None:
                break
            if line:
                with self.lock:
                    buf = self.log_buffers.setdefault(name, [])
                    buf.append(line)
                    if len(buf) > 1000:
                        buf.pop(0)

        proc.wait()

        with self.lock:
            self.active_processes.pop(name, None)

        # Update experiment in db
        summary = self.get_run_summary(name)
        if summary:
            status = "completed" if summary.iterations_completed >= summary.iterations_total else "stopped"
            update_experiment_status(
                f"spsa_{name}", status, datetime.now().isoformat(), summary.model_dump()
            )

    def stop_run(self, name: str) -> bool:
        """Sends SIGINT so spsa.py can cleanly flush state.json."""
        with self.lock:
            proc = self.active_processes.get(name)
            if not proc:
                return False

            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGINT)
                return True
            except Exception:
                try:
                    proc.terminate()
                    return True
                except Exception:
                    return False

    def get_logs(self, name: str, lines: int = 200) -> List[str]:
        with self.lock:
            buf = self.log_buffers.get(name, [])
            return buf[-lines:]


# Global singleton instance
spsa_manager = SPSAManager()
