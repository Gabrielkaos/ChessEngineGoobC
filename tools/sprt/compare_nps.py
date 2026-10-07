#!/usr/bin/env python3
import subprocess, time, sys, re, statistics

BASE_BIN = sys.argv[1] if len(sys.argv) > 1 else "src/bin/linux/GOOB-baseline"
NEW_BIN  = sys.argv[2] if len(sys.argv) > 2 else "src/bin/linux/GOOB-2.2-BETA-native"
DEPTH    = int(sys.argv[3]) if len(sys.argv) > 3 else 12
ROUNDS   = int(sys.argv[4]) if len(sys.argv) > 4 else 10

FENS = [
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

def run_bench(binary, depth):
    p = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
    def send(s): p.stdin.write(s + "\n"); p.stdin.flush()
    def readuntil(tok):
        lines = []
        while True:
            l = p.stdout.readline()
            if not l: raise SystemExit("engine died")
            lines.append(l.strip())
            if l.startswith(tok): return lines

    send("uci"); readuntil("uciok")
    send("setoption name Hash value 64")
    
    t0 = time.perf_counter()
    tot_nodes = 0
    for f in FENS:
        send("ucinewgame"); send("isready"); readuntil("readyok")
        send(f"position fen {f}"); send(f"go depth {depth}")
        lines = readuntil("bestmove")
        last = [l for l in lines if l.startswith(f"info depth {depth} ")]
        last = last[-1] if last else ""
        n = int(re.search(r" nodes (\d+)", last).group(1)) if last else 0
        tot_nodes += n
    send("quit")
    p.wait()
    t1 = time.perf_counter()
    elapsed = t1 - t0
    nps = int(tot_nodes / elapsed)
    return tot_nodes, elapsed, nps

print(f"=== A/B NPS Benchmark: BASE={BASE_BIN} vs NEW={NEW_BIN} (depth={DEPTH}, rounds={ROUNDS}) ===")
print("Warming up...")
run_bench(BASE_BIN, DEPTH)
run_bench(NEW_BIN, DEPTH)

base_nps_list = []
new_nps_list = []
base_time_list = []
new_time_list = []

for r in range(1, ROUNDS + 1):
    if r % 2 == 1:
        n1, t1, nps1 = run_bench(BASE_BIN, DEPTH)
        n2, t2, nps2 = run_bench(NEW_BIN, DEPTH)
    else:
        n2, t2, nps2 = run_bench(NEW_BIN, DEPTH)
        n1, t1, nps1 = run_bench(BASE_BIN, DEPTH)
    
    base_nps_list.append(nps1)
    new_nps_list.append(nps2)
    base_time_list.append(t1)
    new_time_list.append(t2)
    diff_pct = (nps2 - nps1) / nps1 * 100
    print(f"Round {r:2d}: BASE = {nps1:8d} nps ({t1*1000:6.1f}ms) | NEW = {nps2:8d} nps ({t2*1000:6.1f}ms) | Delta = {diff_pct:+5.2f}%")

mean_base_nps = statistics.mean(base_nps_list)
mean_new_nps  = statistics.mean(new_nps_list)
mean_base_t   = statistics.mean(base_time_list) * 1000
mean_new_t    = statistics.mean(new_time_list) * 1000
gain_pct      = (mean_new_nps - mean_base_nps) / mean_base_nps * 100

print("\n" + "=" * 65)
print(f"Summary over {ROUNDS} rounds (depth {DEPTH}):")
print(f"  BASE NPS : {mean_base_nps:10.0f} ± {statistics.stdev(base_nps_list):.0f} (avg time: {mean_base_t:.1f}ms)")
print(f"  NEW  NPS : {mean_new_nps:10.0f} ± {statistics.stdev(new_nps_list):.0f} (avg time: {mean_new_t:.1f}ms)")
print(f"  NPS GAIN : {gain_pct:+.2f}%")
print("=" * 65)
