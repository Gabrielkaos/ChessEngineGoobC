#!/usr/bin/env python3
"""
Self-play dataset generation for GOOB NNUE training.

Plays self-play games starting from an opening book (e.g. UHO unbalanced human openings),
evaluates positions using the engine, and filters positions strictly matching the
filtering logic and defaults in tools/nnue_project/scripts/prepare_data.py.

Data is formatted directly into 68-byte packed binary records (train/val .bin files)
ready for training with tools/nnue_project/scripts/train.py.

Key Filtering (copied from prepare_data.py defaults):
  - Minimum search depth: 20 (rejects depth < 20)
  - No mate scores (forced mate scores are excluded unless --keep-mate is passed)
  - Max centipawn eval: 3000 (positions with |cp| > 3000 are rejected as lopsided)
  - Quiet positions only:
      * Not in check (board.is_check() == False)
      * Not a capture (board.is_capture(move) == False)
      * Not a promotion (move.promotion is None)
      * Skip initial book plies (ply >= min_ply)
  - Per-game position cap: samples a bounded number of positions per game
    to prevent over-representation of long endgames.

Usage examples:
    # Generate 50,000 train records and 1,000 val records at depth 20:
    python3 tools/selfplay_nnue.py --n-train 50000 --n-val 1000 --depth 20 --workers 8

    # Fast game rollouts with 5,000 nodes, rescoring quiet positions at depth 20:
    python3 tools/selfplay_nnue.py --play-nodes 5000 --depth 20 --n-train 100000

    # Custom opening book and output directory:
    python3 tools/selfplay_nnue.py --book ~/Desktop/engines/UHO_4060_v4.epd --out-dir tools/nnue_project/data
"""

import argparse
import os
import random
import select
import signal
import struct
import subprocess
import sys
import threading
import time
import traceback

# Auto-re-exec in project virtualenv if python-chess is missing
try:
    import chess
except ImportError:
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    venv_py = os.path.join(repo_root, "tools", ".venv", "bin", "python3")
    if os.path.exists(venv_py) and sys.executable != venv_py:
        os.execv(venv_py, [venv_py] + sys.argv)
    else:
        sys.stderr.write("Error: python-chess is required. Install with: pip install chess\n")
        sys.exit(1)

try:
    from tqdm import tqdm
    HAVE_TQDM = True
except ImportError:
    HAVE_TQDM = False

# Import 68-byte record packing from fen_utils if available, else use embedded implementation
_SCRIPTS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "nnue_project", "scripts")
if _SCRIPTS_DIR not in sys.path:
    sys.path.insert(0, _SCRIPTS_DIR)

try:
    from fen_utils import pack_record, unpack_record, RECORD_SIZE, fen_to_board_and_stm
except ImportError:
    PIECE_CODE = {
        "P": 1, "N": 2, "B": 3, "R": 4, "Q": 5, "K": 6,
        "p": 7, "n": 8, "b": 9, "r": 10, "q": 11, "k": 12,
    }
    RECORD_STRUCT = struct.Struct("<64sBhB")
    RECORD_SIZE = RECORD_STRUCT.size  # 68
    _pack = RECORD_STRUCT.pack
    _unpack = RECORD_STRUCT.unpack

    def fen_to_board_and_stm(fen: str):
        space = fen.find(" ")
        if space == -1:
            placement = fen
            stm = 0
        else:
            placement = fen[:space]
            stm = 0 if (len(fen) <= space + 1 or fen[space + 1] == "w") else 1

        board = bytearray(64)
        rank = 7
        file = 0
        for ch in placement:
            if ch == "/":
                rank -= 1
                file = 0
            elif "1" <= ch <= "8":
                file += ord(ch) - 48
            else:
                board[rank * 8 + file] = PIECE_CODE[ch]
                file += 1
        return board, stm

    def pack_record(fen: str, cp, mate) -> bytes:
        board, stm = fen_to_board_and_stm(fen)
        if mate is not None:
            is_mate = 1
            eval_cp = 3000 if mate > 0 else -3000
        else:
            is_mate = 0
            eval_cp = int(cp)
            if eval_cp > 3000:
                eval_cp = 3000
            elif eval_cp < -3000:
                eval_cp = -3000
        return _pack(bytes(board), stm, eval_cp, is_mate)

    def unpack_record(record: bytes):
        board, stm, eval_cp, is_mate = _unpack(record)
        return board, stm, eval_cp, bool(is_mate)


