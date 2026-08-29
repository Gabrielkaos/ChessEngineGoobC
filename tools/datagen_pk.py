#!/usr/bin/env python3
"""Self-play dataset generation for GOOB PKNet (Pawn + King only).

Usage:
    python3 datagen_pk.py [nodes] [games] [threads] [out] [pos_cap]

    nodes        search nodes per move          (default 1000)
    games        total games across all threads (default 5000)
    threads      number of engine processes     (default 5)
    out          output file, FEN;result lines  (default dataset_pk.epd, appended)
    pos_cap      max quiet positions kept per game; 0 = keep all (default)

Output format is "FEN;result" (result from white's POV).

--- Reliability notes (fixed) ---
Two resource-leak bugs could hang the whole machine on long runs:
  1. Engine.close() could leave zombie processes: if the graceful
     "quit" + wait(timeout=2) failed, we called p.kill() but never
     called wait() again, so the process stayed a zombie and its
     stdin/stdout pipe fds were never explicitly closed. Over many
     engine restarts this leaks process-table entries and fds.
  2. Reads from the engine (read_until / search's bestmove loop) had
     no timeout. If an engine process ever wedged (never printed
     bestmove), the worker thread blocked forever on readline() -- no
     exception was raised, so the restart/failure-counting logic in
     worker() never kicked in, and the stuck engine process sat there
     consuming a process slot and RAM indefinitely.

Fix: all engine stdout reads now go through a select()-based
_readline() with a timeout (GOOB_ENGINE_TIMEOUT env var, default 60s)
that raises EngineTimeout, which worker() treats like any other
engine failure (close + restart + count toward consecutive_failures).
Engine.close() now unconditionally reaps the process and closes its
pipe file objects even after a forced kill().

  3. (Follow-up fix) The first cut of the timeout used select() on a
     text-mode, line-buffered stdout stream. TextIOWrapper does its
     own internal buffering, so a single underlying read could pull
     several engine lines into Python's buffer at once -- leaving
     nothing at the OS level for select() to see on the next call,
     which then falsely timed out even though a line was already
     sitting in the buffer ready to read. Engine stdout is now opened
     unbuffered/binary and read via os.read() with a hand-rolled line
     buffer, so select() and the actual reads operate on the same
     (raw fd) level and can't disagree about what's "ready".
"""

import os
import random
import select
import subprocess
import sys
import threading
import time
import chess

ENGINE = os.environ.get(
    "GOOB_ENGINE",
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src", "bin","linux", "GOOB-2.2-BETA-native"),
)
NODES = int(sys.argv[1]) if len(sys.argv) > 1 else 1000
GAMES = int(sys.argv[2]) if len(sys.argv) > 2 else 5000
THREADS = int(sys.argv[3]) if len(sys.argv) > 3 else 5
OUT = sys.argv[4] if len(sys.argv) > 4 else "dataset_pk.epd"
POS_CAP = int(sys.argv[5]) if len(sys.argv) > 5 else 0

MAX_PLY = 150
MIN_PLY_RECORD = 0       # record all for PK
LOG_EVAL_MAX = 2000
ADJUDICATE_PLY = 40
ADJUDICATE_EVAL = 800
ADJUDICATE_CHAIN = 4
MULTIPV = 3
PICK_W = [0.60, 0.25, 0.15]

# Max seconds to wait for any single line of engine output before treating
# the engine as hung. Node-limited searches should finish in well under a
# second at reasonable NPS, so this is a generous safety margin, not a
# tight budget. Override with GOOB_ENGINE_TIMEOUT if needed for very large
# node counts.
ENGINE_TIMEOUT = float(os.environ.get("GOOB_ENGINE_TIMEOUT", 60))


class EngineTimeout(Exception):
    """Raised when the engine produces no output within ENGINE_TIMEOUT."""
    pass


