#!/usr/bin/env python3
"""Self-play dataset generation for GOOB Texel tuning.

Usage:
    python3 datagen.py [nodes] [games] [threads] [out] [book] [pos_cap]

    nodes        search nodes per move          (default 5000)
    games        total games across all threads (default 10000)
    threads      number of engine processes     (default: cpu count)
    out          output file, FEN;result lines  (default dataset.epd, appended)
    book         opening book EPD/FEN file      (default book.epd)
    pos_cap      max quiet positions kept per game; 0 = keep all (default)

Output format is "FEN;result" (result from white's POV), which tools/tuner.c
parses directly.
"""

import os
import random
import subprocess
import sys
import threading
import time

_VENDOR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vendor")
if os.path.isdir(_VENDOR):
    sys.path.insert(0, _VENDOR)
import chess

ENGINE = os.environ.get(
    "GOOB_ENGINE",
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src", "bin", "GOOB-2.1"),
)
NODES = int(sys.argv[1]) if len(sys.argv) > 1 else 5000
GAMES = int(sys.argv[2]) if len(sys.argv) > 2 else 10000
THREADS = int(sys.argv[3]) if len(sys.argv) > 3 else os.cpu_count() or 4
OUT = sys.argv[4] if len(sys.argv) > 4 else "dataset.epd"
BOOK_FILE = sys.argv[5] if len(sys.argv) > 5 else "book.epd"
POS_CAP = int(sys.argv[6]) if len(sys.argv) > 6 else 0

MAX_PLY = 200
MIN_PLY_RECORD = 8       # skip first plies; the book already gives variety
LOG_EVAL_MAX = 2000      # don't record positions already evaluated as decided
ADJUDICATE_PLY = 80
ADJUDICATE_EVAL = 800
ADJUDICATE_CHAIN = 4     # require this many consecutive plies beyond threshold
MULTIPV = 3
PICK_W = [0.60, 0.25, 0.15]
START_FEN = chess.STARTING_FEN

# Load opening book
OPENINGS = [START_FEN]
if BOOK_FILE and os.path.exists(BOOK_FILE):
    with open(BOOK_FILE, 'r') as f:
        OPENINGS = [line.strip() for line in f if line.strip()]
    print(f"Loaded {len(OPENINGS)} openings from {BOOK_FILE}")
else:
    print("Warning: No opening book provided. Using standard startpos only. Pass a book.epd file as the 5th argument.")


