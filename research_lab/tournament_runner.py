"""
Tournament and SPRT Match Runner for Goob Chess Engine.
Wraps cutechess-cli, manages concurrent matches, parses real-time statistics,
and records experiments and PGN games.
"""

import os
import re
import sys
import shutil
import subprocess
import threading
import time
from typing import Dict, Any, Optional, List, Callable
from datetime import datetime

from .models import MatchConfig, MatchResult
from .database import save_experiment, get_experiment

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DEFAULT_BOOK = os.path.join(ROOT_DIR, "tools", "book.epd")
LOGS_DIR = os.path.join(ROOT_DIR, "research_lab", "data", "tournament_logs")
PGNS_DIR = os.path.join(ROOT_DIR, "research_lab", "data", "pgns")


class TournamentRunner:
    """Manages cutechess-cli match runs and status tracking."""

    def __init__(self):
        self.active_processes: Dict[str, subprocess.Popen] = {}
        self.match_states: Dict[str, MatchResult] = {}
        self.log_callbacks: Dict[str, List[Callable[[str], None]]] = {}
        self.lock = threading.Lock()
        os.makedirs(LOGS_DIR, exist_ok=True)
        os.makedirs(PGNS_DIR, exist_ok=True)

    def get_cutechess_path(self) -> Optional[str]:
        p = shutil.which("cutechess-cli")
        if p and os.access(p, os.X_OK):
            return p
        fallback = os.path.expanduser("~/.local/bin/cutechess-cli")
        if os.path.exists(fallback) and os.access(fallback, os.X_OK):
            return fallback
        usr_local = "/usr/local/bin/cutechess-cli"
        if os.path.exists(usr_local) and os.access(usr_local, os.X_OK):
            return usr_local
        return None

    def start_match(self, config: MatchConfig, new_bin_path: str, base_bin_path: str) -> MatchResult:
        cutechess = self.get_cutechess_path()
        if not cutechess:
            raise FileNotFoundError("cutechess-cli not found in PATH or ~/.local/bin")

        if not os.path.exists(new_bin_path) or not os.access(new_bin_path, os.X_OK):
            raise FileNotFoundError(f"New engine binary not found or not executable: {new_bin_path}")
        if not os.path.exists(base_bin_path) or not os.access(base_bin_path, os.X_OK):
            raise FileNotFoundError(f"Baseline engine binary not found or not executable: {base_bin_path}")

        book_path = os.path.abspath(config.book)
        if not os.path.exists(book_path):
            book_path = DEFAULT_BOOK

        match_id = f"match_{int(time.time())}_{config.name}"
        log_path = os.path.join(LOGS_DIR, f"{match_id}.log")
        pgn_path = os.path.join(PGNS_DIR, f"{match_id}.pgn")

        cmd = [
            cutechess,
            "-engine", f"cmd={new_bin_path}", f"name={config.engine_new_id}",
            "-engine", f"cmd={base_bin_path}", f"name={config.engine_base_id}",
            "-each", "proto=uci", f"tc={config.tc}", f"option.Hash={config.hash_mb}", f"option.Threads={config.threads}",
            "-openings", f"file={book_path}", "format=epd", "order=random", "-repeat",
            "-rounds", str(config.rounds), "-games", "2", "-concurrency", str(config.concurrency),
            "-draw", "movenumber=40", "movecount=8", "score=10",
            "-resign", "movecount=3", "score=400",
            "-ratinginterval", str(config.rating_interval or 10), "-recover",
            "-pgnout", pgn_path
        ]

        if config.is_sprt:
            cmd.extend([
                "-sprt",
                f"elo0={config.elo0}",
                f"elo1={config.elo1}",
                f"alpha={config.alpha}",
                f"beta={config.beta}"
            ])

        state = MatchResult(
            id=match_id,
            name=config.name,
            status="running",
            engine_new_name=config.engine_new_id,
            engine_base_name=config.engine_base_id,
            tc=config.tc,
            concurrency=config.concurrency,
            log_file=log_path,
            pgn_file=pgn_path,
            created_at=datetime.now().isoformat()
        )

        with self.lock:
            self.match_states[match_id] = state

        # Save initial experiment entry
        save_experiment({
            "id": match_id,
            "title": f"Match: {config.engine_new_id} vs {config.engine_base_id}",
            "exp_type": "sprt" if config.is_sprt else "match",
            "status": "running",
            "created_at": state.created_at,
            "binary_name": os.path.basename(new_bin_path),
            "settings": config.model_dump(),
            "results": state.model_dump(),
            "log_path": log_path,
            "pgn_path": pgn_path
        })

        thread = threading.Thread(
            target=self._run_process,
            args=(match_id, cmd, log_path),
            daemon=True
        )
        thread.start()

        return state

    def _run_process(self, match_id: str, cmd: List[str], log_path: str):
        with open(log_path, "w") as log_file:
            log_file.write(f"Command: {' '.join(cmd)}\n\n")
            log_file.flush()

            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1
            )

            with self.lock:
                self.active_processes[match_id] = proc

            score_pattern = re.compile(r"Score of .*?:\s+(\d+)\s+-\s+(\d+)\s+-\s+(\d+)\s+\[([\d\.]+)\]\s+(\d+)")
            elo_pattern = re.compile(r"Elo difference:\s+([+-]?\d+\.?\d*)\s+\+/-\s+(\d+\.?\d*)")
            sprt_pattern = re.compile(r"SPRT:\s+llr\s+([+-]?\d+\.?\d*)\s+\(([\d\.]+)%\),\s+lbound\s+([+-]?\d+\.?\d*),\s+ubound\s+([+-]?\d+\.?\d*)")
            sprt_dec_pattern = re.compile(r"SPRT:\s+([a-zA-Z0-9\s]+(?:accepted|rejected|H0|H1)[a-zA-Z0-9\s]*)")

            while True:
                line = proc.stdout.readline()
                if not line and proc.poll() is not None:
                    break
                if not line:
                    continue

                log_file.write(line)
                log_file.flush()

                # Dispatch to listeners
                with self.lock:
                    callbacks = self.log_callbacks.get(match_id, [])
                    for cb in callbacks:
                        try:
                            cb(line)
                        except Exception:
                            pass

                # Parse cutechess output lines
                m_score = score_pattern.search(line)
                if m_score:
                    with self.lock:
                        st = self.match_states.get(match_id)
                        if st:
                            st.wins = int(m_score.group(1))
                            st.losses = int(m_score.group(2))
                            st.draws = int(m_score.group(3))
                            st.score_pct = round(float(m_score.group(4)) * 100.0, 2)
                            st.games_played = int(m_score.group(5))

                m_elo = elo_pattern.search(line)
                if m_elo:
                    with self.lock:
                        st = self.match_states.get(match_id)
                        if st:
                            st.elo_diff = float(m_elo.group(1))
                            st.elo_error = float(m_elo.group(2))

                m_sprt = sprt_pattern.search(line)
                if m_sprt:
                    with self.lock:
                        st = self.match_states.get(match_id)
                        if st:
                            st.sprt_llr = float(m_sprt.group(1))
                            st.sprt_lbound = float(m_sprt.group(3))
                            st.sprt_ubound = float(m_sprt.group(4))
                            st.sprt_status = "continuing"

                m_sprt_dec = sprt_dec_pattern.search(line)
                if m_sprt_dec:
                    with self.lock:
                        st = self.match_states.get(match_id)
                        if st:
                            dec = m_sprt_dec.group(1).strip()
                            if "accepted H1" in dec or "H1 accepted" in dec:
                                st.sprt_status = "accepted_h1"
                            elif "accepted H0" in dec or "H0 accepted" in dec:
                                st.sprt_status = "accepted_h0"

            returncode = proc.wait()

            with self.lock:
                self.active_processes.pop(match_id, None)
                st = self.match_states.get(match_id)
                if st:
                    if st.status == "running":
                        st.status = "completed" if returncode == 0 else "failed"
                    st.finished_at = datetime.now().isoformat()

                    save_experiment({
                        "id": match_id,
                        "title": f"Match: {st.engine_new_name} vs {st.engine_base_name}",
                        "exp_type": "sprt" if st.sprt_status else "match",
                        "status": st.status,
                        "finished_at": st.finished_at,
                        "results": st.model_dump(),
                        "log_path": log_path,
                        "pgn_path": st.pgn_file
                    })

    def stop_match(self, match_id: str) -> bool:
        with self.lock:
            proc = self.active_processes.get(match_id)
            st = self.match_states.get(match_id)
            if proc:
                st.status = "stopped"
                st.finished_at = datetime.now().isoformat()
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except Exception:
                    proc.kill()
                self.active_processes.pop(match_id, None)

                save_experiment({
                    "id": match_id,
                    "status": "stopped",
                    "finished_at": st.finished_at,
                    "results": st.model_dump()
                })
                return True
        return False

    def get_status(self, match_id: str) -> Optional[MatchResult]:
        with self.lock:
            if match_id in self.match_states:
                return self.match_states[match_id]

        # Check DB
        exp = get_experiment(match_id)
        if exp and "results" in exp:
            return MatchResult(**exp["results"])
        return None

    def list_all_matches(self) -> List[MatchResult]:
        results = []
        with self.lock:
            for st in self.match_states.values():
                results.append(st)
        return results


# Global singleton instance
runner = TournamentRunner()
