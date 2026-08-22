#!/usr/bin/env python3
"""Optimized Self-play dataset generation for GOOB Texel tuning."""

import os
import random
import subprocess
import sys
import threading
import time
import chess

ENGINE = "/home/gabriel/Desktop/ChessEngineGoobC/src/bin/GOOB-2.1"
NODES = int(sys.argv[1]) if len(sys.argv) > 1 else 5000
GAMES = int(sys.argv[2]) if len(sys.argv) > 2 else 10000
THREADS = int(sys.argv[3]) if len(sys.argv) > 3 else 6
OUT = sys.argv[4] if len(sys.argv) > 4 else "dataset.epd"
BOOK_FILE = sys.argv[5] if len(sys.argv) > 5 else "book.epd"

MAX_PLY = 200
MIN_PLY_RECORD = 12
POSITIONS_PER_GAME = 15 # Randomly sample 15 quiet positions per game to avoid correlation
ADJUDICATE_PLY = 80
ADJUDICATE_EVAL = 800
MULTIPV = 3
PICK_W = [0.60, 0.25, 0.15]

# Load Opening Book
OPENINGS = ["rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"]
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
            stderr=None,
            text=True,
            bufsize=1,
            cwd=engine_src_dir
        )
        self.send("uci")
        self.read_until("uciok")
        self.send(f"setoption name MultiPV value {MULTIPV}")
        self.send("isready")
        self.read_until("readyok")

    def send(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()

    def read_until(self, token):
        lines = []
        while True:
            line = self.p.stdout.readline()
            if line == "":  # True EOF check (process died or stdout closed)
                raise EOFError("Engine crashed or closed stdout")
            
            line_str = line.strip()
            lines.append(line_str)
            if token in line_str:
                return lines

    def search(self, poscmd, nodes):
        self.send(poscmd)
        self.send(f"go nodes {nodes}")
        
        lines = self.read_until("bestmove")
        pvs = {}
        bm = None
        eval_score = 0
        
        for line in lines:
            if line.startswith("info") and "multipv" in line and " pv " in line:
                tokens = line.split()
                try:
                    mpv = int(tokens[tokens.index("multipv") + 1])
                    move = tokens[tokens.index("pv") + 1]
                    pvs[mpv] = move
                    
                    # Grab eval from PV 1 for adjudication
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

        # MultiPV Selection
        ranked_moves = [pvs[k] for k in sorted(pvs.keys()) if k in pvs]
        if ranked_moves:
            bm = random.choices(ranked_moves, weights=PICK_W[:len(ranked_moves)])[0]
            
        return bm, eval_score

    def close(self):
        try:
            self.send("quit")
            self.p.wait(timeout=2)
        except:
            self.p.kill()

def play_game(eng):
    start_fen = random.choice(OPENINGS)
    board = chess.Board(start_fen)
    moves_history = []
    quiet_positions = []
    
    ply = 0
    result = None
    
    while True:
        # 1. Rule-based Draw Checks
        if board.is_insufficient_material() or board.can_claim_draw() or board.is_seventyfive_moves():
            result = 0.5
            break
            
        # 2. Build Position Command (Use moves history so engine detects repetitions)
        cmd = f"position fen {start_fen}"
        if moves_history:
            cmd += " moves " + " ".join(moves_history)
            
        # 3. Search
        bm_str, eval_score = eng.search(cmd, NODES)
        
        if bm_str is None or bm_str == "(none)" or bm_str == "0000":
            result = 0.0 if board.turn == chess.WHITE else 1.0 # checkmate or stalemate handling
            if not board.is_check():
                result = 0.5
            break
            
        move = chess.Move.from_uci(bm_str)
        
        # 4. Engine Eval Adjudication (POV White)
        we = eval_score if board.turn == chess.WHITE else -eval_score
        if ply >= ADJUDICATE_PLY:
            if we >= ADJUDICATE_EVAL: result = 1.0; break
            if we <= -ADJUDICATE_EVAL: result = 0.0; break
            
        if ply >= MAX_PLY:
            result = 0.5 if abs(we) < ADJUDICATE_EVAL else (1.0 if we > 0 else 0.0)
            break

        # 5. Data Collection: Only log QUIET positions
        if ply >= MIN_PLY_RECORD:
            is_capture = board.is_capture(move)
            is_check = board.is_check()
            is_promo = move.promotion is not None
            
            # If the board is quiet right now, and the move we are ABOUT to make isn't a capture/promo
            if not is_capture and not is_check and not is_promo:
                quiet_positions.append(board.fen())

        board.push(move)
        moves_history.append(bm_str)
        ply += 1

    # Format positions with result
    sampled = random.sample(quiet_positions, min(len(quiet_positions), POSITIONS_PER_GAME))
    return [f"{fen};{result}" for fen in sampled]

def worker(worker_id, out_list, lock):
    eng = Engine()
    done = 0
    target = GAMES // THREADS
    
    while done < target:
        try:
            positions = play_game(eng)
            if positions:
                with lock:
                    out_list.extend(positions)
            done += 1
            if done % 10 == 0:
                print(f"Worker {worker_id}: {done}/{target} games", flush=True)
        except Exception as e:
            # Handle engine crashes, restart and continue
            eng.close()
            eng = Engine()
            
    eng.close()

def main():
    lock = threading.Lock()
    out_list = []
    threads = [
        threading.Thread(target=worker, args=(i, out_list, lock), daemon=True)
        for i in range(THREADS)
    ]
    
    print(f"Starting datagen: {GAMES} games, {THREADS} threads, {NODES} nodes/move")
    t0 = time.time()
    for t in threads: t.start()
    for t in threads: t.join()
    
    with open(OUT, "w") as f:
        for line in out_list:
            f.write(line + "\n")
            
    print(f"Wrote {len(out_list)} quiet positions to {OUT} in {time.time() - t0:.0f}s")

if __name__ == "__main__":
    main()