class Engine:
    def __init__(self):
        # Point working directory to src/ (one folder up from src/bin/)
        engine_bin_dir = os.path.dirname(ENGINE)
        engine_src_dir = os.path.abspath(os.path.join(engine_bin_dir, ".."))

        self.p = subprocess.Popen(
            [ENGINE],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
            cwd=engine_src_dir
        )
        self.send("uci")
        self.read_until("uciok")
        # One engine process per worker thread: keep them single-threaded and
        # small so many workers fit in RAM.
        self.send("setoption name Threads value 1")
        self.send("setoption name Hash value 16")
        self.send("setoption name EvalHash value 4")
        self.send(f"setoption name MultiPV value {MULTIPV}")
        self.isready()

    def send(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()

    def read_until(self, token):
        while True:
            line = self.p.stdout.readline()
            if line == "":  # EOF: process died or closed stdout
                raise EOFError("Engine crashed or closed stdout")
            line_str = line.strip()
            if token in line_str:
                return line_str

    def isready(self):
        self.send("isready")
        self.read_until("readyok")

    def newgame(self):
        self.send("ucinewgame")
        self.isready()

    def search(self, poscmd, nodes):
        """Returns (chosen_move_uci, eval_cp_from_side_to_move_pov)."""
        self.send(poscmd)
        self.send(f"go nodes {nodes}")

        pvs = {}
        bm = None
        eval_score = 0

        while True:
            line = self.p.stdout.readline()
            if line == "":
                raise EOFError("Engine crashed during search")
            line = line.strip()

            if line.startswith("info") and "multipv" in line and " pv " in line:
                tokens = line.split()
                try:
                    mpv = int(tokens[tokens.index("multipv") + 1])
                    move = tokens[tokens.index("pv") + 1]
                    pvs[mpv] = move

                    if mpv == 1 and "score" in tokens:
                        if "cp" in tokens:
                            eval_score = int(tokens[tokens.index("cp") + 1])
                        elif "mate" in tokens:
                            mate_dist = int(tokens[tokens.index("mate") + 1])
                            eval_score = 10000 if mate_dist > 0 else -10000
                except (ValueError, IndexError):
                    pass
            elif line.startswith("bestmove"):
                tokens = line.split()
                if len(tokens) >= 2:
                    bm = tokens[1]
                break

        ranked_moves = [pvs[k] for k in sorted(pvs.keys()) if k in pvs]
        if ranked_moves:
            bm = random.choices(ranked_moves, weights=PICK_W[:len(ranked_moves)])[0]

        return bm, eval_score

    def close(self):
        try:
            self.send("quit")
            self.p.wait(timeout=2)
        except Exception:
            self.p.kill()


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.games = 0
        self.wdl = {1.0: 0, 0.5: 0, 0.0: 0}
        self.adjudicated = 0
        self.natural_endings = 0
        self.timeouts = 0
        self.positions = 0
        self.restarts = 0
        self.total_ply = 0
        self.t0 = time.time()

    def add_game(self, result, ply, npos, how):
        with self.lock:
            self.games += 1
            self.wdl[result] = self.wdl.get(result, 0) + 1
            self.total_ply += ply
            self.positions += npos
            if how == "adjudicated":
                self.adjudicated += 1
            elif how == "natural":
                self.natural_endings += 1
            else:
                self.timeouts += 1

    def add_restart(self):
        with self.lock:
            self.restarts += 1

    def report(self):
        with self.lock:
            g = max(self.games, 1)
            dt = time.time() - self.t0
            w = self.wdl.get(1.0, 0)
            d = self.wdl.get(0.5, 0)
            l = self.wdl.get(0.0, 0)
            print(
                f"\n=== Datagen summary ===\n"
                f"Games: {self.games} ({dt:.0f}s, {self.games / dt * 3600:.0f} games/h)\n"
                f"Results: W {w / g:.1%} / D {d / g:.1%} / L {l / g:.1%}"
                f"   [adjudicated {self.adjudicated}, natural {self.natural_endings}, max-ply {self.timeouts}]\n"
                f"Avg game length: {self.total_ply / g:.0f} plies\n"
                f"Positions written: {self.positions} ({self.positions / g:.1f}/game)\n"
                f"Engine restarts (crashes): {self.restarts}"
            )


def pick_opening():
    """Random opening, lazily dropping malformed book entries."""
    while True:
        fen = random.choice(OPENINGS)
        try:
            chess.Board(fen)
            return fen
        except ValueError:
            print(f"Dropping invalid book entry: {fen[:40]}...", flush=True)
            try:
                OPENINGS.remove(fen)
            except ValueError:
                pass
            if not OPENINGS:
                raise RuntimeError("No valid openings left in book")


def play_game(eng, out_fh, lock, stats):
    """Plays one game, appends 'FEN;result' lines to the shared output."""
    start_fen = pick_opening()
    board = chess.Board(start_fen)
    moves_history = []
    quiet_positions = []

    ply = 0
    result = None
    how = "natural"
    win_chain = 0

    eng.newgame()

    while True:
        # 1. Rule-based draw checks (fifty-move/threefold claims + insufficient material)
        if board.is_insufficient_material() or board.can_claim_draw():
            result = 0.5
            break

        # 2. Build position command (send full history so the engine sees repetitions)
        cmd = f"position fen {start_fen}"
        if moves_history:
            cmd += " moves " + " ".join(moves_history)

        # 3. Search
        bm_str, eval_score = eng.search(cmd, NODES)

        # 4. Handle terminal positions / validate the engine's move
        if bm_str is None or bm_str in ("(none)", "0000"):
            # No move returned: checkmate (side to move loses) or stalemate
            if board.is_check():
                result = 0.0 if board.turn == chess.WHITE else 1.0
            else:
                result = 0.5
            break
        try:
            move = board.parse_uci(bm_str)
        except ValueError:
            raise RuntimeError(f"Illegal/unknown bestmove '{bm_str}' at {board.fen()}")

        # White-POV eval for adjudication/logging
        we = eval_score if board.turn == chess.WHITE else -eval_score

        # 5. Adjudication: require several consecutive plies beyond threshold so a
        # single tactical fluke can't flip the label of the whole game.
        if ply >= ADJUDICATE_PLY:
            if we >= ADJUDICATE_EVAL:
                win_chain = win_chain + 1 if win_chain > 0 else 1
            elif we <= -ADJUDICATE_EVAL:
                win_chain = win_chain - 1 if win_chain < 0 else -1
            else:
                win_chain = 0
            if win_chain >= ADJUDICATE_CHAIN:
                result = 1.0
                how = "adjudicated"
                break
            if win_chain <= -ADJUDICATE_CHAIN:
                result = 0.0
                how = "adjudicated"
                break

        if ply >= MAX_PLY:
            result = 0.5 if abs(we) < ADJUDICATE_EVAL else (1.0 if we > 0 else 0.0)
            how = "max-ply"
            break

        # 6. Data collection: only log QUIET, non-decided positions
        if (
            ply >= MIN_PLY_RECORD
            and not board.is_capture(move)
            and not board.is_check()
            and move.promotion is None
            and abs(we) < LOG_EVAL_MAX
        ):
            quiet_positions.append(board.fen())

        board.push(move)
        moves_history.append(bm_str)
        ply += 1

    if POS_CAP and len(quiet_positions) > POS_CAP:
        quiet_positions = random.sample(quiet_positions, POS_CAP)

    lines = [f"{fen};{result}" for fen in quiet_positions]
    with lock:
        out_fh.write("\n".join(lines) + ("\n" if lines else ""))
        out_fh.flush()
    stats.add_game(result, ply, len(lines), how)


def worker(worker_id, out_fh, lock, stats, stop_event):
    eng = None
    consecutive_failures = 0
    done = 0

    while not stop_event.is_set():
        with stats.lock:
            remaining = GAMES - stats.games
        if remaining <= 0:
            break

        try:
            if eng is None:
                eng = Engine()
            play_game(eng, out_fh, lock, stats)
            done += 1
            consecutive_failures = 0
            if done % 25 == 0:
                print(f"Worker {worker_id}: {done} games", flush=True)
        except Exception as e:
            print(f"Worker {worker_id}: error ({e}), restarting engine", flush=True)
            if eng is not None:
                eng.close()
            eng = None
            stats.add_restart()
            consecutive_failures += 1
            if consecutive_failures >= 10:
                print(f"Worker {worker_id}: too many consecutive failures, giving up", flush=True)
                stop_event.set()
                break

    if eng is not None:
        eng.close()


def main():
    lock = threading.Lock()
    stats = Stats()
    stop_event = threading.Event()

    mode = "a" if os.path.exists(OUT) else "w"
    existing = os.path.getsize(OUT) // 72 if mode == "a" else 0
    out_fh = open(OUT, mode)

    print(
        f"Datagen: {GAMES} games | {THREADS} engines x 1 thread | {NODES} nodes/move\n"
        f"MultiPV={MULTIPV} pick={PICK_W} | record from ply {MIN_PLY_RECORD}, "
        f"|eval| < {LOG_EVAL_MAX}, cap/game {'none' if not POS_CAP else POS_CAP}\n"
        f"Adjudication: ply>={ADJUDICATE_PLY}, |eval|>={ADJUDICATE_EVAL} for {ADJUDICATE_CHAIN} plies\n"
        f"Output: {OUT} (mode '{mode}'"
        + (f", ~{existing} existing positions)" if existing else ")")
    )
    t0 = time.time()

    threads = [
        threading.Thread(target=worker, args=(i, out_fh, lock, stats, stop_event), daemon=True)
        for i in range(THREADS)
    ]
    try:
        for t in threads:
            t.start()
        while any(t.is_alive() for t in threads):
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nInterrupted: stopping workers (data already flushed is kept)")
        stop_event.set()
        for t in threads:
            t.join(timeout=15)
    finally:
        out_fh.close()
        stats.report()
        print(f"Wrote to {OUT} in {time.time() - t0:.0f}s")


if __name__ == "__main__":
    main()
