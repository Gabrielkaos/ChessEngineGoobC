#!/usr/bin/env python3
"""Generate reference scores for the GOOB search-constant tuner (SPT / --mode ref).

WHY THIS EXISTS
---------------
tools/search_tuner.c can minimise the distance between our fixed-node search and
a target.  In --mode label the target is the centipawn eval baked into the
training records, but the network was *trained* on exactly those labels, so the
static eval already predicts them and the gradient mostly asks the search to
change as little as possible.  In --mode texel the target is a game result,
which needs self-play data.

--mode ref sits in between and is the right default when all you have is an
eval-derived dataset: it asks a well-posed question --

    make our fixed-node search agree with a *stronger* engine's search of the
    same positions

-- and it needs nothing but a stronger binary and a list of positions.  This
script is the producer half of that pair: it drives a reference engine over
UCI, records the score it returns for each position, and writes the int32
stream tools/search_tuner.c --ref expects.

Dataset order is the contract
-----------------------------
The tuner loads --data, drops positions it cannot parse, and then asserts the
reference file has *exactly* as many int32s as survive that filter.  So this
script writes the positions it actually scored to a companion .fen file and
tells you to point --data at *that*, not at the original input.  Both files are
written together, from the same list of successes, so the two cannot drift
apart.  Scoring a position the engine rejects drops it from both.

    tools/gen_ref_scores.py --engine ./sf --in positions.txt \\
        --out ref.bin --depth 12
    src/bin/linux/search-tuner --data ref.bin.fen --ref ref.bin --mode ref ...

Starting from a binary dataset instead: the .fen file only works if you already
have FENs, and the 68-byte training records do not have castling rights or an
en passant square.  Let the tuner do the decoding and tell you what it loaded,
which is the same list by construction:

    src/bin/linux/search-tuner --data train.bin --limit 200000 \\
        --dump-fens fens.txt --steps 1 --positions 1
    tools/gen_ref_scores.py --engine ./sf --nodes 200000 \\
        --in fens.txt --out ref.bin
    src/bin/linux/search-tuner --data train.bin --limit 200000 \\
        --ref ref.bin --mode ref ...

Score conventions
-----------------
Scores are written side-to-move relative, which is what UCI reports and what
the tuner compares against, so no sign flipping happens anywhere.  `score mate N`
is written as a saturated sentinel (see MATE_CP): the search treats a forced
mate as a certain win, and tanh() of that sentinel is exactly 1.0, so a mate is
a hard target rather than a slightly-larger-than-centipawn one.
"""

import argparse
import os
import re
import struct
import subprocess
import sys

# Must stay below the engine's ISMATE (= AB_BOUND - MAXDEPTH = 30000 - 128), or
# the tuner discards the sample as a mate and the run is quietly short of data.
MATE_CP = 29000
# Keeps a mate sentinel from saturating the tanh scale as if it were a real
# eval, while still being unmistakably "this is a mate".
MAX_CP = 20000

RANK_1, RANK_8 = 0, 7
FILE_A, FILE_H = 0, 7
PIECES = "PNBRQKpnbrqk"

SCORE_RE = re.compile(r"\bscore\s+(cp|mate)\s+(-?\d+)")


def fen_looks_playable(fen):
    """Port of fenLooksPlayable() in tools/search_tuner.c.

    Deliberately duplicated rather than shared: the tuner is C and this is a
    build-time tool, and the two only have to agree.  If the engine's filter
    ever changes, change it here too or --mode ref silently loses positions.
    """
    rank, file = RANK_8, FILE_A
    kings = [0, 0]
    for ch in fen:
        if ch == " ":
            break
        if ch == "/":
            if file != 8 or rank - 1 < RANK_1:
                return False
            rank -= 1
            file = FILE_A
            continue
        if "1" <= ch <= "8":
            file += ord(ch) - ord("0")
            continue
        if ch not in PIECES:
            return False
        if file > FILE_H:
            return False
        if ch in "Pp" and rank in (RANK_1, RANK_8):
            return False
        if ch == "K":
            kings[0] += 1
        elif ch == "k":
            kings[1] += 1
        file += 1
    if rank != RANK_1 or file != 8:
        return False
    return kings[0] == 1 and kings[1] == 1


