#!/usr/bin/env python3
"""Self-play dataset generation for GOOB Texel tuning.

Plays games between two instances of the engine (MultiPV 3, random pick),
records every position after ply MIN_PLY with the game result.

Output format: one position per line,  "<fen>;<result>"  result in {1.0,0.5,0.0}
(white's perspective).
"""

import os
import random
import re
import select
import subprocess
import sys
import threading
import time

ENGINE = "/home/gabriel/Desktop/ChessEngineGoobC/bin/GOOB-2.1-linux-x64"
DEPTH = int(sys.argv[1]) if len(sys.argv) > 1 else 6
GAMES = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
THREADS = int(sys.argv[3]) if len(sys.argv) > 3 else 12
OUT = sys.argv[4] if len(sys.argv) > 4 else "dataset.epd"
MAX_PLY = 200
MIN_PLY_RECORD = 10
ADJUDICATE_PLY = 60
ADJUDICATE_EVAL = 700
MULTIPV = 3
PICK_W = [0.55, 0.30, 0.15]
READ_TIMEOUT = 30
MAX_OUT_LINES = 200000

OPENINGS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/2P1P3/8/PP1P1PPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/ppp1pppp/8/3p4/3PP3/8/PPP2PPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkb1r/ppp1pppp/5n2/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3",
    "rnbqkbnr/pp2pppp/2p5/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3",
    "rnbqkbnr/ppp1pppp/8/8/2pP4/8/PP2PPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/2P5/8/PP1PPPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pppppp1p/6p1/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pppppp1p/6p1/8/2P5/8/PP1PPPPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pppp1ppp/8/4p3/2P5/8/PP1PPPPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pp1ppppp/8/2p5/2P5/8/PP1PPPPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pppppp1p/6p1/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2",
    "rnbqkbnr/pppppp1p/6p1/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 2",
    "rnbqkbnr/pp1ppppp/8/2p5/2P5/8/PP1PPPPP/RNBQKBNR b KQkq - 0 2",
]

KNIGHT_D = [(1, 2), (2, 1), (2, -1), (1, -2), (-1, -2), (-2, -1), (-2, 1), (-1, 2)]


def parse_board(fen):
    board = {}
    occ = set()
    rows = fen.split()[0].split("/")
    for r, row in enumerate(rows):
        f = 0
        for ch in row:
            if ch.isdigit():
                f += int(ch)
            else:
                board[(f, 7 - r)] = ch
                occ.add((f, 7 - r))
                f += 1
    return board, occ


def in_check(board, occ, color):
    king = "K" if color == "w" else "k"
    enemy = "pnbrq" if color == "w" else "PNBRQ"
    ksq = next((sq for sq, p in board.items() if p == king), None)
    if ksq is None:
        return False
    kx, ky = ksq
    if color == "w":
        for dx in (-1, 1):
            if (kx + dx, ky + 1) in board and board[(kx + dx, ky + 1)] == "p":
                return True
    else:
        for dx in (-1, 1):
            if (kx + dx, ky - 1) in board and board[(kx + dx, ky - 1)] == "P":
                return True
    for dx, dy in KNIGHT_D:
        sq = (kx + dx, ky + dy)
        if sq in board and board[sq] in enemy and board[sq].lower() == "n":
            return True
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            if (dx, dy) == (0, 0):
                continue
            sq = (kx + dx, ky + dy)
            if sq in board and board[sq] in enemy and board[sq].lower() == "k":
                return True
    for dx, dy in ((-1, -1), (-1, 1), (1, -1), (1, 1)):
        x, y = kx + dx, ky + dy
        while (x, y) not in occ:
            x += dx
            y += dy
        if (x, y) in board and board[(x, y)] in enemy and board[(x, y)].lower() in ("b", "q"):
            return True
    for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        x, y = kx + dx, ky + dy
        while (x, y) not in occ:
            x += dx
            y += dy
        if (x, y) in board and board[(x, y)] in enemy and board[(x, y)].lower() in ("r", "q"):
            return True
    return False


def insufficient(fen):
    pieces = [c for c in fen.split()[0] if c.isalpha()]
    if any(c in "PpRrQq" for c in pieces):
        return False
    minors = sum(1 for c in pieces if c in "NBnb")
    return minors <= 1


def sig_of(fen):
    return " ".join(fen.split()[:3])