FLUSH_BYTES = 4 * 1024 * 1024  # Flush buffer every 4MB
ENGINE_TIMEOUT = float(os.environ.get("GOOB_ENGINE_TIMEOUT", 120))  # seconds


class EngineTimeout(Exception):
    pass


class Engine:
    """Manages a single UCI engine process with non-blocking I/O and timeouts."""

    def __init__(self, engine_path: str, cwd: str, hash_mb: int = 16, eval_hash_mb: int = 4):
        self.engine_path = engine_path
        self.cwd = cwd
        self.p = subprocess.Popen(
            [self.engine_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            bufsize=0,
            cwd=self.cwd,
            start_new_session=True,
        )
        self._buf = b""

        self.send("uci")
        self.read_until("uciok")
        self.send("setoption name Threads value 1")
        self.send(f"setoption name Hash value {hash_mb}")
        self.send(f"setoption name EvalHash value {eval_hash_mb}")
        self.isready()

    def send(self, line: str):
        self.p.stdin.write((line + "\n").encode())
        self.p.stdin.flush()

    def _readline(self, timeout=ENGINE_TIMEOUT):
        fd = self.p.stdout
        while b"\n" not in self._buf:
            ready, _, _ = select.select([fd], [], [], timeout)
            if not ready:
                raise EngineTimeout(
                    f"No engine output for {timeout:.0f}s (pid {self.p.pid}); treating as hung"
                )
            chunk = os.read(fd.fileno(), 65536)
            if chunk == b"":
                raise EOFError("Engine crashed or closed stdout")
            self._buf += chunk
            if len(self._buf) > 20 * 1024 * 1024:
                raise RuntimeError("Engine output buffer exceeded 20MB")
        line, self._buf = self._buf.split(b"\n", 1)
        return line.decode(errors="replace").strip()

    def read_until(self, token: str, timeout=ENGINE_TIMEOUT):
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

    def search(self, poscmd: str, depth: int = None, nodes: int = None, timeout=ENGINE_TIMEOUT):
        """Searches a position. Returns (chosen_move_uci, best_depth, best_cp, best_mate).

        best_cp and best_mate are from the side-to-move's perspective (standard UCI convention).
        """
        self.send(poscmd)
        if nodes is not None and nodes > 0:
            self.send(f"go nodes {nodes}")
        elif depth is not None and depth > 0:
            self.send(f"go depth {depth}")
        else:
            self.send("go depth 20")

        best_depth = 0
        best_cp = None
        best_mate = None
        bm = None
        start_time = time.time()

        while True:
            if time.time() - start_time > timeout * 2:
                raise EngineTimeout("Engine search exceeded absolute timeout")

            line = self._readline(timeout)

            if line.startswith("info"):
                tokens = line.split()
                try:
                    if "depth" in tokens:
                        d = int(tokens[tokens.index("depth") + 1])
                        if d > best_depth:
                            best_depth = d
                    if "score" in tokens:
                        s_idx = tokens.index("score")
                        if tokens[s_idx + 1] == "cp":
                            best_cp = int(tokens[s_idx + 2])
                            best_mate = None
                        elif tokens[s_idx + 1] == "mate":
                            best_mate = int(tokens[s_idx + 2])
                            best_cp = None
                except (ValueError, IndexError):
                    pass
            elif line.startswith("bestmove"):
                tokens = line.split()
                if len(tokens) >= 2:
                    bm = tokens[1]
                break

        chosen = bm if bm and bm not in ("(none)", "0000") else None
        return chosen, best_depth, best_cp, best_mate

    def close(self):
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
            try:
                self.p.wait(timeout=2)
            except Exception:
                pass
            for f in (self.p.stdin, self.p.stdout):
                try:
                    if f is not None:
                        f.close()
                except Exception:
                    pass

    def kill(self):
        """Immediately kills the engine subprocess without waiting for graceful quit."""
        try:
            self.p.kill()
        except Exception:
            pass
        finally:
            try:
                self.p.wait(timeout=1)
            except Exception:
                pass
            for f in (self.p.stdin, self.p.stdout):
                try:
                    if f is not None:
                        f.close()
                except Exception:
                    pass


class Stats:
    """Thread-safe statistics collector."""

    def __init__(self, initial_train: int = 0, initial_val: int = 0):
        self.lock = threading.Lock()
        self.games = 0
        self.total_ply = 0
        self.positions_inspected = 0
        self.filtered_non_quiet = 0
        self.filtered_shallow_depth = 0
        self.filtered_mate = 0
        self.filtered_lopsided = 0
        self.initial_train = initial_train
        self.initial_val = initial_val
        self.train_written = initial_train
        self.val_written = initial_val
        self.restarts = 0
        self.t0 = time.time()

    def record_filters(self, inspected, non_quiet, shallow, mate, lopsided):
        with self.lock:
            self.positions_inspected += inspected
            self.filtered_non_quiet += non_quiet
            self.filtered_shallow_depth += shallow
            self.filtered_mate += mate
            self.filtered_lopsided += lopsided

    def add_ply(self):
        with self.lock:
            self.total_ply += 1

    def add_game(self, n_train, n_val):
        with self.lock:
            self.games += 1
            self.train_written += n_train
            self.val_written += n_val

    def add_restart(self):
        with self.lock:
            self.restarts += 1

    def summary(self):
        with self.lock:
            dt = max(time.time() - self.t0, 0.001)
            g = max(self.games, 1)
            new_train = self.train_written - self.initial_train
            new_val = self.val_written - self.initial_val
            tot_new = new_train + new_val
            rate = tot_new / dt
            games_h = (self.games / dt) * 3600
            return (
                f"\n{'=' * 65}\n"
                f"=== NNUE Self-Play Data Generation Summary ===\n"
                f"{'=' * 65}\n"
                f"Games completed:        {self.games:,} ({dt:.1f}s, {games_h:,.0f} games/hour)\n"
                f"Average game length:    {self.total_ply / g:.1f} plies\n"
                f"Positions inspected:    {self.positions_inspected:,}\n"
                f"  - Non-quiet (check/cap/promo): {self.filtered_non_quiet:,}\n"
                f"  - Shallow depth (< min_depth): {self.filtered_shallow_depth:,}\n"
                f"  - Mate scores (skipped):       {self.filtered_mate:,}\n"
                f"  - Lopsided (|cp| > max_cp):    {self.filtered_lopsided:,}\n"
                f"Positions recorded:     +{tot_new:,} ({tot_new / g:.1f} / game this session)\n"
                f"  - Train records:      {self.train_written:,} total (+{new_train:,} added)\n"
                f"  - Val records:        {self.val_written:,} total (+{new_val:,} added)\n"
                f"Overall rate:           {rate:,.1f} positions/sec\n"
                f"Engine restarts:        {self.restarts}\n"
                f"{'=' * 65}"
            )


def find_default_book() -> str:
    """Finds UHO dataset or fallback book."""
    p = os.path.expanduser("~/Desktop/engines/UHO_4060_v4.epd")
    if os.path.exists(p):
        return p

    engines_dir = os.path.expanduser("~/Desktop/engines")
    if os.path.isdir(engines_dir):
        for f in os.listdir(engines_dir):
            if "uho" in f.lower() and (f.endswith(".epd") or f.endswith(".fen")):
                return os.path.join(engines_dir, f)
        for f in os.listdir(engines_dir):
            if f.endswith(".epd") or f.endswith(".fen"):
                return os.path.join(engines_dir, f)

    tools_book = os.path.join(os.path.dirname(os.path.abspath(__file__)), "book.epd")
    if os.path.exists(tools_book):
        return tools_book

    return ""


def find_default_engine() -> str:
    """Finds GOOB engine binary."""
    env = os.environ.get("GOOB_ENGINE")
    if env and os.path.exists(env):
        return env

    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    repo_eng = os.path.join(repo_root, "src", "bin", "linux", "GOOB-2.2-BETA-native")
    if os.path.exists(repo_eng):
        return repo_eng

    desktop_new = os.path.expanduser("~/Desktop/engines/GOOB-2.2-BETA-native-NEW")
    if os.path.exists(desktop_new):
        return desktop_new

    desktop_old = os.path.expanduser("~/Desktop/engines/GOOB-2.2-BETA-native-OLD")
    if os.path.exists(desktop_old):
        return desktop_old

    return "GOOB-2.2-BETA-native"


def load_opening_book(book_path: str):
    """Loads and validates opening FENs from the book file."""
    if not book_path or not os.path.exists(book_path):
        print("Notice: No opening book found. Using standard starting position.")
        return [chess.STARTING_FEN]

    openings = []
    with open(book_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            # EPD line can contain trailing operations; extract the 4 to 6 FEN tokens
            tokens = line.split()
            if len(tokens) >= 4:
                fen_candidate = " ".join(tokens[:4]) if len(tokens) < 6 else " ".join(tokens[:6])
                openings.append(fen_candidate)
            else:
                openings.append(line)

    print(f"Loaded {len(openings):,} opening positions from {book_path}")
    return openings


def play_game(
    eng: Engine,
    openings: list,
    args,
    stats: Stats,
    rng: random.Random,
    f_train,
    f_val,
    train_buf: bytearray,
    val_buf: bytearray,
    buf_lock: threading.Lock,
    pbar,
    epd_fh=None,
):
    """Plays one self-play game, filters candidate positions, and writes packed records."""
    start_fen = rng.choice(openings)
    try:
        board = chess.Board(start_fen)
    except Exception:
        # Fallback to startpos if malformed line
        board = chess.Board()
        start_fen = chess.STARTING_FEN

    eng.newgame()
    moves_history = []
    candidate_records = []      # for direct mode: (fen, cp_white, mate_white)
    candidate_quiet_fens = []   # for play-nodes mode: list of fens to rescore

    ply = 0
    win_chain = 0
    draw_chain = 0

    # Track filter counts for this game
    inspected = 0
    non_quiet = 0
    shallow = 0
    mate_count = 0
    lopsided = 0

    while ply < args.max_ply:
        # 1. Rule-based draw checks
        if board.is_insufficient_material() or board.is_repetition(3) or board.halfmove_clock >= 100:
            break

        # 2. Build position command
        poscmd = f"position fen {start_fen}"
        if moves_history:
            poscmd += " moves " + " ".join(moves_history)

        # 3. Search move
        # If --play-nodes is set, use node limit for playing moves, then rescore quiet candidates at depth
        if args.play_nodes > 0:
            move_str, depth, cp, mate = eng.search(poscmd, nodes=args.play_nodes)
        else:
            move_str, depth, cp, mate = eng.search(poscmd, depth=args.depth)

        if move_str is None or move_str in ("(none)", "0000"):
            break

        try:
            move = board.parse_uci(move_str)
        except ValueError:
            break

        # Calculate White-POV centipawn score
        if cp is not None:
            cp_white = cp if board.turn == chess.WHITE else -cp
        else:
            cp_white = None

        if mate is not None:
            mate_white = mate if board.turn == chess.WHITE else -mate
        else:
            mate_white = None

        # 4. Adjudication check (White POV score)
        if cp_white is not None:
            # Resignation adjudication
            if ply >= args.adjudicate_ply:
                if cp_white >= args.adjudicate_eval:
                    win_chain = win_chain + 1 if win_chain > 0 else 1
                elif cp_white <= -args.adjudicate_eval:
                    win_chain = win_chain - 1 if win_chain < 0 else -1
                else:
                    win_chain = 0

                if abs(win_chain) >= args.adjudicate_chain:
                    break

            # Draw adjudication (matches cutechess-cli convention: small eval for consecutive plies in late game)
            if ply >= args.draw_ply:
                if abs(cp_white) <= args.draw_eval:
                    draw_chain += 1
                else:
                    draw_chain = 0

                if draw_chain >= args.draw_chain:
                    break

        # 5. Position candidate filtering
        # Check quiet criteria first (not check, not capture, not promotion, ply >= min_ply)
        inspected += 1
        is_quiet = (
            ply >= args.min_ply
            and not board.is_check()
            and not board.is_capture(move)
            and move.promotion is None
        )

        if not is_quiet:
            non_quiet += 1
        else:
            current_fen = board.fen()
            if args.rescore:
                # Rescore mode: defer depth evaluation until end of game
                candidate_quiet_fens.append(current_fen)
            else:
                # Fast single-pass mode (like Schoenemann / Berserk):
                # use cp and mate directly from the move search!
                if depth is not None and depth < args.min_depth:
                    shallow += 1
                elif mate is not None:
                    if not args.keep_mate:
                        mate_count += 1
                    else:
                        candidate_records.append((current_fen, cp_white, mate_white))
                else:
                    if cp is None:
                        shallow += 1
                    elif abs(cp) > args.max_abs_cp:
                        lopsided += 1
                    else:
                        candidate_records.append((current_fen, cp_white, None))

        board.push(move)
        moves_history.append(move_str)
        ply += 1
        stats.add_ply()

    # In rescore mode: subsample quiet candidates FIRST, then evaluate only those at --depth
    if args.rescore and candidate_quiet_fens:
        if args.pos_cap > 0 and len(candidate_quiet_fens) > args.pos_cap:
            candidate_quiet_fens = rng.sample(candidate_quiet_fens, args.pos_cap)

        for fen in candidate_quiet_fens:
            eval_cmd = f"position fen {fen}"
            _, depth, cp, mate = eng.search(eval_cmd, depth=args.depth)
            b_fen = chess.Board(fen)
            cp_white = cp if b_fen.turn == chess.WHITE else -cp if cp is not None else None
            mate_white = mate if b_fen.turn == chess.WHITE else -mate if mate is not None else None

            # Apply prepare_data.py filters:
            if depth is not None and depth < args.min_depth:
                shallow += 1
            elif mate is not None:
                if not args.keep_mate:
                    mate_count += 1
                else:
                    candidate_records.append((fen, cp_white, mate_white))
            else:
                if cp is None:
                    shallow += 1
                elif abs(cp) > args.max_abs_cp:
                    lopsided += 1
                else:
                    candidate_records.append((fen, cp_white, None))
    elif args.pos_cap > 0 and len(candidate_records) > args.pos_cap:
        candidate_records = rng.sample(candidate_records, args.pos_cap)

    stats.record_filters(inspected, non_quiet, shallow, mate_count, lopsided)

    if not candidate_records:
        stats.add_game(0, 0)
        return

    # Pack records and route to train or val split
    packed_train = bytearray()
    packed_val = bytearray()
    epd_lines = []

    for fen, c_w, m_w in candidate_records:
        rec = pack_record(fen, c_w, m_w)
        # Random draw matching prepare_data.py logic
        r = random.random()
        with buf_lock:
            val_needed = stats.val_written < args.target_val
            train_needed = stats.train_written < args.target_train

            if val_needed and (not train_needed or r < args.val_fraction):
                packed_val += rec
                stats.val_written += 1
            elif train_needed:
                packed_train += rec
                stats.train_written += 1
                if pbar is not None:
                    pbar.update(1)

        if epd_fh is not None:
            epd_lines.append(f"{fen};{c_w}")

    # Write records directly to disk under shared buffer lock
    with buf_lock:
        if packed_train:
            f_train.write(packed_train)
            f_train.flush()
        if packed_val:
            f_val.write(packed_val)
            f_val.flush()

        if epd_fh is not None and epd_lines:
            epd_fh.write("\n".join(epd_lines) + "\n")
            epd_fh.flush()

    stats.add_game(len(packed_train) // RECORD_SIZE, len(packed_val) // RECORD_SIZE)


active_engines = set()
engines_lock = threading.Lock()


def worker_loop(
    worker_id: int,
    openings: list,
    args,
    stats: Stats,
    stop_event: threading.Event,
    f_train,
    f_val,
    train_buf: bytearray,
    val_buf: bytearray,
    buf_lock: threading.Lock,
    pbar,
    epd_fh=None,
):
    """Worker thread running an independent engine process."""
    eng = None
    consecutive_failures = 0
    rng = random.Random(args.seed + worker_id * 7919)

    while not stop_event.is_set():
        with stats.lock:
            if stats.train_written >= args.target_train and stats.val_written >= args.target_val:
                stop_event.set()
                break
            if args.games > 0 and stats.games >= args.games:
                stop_event.set()
                break

        try:
            if eng is None:
                eng = Engine(
                    engine_path=args.engine,
                    cwd=args.engine_cwd,
                    hash_mb=args.hash,
                    eval_hash_mb=args.eval_hash,
                )
                with engines_lock:
                    active_engines.add(eng)
            play_game(
                eng=eng,
                openings=openings,
                args=args,
                stats=stats,
                rng=rng,
                f_train=f_train,
                f_val=f_val,
                train_buf=train_buf,
                val_buf=val_buf,
                buf_lock=buf_lock,
                pbar=pbar,
                epd_fh=epd_fh,
            )
            consecutive_failures = 0
        except Exception as e:
            if stop_event.is_set():
                break
            stats.add_restart()
            consecutive_failures += 1
            if eng is not None:
                with engines_lock:
                    active_engines.discard(eng)
                eng.close()
                eng = None

            if consecutive_failures >= 10:
                print(f"[Worker {worker_id}] Exceeded 10 consecutive failures ({e}). Stopping worker.", flush=True)
                break
            time.sleep(0.5)

    if eng is not None:
        with engines_lock:
            active_engines.discard(eng)
        eng.close()


def main():
    parser = argparse.ArgumentParser(
        description="Self-play dataset generator for NNUE training with prepare_data.py filtering logic.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    # Output paths
    parser.add_argument("--out-dir", default="tools/nnue_project/data", help="Output directory for train and val .bin files")
    parser.add_argument("--train-file", default=None, help="Custom filename for train binary (default: train1.bin)")
    parser.add_argument("--val-file", default=None, help="Custom filename for val binary (default: val1.bin)")
    parser.add_argument("--append", action="store_true", help="Append to existing output files instead of overwriting")
    parser.add_argument("--add", action="store_true", help="If appending, add --n-train and --n-val on top of existing records instead of targeting total records in file")
    parser.add_argument("--epd-out", default=None, help="Optional text EPD output path (FEN;cp)")

    # Data collection targets (matching prepare_data.py)
    parser.add_argument("--n-train", type=int, default=1_000_000, help="Target training positions (or positions to add if --add)")
    parser.add_argument("--n-val", type=int, default=10_000, help="Target validation positions (or positions to add if --add)")
    parser.add_argument("--val-fraction", type=float, default=0.01, help="Fraction of positions routed to validation (auto-calculated from targets if left at default)")
    parser.add_argument("--games", type=int, default=0, help="Stop after N games (0 = stop when n-train and n-val reached)")

    # Search & evaluation filters
    parser.add_argument("--depth", type=int, default=16, help="Engine search depth (used if --play-nodes 0, or if --rescore is enabled)")
    parser.add_argument("--min-depth", type=int, default=0, help="Minimum search depth required to keep a position (default: 0)")
    parser.add_argument("--max-abs-cp", type=int, default=3000, help="Maximum absolute centipawn eval to keep (default: 3000)")
    parser.add_argument("--keep-mate", action="store_true", help="Include forced mate positions (default: False)")

    # Self-play configuration
    parser.add_argument("--play-nodes", type=int, default=5000, help="Node limit per move (default: 5000, set to 0 for depth search)")
    parser.add_argument("--rescore", action="store_true", help="Rescore quiet positions at --depth at end of game (slow). Default is single-pass (fast).")
    parser.add_argument("--book", default=None, help="Path to opening book (.epd / .fen). Auto-detects UHO_4060_v4.epd if omitted")
    parser.add_argument("--engine", default=None, help="Path to engine binary. Auto-detects GOOB binary if omitted")
    parser.add_argument("--engine-cwd", default=None, help="Engine working directory (containing weights/). Auto-detected if omitted")
    parser.add_argument("--workers", type=int, default=min(os.cpu_count() or 4, 16), help="Number of concurrent engine processes")
    parser.add_argument("--hash", type=int, default=16, help="Hash table size in MB per engine worker")
    parser.add_argument("--eval-hash", type=int, default=4, help="Eval hash size in MB per engine worker")
    parser.add_argument("--pos-cap", type=int, default=16, help="Max quiet positions kept per game (0 = keep all)")
    parser.add_argument("--min-ply", type=int, default=4, help="Plies to skip after opening book before recording")
    parser.add_argument("--max-ply", type=int, default=160, help="Maximum plies per game before draw termination")
    parser.add_argument("--adjudicate-ply", type=int, default=40, help="Minimum ply to start win/loss adjudication")
    parser.add_argument("--adjudicate-eval", type=int, default=600, help="Centipawn threshold for win/loss adjudication (|cp| >= threshold)")
    parser.add_argument("--adjudicate-chain", type=int, default=4, help="Consecutive plies required for win/loss adjudication")
    parser.add_argument("--draw-ply", type=int, default=60, help="Minimum ply to start draw adjudication")
    parser.add_argument("--draw-eval", type=int, default=15, help="Centipawn threshold for draw adjudication (|cp| <= threshold)")
    parser.add_argument("--draw-chain", type=int, default=6, help="Consecutive plies required for draw adjudication")
    parser.add_argument("--seed", type=int, default=42, help="Random number generator seed")
    parser.add_argument("--progress-every", type=int, default=1000, help="Print progress update every N positions if tqdm is not installed")

    args = parser.parse_args()
    random.seed(args.seed)

    # Resolve opening book
    if not args.book:
        args.book = find_default_book()
    openings = load_opening_book(args.book)

    # Resolve engine binary and working directory
    if not args.engine:
        args.engine = find_default_engine()
    args.engine = os.path.abspath(args.engine)
    if not os.path.exists(args.engine):
        sys.stderr.write(f"Error: Engine binary not found at '{args.engine}'. Use --engine to specify.\n")
        sys.exit(1)

    if not args.engine_cwd:
        # Default to repo src directory so weights/quantised.bin is resolved properly
        repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        args.engine_cwd = os.path.join(repo_root, "src")
        if not os.path.exists(args.engine_cwd):
            args.engine_cwd = os.path.dirname(args.engine)

    # Prepare output files
    os.makedirs(args.out_dir, exist_ok=True)
    train_filename = args.train_file or "train1.bin"
    val_filename = args.val_file or "val1.bin"
    train_path = os.path.join(args.out_dir, train_filename)
    val_path = os.path.join(args.out_dir, val_filename)

    file_mode = "ab" if args.append else "wb"

    # Pre-alignment check if appending
    for path in (train_path, val_path):
        if args.append and os.path.exists(path):
            fsize = os.path.getsize(path)
            rem = fsize % RECORD_SIZE
            if rem != 0:
                with open(path, "a+b") as fix_f:
                    fix_f.truncate(fsize - rem)
                print(f"[Safeguard] Truncated {rem} trailing bytes from {path} for clean record alignment.")

    existing_train = (os.path.getsize(train_path) // RECORD_SIZE) if (args.append and os.path.exists(train_path)) else 0
    existing_val = (os.path.getsize(val_path) // RECORD_SIZE) if (args.append and os.path.exists(val_path)) else 0

    if args.append and not args.add:
        args.target_train = max(existing_train, args.n_train)
        args.target_val = max(existing_val, args.n_val)
    else:
        args.target_train = existing_train + args.n_train
        args.target_val = existing_val + args.n_val

    rem_train = max(0, args.target_train - existing_train)
    rem_val = max(0, args.target_val - existing_val)
    rem_total = rem_train + rem_val

    # Auto-balance val_fraction based on remaining needed counts if default
    if rem_total > 0 and args.val_fraction == 0.01:
        args.val_fraction = max(0.001, min(0.999, rem_val / rem_total))

    if args.append and rem_train == 0 and rem_val == 0:
        print(
            f"\n[Target Already Reached] {existing_train:,} train and {existing_val:,} val records already exist in:\n"
            f"  {train_path}\n"
            f"  {val_path}\n"
            f"To generate more records on top of existing data, use --add or specify higher --n-train / --n-val.\n",
            flush=True,
        )
        return

    print(
        f"\nStarting NNUE self-play data generation:\n"
        f"  Engine:       {args.engine} (cwd: {args.engine_cwd})\n"
        f"  Workers:      {args.workers} concurrent engine processes\n"
        f"  Book:         {args.book} ({len(openings):,} positions)\n"
        f"  Search:       {f'{args.play_nodes:,} nodes/move' if args.play_nodes > 0 else f'depth {args.depth}'}\n"
        f"  Play mode:    {f'Fast single-pass ({args.play_nodes:,} nodes/move)' if (args.play_nodes > 0 and not args.rescore) else (f'Fast play ({args.play_nodes} nodes) + depth {args.depth} rescore' if args.play_nodes > 0 else f'Direct at depth {args.depth}')}\n"
        f"  Filters:      quiet positions only, no mate ({'kept' if args.keep_mate else 'excluded'}), |cp| <= {args.max_abs_cp}, min_depth >= {args.min_depth}\n"
        f"  Outputs:      {train_path} ({existing_train:,} existing) | {val_path} ({existing_val:,} existing)\n"
        f"  Targets:      {args.target_train:,} train total (+{rem_train:,} to add) | {args.target_val:,} val total (+{rem_val:,} to add)\n"
        f"  Val fraction: {args.val_fraction:.1%} of remaining records routed to validation\n"
    )

    stats = Stats(initial_train=existing_train, initial_val=existing_val)
    stop_event = threading.Event()
    buf_lock = threading.Lock()
    train_buf = bytearray()
    val_buf = bytearray()

    pbar = tqdm(initial=existing_train, total=args.target_train, unit="rec", desc="train") if HAVE_TQDM else None

    f_train = open(train_path, file_mode)
    f_val = open(val_path, file_mode)
    epd_fh = open(args.epd_out, "a" if args.append else "w", encoding="utf-8") if args.epd_out else None

    threads = []
    for i in range(args.workers):
        t = threading.Thread(
            target=worker_loop,
            args=(i, openings, args, stats, stop_event, f_train, f_val, train_buf, val_buf, buf_lock, pbar, epd_fh),
            daemon=True,
        )
        t.start()
        threads.append(t)

    try:
        last_progress_time = time.time()
        last_train_count = 0

        while not stop_event.is_set():
            time.sleep(0.5)
            # Text progress reporting if tqdm is not active
            if pbar is None:
                now = time.time()
                if now - last_progress_time >= 5.0:
                    with stats.lock:
                        cur_train = stats.train_written
                        cur_val = stats.val_written
                        cur_games = stats.games
                        cur_plies = stats.total_ply
                    elapsed = now - stats.t0
                    added_train = cur_train - stats.initial_train
                    rate = added_train / elapsed if elapsed > 0 else 0
                    print(
                        f"Games: {cur_games:,} | Plies: {cur_plies:,} | Train: {cur_train:,}/{args.target_train:,} | "
                        f"Val: {cur_val:,}/{args.target_val:,} | Rate: {rate:,.0f} rec/s",
                        flush=True,
                    )
                    last_progress_time = now
            elif pbar is not None:
                with stats.lock:
                    pbar.set_postfix(
                        games=stats.games,
                        plies=stats.total_ply,
                        val=f"{stats.val_written}/{args.target_val}",
                        refresh=True,
                    )

            if not any(t.is_alive() for t in threads):
                break

    except KeyboardInterrupt:
        print("\n[Interrupted] Stopping workers gracefully and saving all generated data...", flush=True)
        stop_event.set()
    finally:
        # Prevent secondary Ctrl+C from interrupting the file flush / truncation safeguards
        try:
            signal.signal(signal.SIGINT, signal.SIG_IGN)
        except Exception:
            pass

        stop_event.set()

        # Immediately terminate all engine child processes to unblock worker threads
        with engines_lock:
            for eng in list(active_engines):
                try:
                    eng.kill()
                except Exception:
                    pass
            active_engines.clear()

        for t in threads:
            t.join(timeout=2)

        # Final flush under lock
        with buf_lock:
            if train_buf:
                f_train.write(train_buf)
                train_buf.clear()
            if val_buf:
                f_val.write(val_buf)
                val_buf.clear()
            f_train.flush()
            f_val.flush()

        f_train.close()
        f_val.close()
        if epd_fh:
            epd_fh.close()

        if pbar is not None:
            pbar.close()

        # Enforce exact 68-byte record alignment safeguard
        for path in (train_path, val_path):
            if os.path.exists(path):
                fsize = os.path.getsize(path)
                rem = fsize % RECORD_SIZE
                if rem != 0:
                    with open(path, "a+b") as fix_f:
                        fix_f.truncate(fsize - rem)
                    print(f"[Safeguard] Truncated {rem} trailing bytes from {path} for clean record alignment.", flush=True)

        print(stats.summary(), flush=True)


if __name__ == "__main__":
    main()