def read_fens(path, limit):
    """Read positions, applying the same normalisation the tuner applies."""
    out, total, bad = [], 0, 0
    with open(path, "r", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            total += 1
            # A ';result' suffix is datagen.py's convention and the tuner
            # strips it; do the same so the FEN we score is the FEN it loads.
            fen = line.split(";", 1)[0].strip()
            if not fen_looks_playable(fen):
                bad += 1
                continue
            out.append(fen)
            if limit and len(out) >= limit:
                break
    return out, total, bad


class Engine:
    """Minimal UCI driver.  One process, one search at a time."""

    def __init__(self, path, hash_mb, threads, verbose):
        self.proc = subprocess.Popen(
            [path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=None if verbose else subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        self.verbose = verbose
        self._send("uci")
        self._drain_until("uciok")
        if threads > 1:
            self._send("setoption name Threads value %d" % threads)
        if hash_mb:
            self._send("setoption name Hash value %d" % hash_mb)
        self._send("isready")
        self._drain_until("readyok")

    def _send(self, line):
        if self.verbose:
            print(">", line, file=sys.stderr)
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def _drain_until(self, token):
        for line in self.proc.stdout:
            if self.verbose:
                print("<", line.rstrip(), file=sys.stderr)
            if token in line:
                return True
        return False

    def score(self, fen, limit_spec):
        """Return (score_cp, n) for a FEN, or (None, 0) if the engine refused."""
        self._send("position fen " + fen)
        self._send("go " + limit_spec)
        best, got_move = None, False
        for line in self.proc.stdout:
            if self.verbose:
                print("<", line.rstrip(), file=sys.stderr)
            m = SCORE_RE.search(line)
            if m:
                kind, val = m.group(1), int(m.group(2))
                # Keep the deepest report: aspiration re-searches emit
                # fail-high/fail-low bounds that a later line supersedes.
                if kind == "cp":
                    best = max(-MAX_CP, min(MAX_CP, val))
                else:
                    best = MATE_CP if val > 0 else -MATE_CP
            elif line.startswith("bestmove"):
                got_move = True
                break
        return (best, 1) if (got_move and best is not None) else (None, 0)

    def close(self):
        try:
            self._send("quit")
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


def main():
    ap = argparse.ArgumentParser(
        description="Generate int32 reference scores for search_tuner --mode ref.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("-- Dataset order is the contract --")[0],
    )
    ap.add_argument("--engine", required=True, help="reference engine binary (UCI)")
    ap.add_argument("--in", dest="inp", required=True, help="input FEN / 'FEN;result' file")
    ap.add_argument("--out", default="ref.bin", help="output int32 score file")
    ap.add_argument("--fen-out", default=None,
                    help="positions actually scored (default: <out>.fen). "
                         "Point the tuner's --data at this.")
    ap.add_argument("--depth", type=int, default=0, help="fixed depth per position")
    ap.add_argument("--nodes", type=int, default=0, help="fixed node budget per position")
    ap.add_argument("--limit", type=int, default=0, help="stop after N positions")
    ap.add_argument("--hash", dest="hash_mb", type=int, default=64, help="engine Hash (MB)")
    ap.add_argument("--threads", type=int, default=1, help="engine Threads (for go nodes/movetime)")
    ap.add_argument("--report-every", type=int, default=500, help="progress interval")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if args.depth and args.nodes:
        sys.exit("give at most one of --depth / --nodes")
    if args.depth:
        limit_spec = "depth %d" % args.depth
    elif args.nodes:
        limit_spec = "nodes %d" % args.nodes
    else:
        limit_spec = "depth 12"

    if not os.path.exists(args.engine):
        sys.exit("no such engine: %s" % args.engine)

    fens, total, bad = read_fens(args.inp, args.limit)
    print("input: %s -- %d lines, %d usable, %d rejected by the FEN filter"
          % (args.inp, total, len(fens), bad))
    if not fens:
        sys.exit("nothing to score")

    print("engine: %s  (%s)" % (args.engine, limit_spec))
    eng = Engine(args.engine, args.hash_mb, args.threads, args.verbose)

    scores, mates, refused = [], 0, 0
    kept = []          # the FENs the scores line up with -- see below
    try:
        for i, fen in enumerate(fens, 1):
            sc, ok = eng.score(fen, limit_spec)
            if not ok:
                refused += 1
                continue
            if abs(sc) == MATE_CP:
                mates += 1
            scores.append(sc)
            # Append in lockstep.  The score file is a positional stream, so a
            # single refusal in the middle of the list shifts everything after
            # it by one if the FEN list is sliced off the front at the end --
            # and the shift is silent: every position would still get a
            # plausible score, just the wrong one, and tuning against it would
            # quietly fit noise.
            kept.append(fen)
            if args.report_every and i % args.report_every == 0:
                print("  %d/%d scored (%d refused)" % (len(scores), i, refused), flush=True)
    except KeyboardInterrupt:
        print("\ninterrupted; writing what has been scored so far", file=sys.stderr)
    finally:
        eng.close()

    if not scores:
        sys.exit("the engine returned no usable score for any position")
    assert len(kept) == len(scores), (len(kept), len(scores))

    fen_out = args.fen_out or (args.out + ".fen")
    with open(args.out, "wb") as fh:
        fh.write(struct.pack("<%di" % len(scores), *scores))
    with open(fen_out, "w") as fh:
        fh.write("\n".join(kept) + "\n")

    print("\nwrote %s  -- %d int32 scores" % (args.out, len(scores)))
    print("wrote %s  -- %d positions" % (fen_out, len(scores)))
    if refused:
        print("note: %d position(s) the engine refused were dropped from BOTH files"
              % refused)
    if mates:
        print("note: %d mate score(s) written as +/-%d" % (mates, MATE_CP))
    print("\nnext:\n  src/bin/linux/search-tuner --data %s --ref %s --mode ref \\\n"
          "      --nodes 20000 --positions 8192 --steps 500" % (fen_out, args.out))


if __name__ == "__main__":
    main()
