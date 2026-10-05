#!/usr/bin/env python3
import subprocess, sys, re
eng = sys.argv[1]; depth = int(sys.argv[2]) if len(sys.argv) > 2 else 12
fens = [
"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
"2r2rk1/1bqnbpp1/1p1ppn1p/pP6/N1P1P3/P2B1N1P/1B2QPP1/R2R2K1 b - - 0 1",
"r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R1BQKB1R w KQ - 0 1",
"8/8/1p1k4/p1p1p3/P1P1P1p1/1P1K2P1/8/8 w - - 0 1",
"6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
"r2q1rk1/1b1nbppp/pp1ppn2/8/2PNP3/1PN1B3/P2QBPPP/R4RK1 w - - 0 1",
"3rr1k1/pp3ppp/2n2q2/2b1p3/8/P1NB1Q2/1PP2PPP/R3R1K1 w - - 0 1",
"r1b1k2r/ppppqppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R w KQkq - 0 1",
"2kr3r/ppp1qppp/2n1bn2/4p3/4P3/2N1BN2/PPPQ1PPP/2KR3R w - - 0 1",
"8/5pk1/6p1/8/3K4/8/5PPP/8 w - - 0 1",
"r1bqkb1r/pp3ppp/2n1pn2/2pp4/3P4/2P1PN2/PP1N1PPP/R1BQKB1R w KQkq - 0 1",
]
p = subprocess.Popen([eng], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
def send(s): p.stdin.write(s + "\n"); p.stdin.flush()
def readuntil(tok):
    lines = []
    while True:
        l = p.stdout.readline()
        if not l: raise SystemExit("engine died")
        lines.append(l.strip())
        if l.startswith(tok): return lines
send("uci"); readuntil("uciok")
send("setoption name Hash value 64"); 
totn = tott = 0
for f in fens:
    send("ucinewgame"); send("isready"); readuntil("readyok")
    send(f"position fen {f}"); send(f"go depth {depth}")
    lines = readuntil("bestmove")
    last = [l for l in lines if l.startswith(f"info depth {depth} ")]
    last = last[-1] if last else ""
    n = int(re.search(r" nodes (\d+)", last).group(1)) if last else 0
    t = int(re.search(r" time (\d+)", last).group(1)) if last else 0
    sc = re.search(r"score (cp|mate) (-?\d+)", last)
    sc = f"{sc.group(1)} {sc.group(2)}" if sc else "?"
    bm = lines[-1].split()[1]
    print(f"{bm:8s} {sc:10s} nodes={n:<10d} time={t:<6d} {f[:32]}")
    totn += n; tott += t
send("quit")
print(f"TOTAL nodes={totn} time={tott}ms nps={totn*1000//max(1,tott)}")