class Engine:
    def __init__(self):
        engine_bin_dir = os.path.dirname(ENGINE)
        engine_src_dir = os.path.abspath(os.path.join(engine_bin_dir, ".."))

        self.p = subprocess.Popen(
            [ENGINE],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            bufsize=0,          # unbuffered, binary -- see _readline() below
            cwd=engine_src_dir,
            start_new_session=True
        )
        self._buf = b""         # our own line buffer, read at the same
                                 # level select() operates on
        self.send("uci")
        self.read_until("uciok")
        self.send("setoption name Threads value 1")
        self.send("setoption name Hash value 16")
        self.send("setoption name EvalHash value 4")
        self.send(f"setoption name MultiPV value {MULTIPV}")
        self.isready()

    def send(self, line):
        self.p.stdin.write((line + "\n").encode())
        self.p.stdin.flush()

    def _readline(self, timeout=ENGINE_TIMEOUT):
        """Read one line from engine stdout, with a hang-detection timeout.

        Reads raw bytes directly off the fd via os.read(), buffered by hand,
        rather than going through a TextIOWrapper's readline(). That wrapper
        does its own internal buffering, which can silently pull multiple
        lines into its buffer on one underlying read -- so a select() call
        on the fd afterward sees nothing waiting at the OS level and times
        out even though a full line is already sitted in Python's buffer.
        Keeping select() and the actual read on the same (raw fd) level
        avoids that false-timeout mismatch entirely.
        """
        fd = self.p.stdout
        while b"\n" not in self._buf:
            ready, _, _ = select.select([fd], [], [], timeout)
            if not ready:
                raise EngineTimeout(
                    f"No engine output for {timeout:.0f}s (pid {self.p.pid}); "
                    f"treating as hung"
                )
            chunk = os.read(fd.fileno(), 65536)
            if chunk == b"":
                raise EOFError("Engine crashed or closed stdout")
            self._buf += chunk
            if len(self._buf) > 10 * 1024 * 1024:
                raise RuntimeError("Engine output buffer exceeded 10MB")
        line, self._buf = self._buf.split(b"\n", 1)
        return line.decode(errors="replace").strip()

    def read_until(self, token, timeout=ENGINE_TIMEOUT):
        while True:
            line_str = self._readline(timeout)
            if token in line_str:
                return line_str

    def isready(self):
        self.send("isready")
        self.read_until("readyok")

    def newgame(self):
        self.send("ucinewgame")
        self.isready()

    def search(self, poscmd, nodes, timeout=ENGINE_TIMEOUT):
        self.send(poscmd)
        self.send(f"go nodes {nodes}")

        pvs = {}
        bm = None
        eval_score = 0
        start_time = time.time()

        while True:
            if time.time() - start_time > timeout * 2:
                raise EngineTimeout("Engine search exceeded absolute timeout")
            
            line = self._readline(timeout)

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
        # Always try graceful shutdown first.
        try:
            self.send("quit")
            self.p.wait(timeout=2)
        except Exception:
            try:
                os.killpg(self.p.pid, 9)
            except Exception:
                try:
                    self.p.kill()
                except Exception:
                    pass
        finally:
            # Unconditionally reap, even after a forced kill(), so we never
            # leave a zombie process behind -- this was the main source of
            # the long-run resource leak.
            try:
                self.p.wait(timeout=2)
            except Exception:
                pass
            # Explicitly close the pipe file objects rather than relying on
            # GC/__del__ timing to release the fds.
            for f in (self.p.stdin, self.p.stdout):
                try:
                    if f is not None:
                        f.close()
                except Exception:
                    pass


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.games = 0
        self.wdl = {1.0: 0, 0.5: 0, 0.0: 0}
        self.adjudicated = 0
        self.natural_endings = 0
        self.timeouts = 0
        self.hangs = 0
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

    def add_restart(self, hang=False):
        with self.lock:
            self.restarts += 1
            if hang:
                self.hangs += 1

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
                f"Engine restarts (crashes): {self.restarts} (of which hangs: {self.hangs})"
            )