class Engine:
    def __init__(self, seed):
        self.p = subprocess.Popen(
            [ENGINE],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        self.fd = self.p.stdout.fileno()
        self.buf = b""
        self.send("uci")
        self.read_until("uciok")
        self.send("setoption name MultiPV value %d" % MULTIPV)
        self.send("isready")
        self.read_until("readyok")

    def send(self, line):
        self.p.stdin.write((line + "\n").encode())
        self.p.stdin.flush()

    def readline(self, timeout=READ_TIMEOUT):
        deadline = time.time() + timeout
        while b"\n" not in self.buf:
            remaining = deadline - time.time()
            if remaining <= 0:
                raise TimeoutError("engine read timeout")
            r, _, _ = select.select([self.fd], [], [], max(remaining, 0.001))
            if not r:
                raise TimeoutError("engine read timeout")
            chunk = os.read(self.fd, 65536)
            if not chunk:
                raise EOFError("engine closed stdout")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode(errors="replace").rstrip()

    def read_until(self, token):
        out = []
        while True:
            line = self.readline()
            out.append(line)
            if len(out) > MAX_OUT_LINES:
                raise RuntimeError("runaway output")
            if token in line:
                return out

    def read_bestmove(self):
        out = []
        while True:
            line = self.readline()
            out.append(line)
            if len(out) > MAX_OUT_LINES:
                raise RuntimeError("runaway output")
            if line.startswith("bestmove"):
                return out

    def search(self, poscmd, depth):
        self.send(poscmd)
        self.send("go depth %d" % depth)
        lines = self.read_bestmove()
        pvs = []
        for ln in lines:
            m = re.search(r"depth (\d+).*?multipv (\d+).*?pv (.+)$", ln)
            if m:
                pvs.append((int(m.group(1)), int(m.group(2)), m.group(3).split()))
        bm = None
        for ln in lines:
            if ln.startswith("bestmove"):
                bm = ln.split()[1]
                break
        return pvs, bm

    def fen_now(self):
        self.send("print")
        lines = self.read_until("Checkers:")
        for ln in lines:
            m = re.search(r"Fen: (.+)$", ln)
            if m:
                return m.group(1)
        raise RuntimeError("no Fen line in print output: %r" % lines)

    def eval_white(self):
        self.send("evaluate")
        lines = self.read_until("Eval Mirrored:")
        for ln in lines:
            m = re.search(r"Eval:\s*(-?\d+)", ln)
            if m:
                return int(m.group(1))
        raise RuntimeError("no Eval line: %r" % lines)

    def close(self):
        try:
            self.p.kill()
        except Exception:
            pass
        try:
            self.p.wait(timeout=5)
        except Exception:
            pass


def play_game(eng):
    opening = random.choice(OPENINGS)
    fen = opening
    moves = []
    history = []
    halfmove = int(fen.split()[4])
    positions = []
    ply = 0
    while True:
        sig = sig_of(fen)
        history.append(sig)
        if history.count(sig) >= 3 or insufficient(fen) or halfmove >= 100:
            return positions, 0.5
        if ply >= ADJUDICATE_PLY:
            we = eng.eval_white()
            if fen.split()[1] == "b":
                we = -we
            if we >= ADJUDICATE_EVAL:
                return positions, 1.0
            if we <= -ADJUDICATE_EVAL:
                return positions, 0.0
        if ply >= MAX_PLY:
            we = eng.eval_white()
            if fen.split()[1] == "b":
                we = -we
            return positions, 0.5 if abs(we) < ADJUDICATE_EVAL else (1.0 if we > 0 else 0.0)
        cmd = "position fen %s" % opening
        if moves:
            cmd += " moves " + " ".join(moves)
        pvs, bm = eng.search(cmd, DEPTH)
        if bm == "(none)":
            board, occ = parse_board(fen)
            side = fen.split()[1]
            if in_check(board, occ, side):
                return positions, 1.0 if side == "b" else 0.0
            return positions, 0.5
        if ply >= MIN_PLY_RECORD:
            positions.append((fen, ply))
        if pvs:
            max_depth = max(d for d, _, _ in pvs)
            ranked = []
            for d, _, mv in sorted(pvs):
                if d == max_depth and mv and mv[0] not in ranked:
                    ranked.append(mv[0])
            if ranked:
                bm = random.choices(ranked, weights=PICK_W[: len(ranked)])[0]
        moves.append(bm)
        cmd = "position fen %s" % opening
        if moves:
            cmd += " moves " + " ".join(moves)
        eng.send(cmd)
        fen = eng.fen_now()
        halfmove = int(fen.split()[4])
        ply += 1


def worker(seed, out_list, lock):
    eng = Engine(seed)
    done = 0
    while done < GAMES // THREADS:
        try:
            positions, result = play_game(eng)
        except (TimeoutError, EOFError, RuntimeError, ValueError, BrokenPipeError):
            eng.close()
            eng = Engine(seed)
            continue
        if result is None:
            continue
        done += 1
        with lock:
            for f, _ in positions:
                out_list.append("%s;%s" % (f, result))
        if done % 50 == 0:
            print("worker %d: %d games" % (seed, done), flush=True)
    eng.close()


def main():
    lock = threading.Lock()
    out_list = []
    threads = [
        threading.Thread(target=worker, args=(i, out_list, lock), daemon=True)
        for i in range(THREADS)
    ]
    t0 = time.time()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    with open(OUT, "w") as f:
        for line in out_list:
            f.write(line + "\n")
    print("wrote %d positions to %s in %.0fs" % (len(out_list), OUT, time.time() - t0))


if __name__ == "__main__":
    main()