def generate_random_pk_fen():
    """Generates a random valid Pawn+King endgame FEN."""
    while True:
        board = chess.Board(None) # empty board

        wk_sq = random.randint(0, 63)
        bk_sq = random.randint(0, 63)

        # Kings cannot be adjacent or on same square
        if bk_sq == wk_sq or chess.square_distance(wk_sq, bk_sq) <= 1:
            continue

        board.set_piece_at(wk_sq, chess.Piece(chess.KING, chess.WHITE))
        board.set_piece_at(bk_sq, chess.Piece(chess.KING, chess.BLACK))

        # 1 to 5 pawns total
        num_pawns = random.randint(1, 5)
        available_squares = list(set(range(8, 56)) - {wk_sq, bk_sq})

        if len(available_squares) < num_pawns:
            continue

        pawn_squares = random.sample(available_squares, num_pawns)
        for sq in pawn_squares:
            color = chess.WHITE if random.random() > 0.5 else chess.BLACK
            board.set_piece_at(sq, chess.Piece(chess.PAWN, color))

        board.turn = chess.WHITE if random.random() > 0.5 else chess.BLACK

        if board.is_valid():
            board.halfmove_clock = 0
            board.fullmove_number = 1
            return board.fen()


def play_game(eng, out_fh, lock, stats):
    start_fen = generate_random_pk_fen()
    board = chess.Board(start_fen)
    moves_history = []
    quiet_positions = []

    ply = 0
    result = None
    how = "natural"
    win_chain = 0

    eng.newgame()

    while True:
        if board.is_insufficient_material() or board.is_repetition(3) or board.halfmove_clock >= 100:
            result = 0.5
            break

        cmd = f"position fen {start_fen}"
        if moves_history:
            cmd += " moves " + " ".join(moves_history)

        bm_str, eval_score = eng.search(cmd, NODES)

        if bm_str is None or bm_str in ("(none)", "0000"):
            if board.is_check():
                result = 0.0 if board.turn == chess.WHITE else 1.0
            else:
                result = 0.5
            break
        try:
            move = board.parse_uci(bm_str)
        except ValueError:
            raise RuntimeError(f"Illegal/unknown bestmove '{bm_str}' at {board.fen()}")

        we = eval_score if board.turn == chess.WHITE else -eval_score

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

        has_promotion_piece = any(len(board.pieces(pt, c)) > 0
                                  for pt in [chess.KNIGHT, chess.BISHOP, chess.ROOK, chess.QUEEN]
                                  for c in [chess.WHITE, chess.BLACK])

        if (
            not has_promotion_piece
            and ply >= MIN_PLY_RECORD
            and not board.is_capture(move)
            and not board.is_check()
            and move.promotion is None
            and abs(we) < LOG_EVAL_MAX
        ):
            quiet_positions.append(board.fen())

        board.push(move)
        moves_history.append(bm_str)
        ply += 1

    from evaluation import evalFen
    
    if POS_CAP and len(quiet_positions) > POS_CAP:
        quiet_positions = random.sample(quiet_positions, POS_CAP)

    lines = []
    for fen in quiet_positions:
        mg, eg = evalFen(fen)
        lines.append(f"{fen};{mg};{eg}")

    with lock:
        if lines:
            out_fh.write("\n".join(lines) + "\n")
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
            if stop_event.is_set():
                break
            is_hang = isinstance(e, EngineTimeout)
            kind = "hung" if is_hang else "error"
            print(f"Worker {worker_id}: {kind} ({e}), restarting engine", flush=True)
            if eng is not None:
                eng.close()
            eng = None
            stats.add_restart(hang=is_hang)
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
    if mode == "a" and os.path.getsize(OUT) > 0:
        with open(OUT, "rb") as chk:
            chk.seek(-1, os.SEEK_END)
            if chk.read(1) != b"\n":
                out_fh.write("\n")
                out_fh.flush()
                print("Repaired missing trailing newline in output file")

    print(
        f"PK Datagen: {GAMES} games | {THREADS} engines x 1 thread | {NODES} nodes/move\n"
        f"MultiPV={MULTIPV} pick={PICK_W} | record from ply {MIN_PLY_RECORD}, "
        f"|eval| < {LOG_EVAL_MAX}, cap/game {'none' if not POS_CAP else POS_CAP}\n"
        f"Adjudication: ply>={ADJUDICATE_PLY}, |eval|>={ADJUDICATE_EVAL} for {ADJUDICATE_CHAIN} plies\n"
        f"Engine read timeout: {ENGINE_TIMEOUT:.0f}s (override with GOOB_ENGINE_TIMEOUT)\n"
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