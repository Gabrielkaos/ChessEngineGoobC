#!/usr/bin/env python3
"""
Read-only statistics for 68-byte NNUE record files (Round 2, Phase 1).

Opens every input with np.memmap(mode="r") and never writes next to it;
the only output is stdout (redirect or tee it yourself).

Usage:
    python3 data_stats.py --data <file|dir> [--data ...] [--sample-limit N]
                          [--dup-sample 5000000] [--log-dir <dir>]

Prints:
  * record count, mate-flag share, W/D/L share per record
  * stm-relative eval histogram and the share with |eval| >= 500 / 1500 / 3000
  * piece-count histogram mapped to the 8 output buckets
  * king-bucket histograms per perspective, for the current 4-bucket layout
    and the 8-bucket layout proposed in NNUE_IMPROVEMENT_PLAN_ROUND2.md 13.3
  * duplicate rate (64-bit hash of bytes 0..64) on a random sample
  * for records with a game result: a calibration table (mean game score per
    50 cp bin vs sigmoid(cp / 400)), the fitted scale K that minimises the
    log-loss of sigmoid(cp / K) against the results (overall and per output
    bucket), and the sign-disagreement rate for |eval| > 300

--log-dir also parses tools/datagen.c worker logs (gen_w*.log): games,
records per game, adjudication reasons, W/D/L per game, positions per second.
"""

import argparse
import glob
import math
import os
import re
import sys
from collections import Counter

import numpy as np

RECORD_SIZE = 68
CHUNK = 2_000_000
CP_SCALE = 400.0
CAL_BIN = 10          # cp width of the bins the K fit runs on
CAL_MAX = 3000

# current 4-bucket layout (model.py KING_BUCKET), by perspective-relative square
KB4 = np.array(
    [0, 0, 1, 1, 1, 1, 0, 0] + [2] * 8 + [3] * 48, dtype=np.int64
)
# proposed 8-bucket layout (plan 13.3), mirrored across the d/e line
_R1 = [0, 0, 1, 2, 2, 1, 0, 0]
_R2 = [3, 3, 4, 4, 4, 4, 3, 3]
KB8 = np.array(_R1 + _R2 + [5] * 8 + [6] * 16 + [7] * 24, dtype=np.int64)


def input_files(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            files += sorted(glob.glob(os.path.join(p, "*.bin")))
        elif os.path.isfile(p):
            files.append(p)
        else:
            files += sorted(glob.glob(p))
    if not files:
        raise SystemExit(f"no record files found in {paths}")
    for f in files:
        if os.path.getsize(f) % RECORD_SIZE:
            raise SystemExit(f"{f}: size is not a multiple of {RECORD_SIZE}")
    return files


def open_maps(files):
    maps = []
    for f in files:
        n = os.path.getsize(f) // RECORD_SIZE
        if n:
            maps.append(np.memmap(f, dtype=np.uint8, mode="r", shape=(n, RECORD_SIZE)))
    return maps


def iter_chunks(maps, limit):
    """Yields record chunks; with a limit, spreads it over the files by size."""
    total = sum(len(m) for m in maps)
    for m in maps:
        take = len(m) if limit <= 0 or limit >= total else int(len(m) * limit / total)
        if take <= 0:
            continue
        if take == len(m):
            for s in range(0, len(m), CHUNK):
                yield np.asarray(m[s:s + CHUNK])
        else:
            # an evenly spread subsample, read in sorted order
            idx = np.linspace(0, len(m) - 1, take).astype(np.int64)
            for s in range(0, take, CHUNK):
                yield np.asarray(m[idx[s:s + CHUNK]])


def record_hash(chunk):
    """64-bit hash of bytes 0..64 (board + side to move)."""
    words = np.ascontiguousarray(chunk[:, :64]).view("<u8")      # (B, 8)
    h = np.full(len(chunk), 0x9E3779B97F4A7C15, dtype=np.uint64)
    with np.errstate(over="ignore"):
        for i in range(8):
            h ^= words[:, i]
            h *= np.uint64(0xBF58476D1CE4E5B9)
            h ^= h >> np.uint64(31)
        h ^= chunk[:, 64].astype(np.uint64)
        h *= np.uint64(0x94D049BB133111EB)
        h ^= h >> np.uint64(29)
    return h


def pct(a, b):
    return 100.0 * a / b if b else 0.0


def fit_k(cp, n, s):
    """K minimising sum n * logloss(sigmoid(cp / K), mean score s)."""
    keep = n > 0
    cp, n, s = cp[keep], n[keep], s[keep]
    if n.sum() == 0:
        return float("nan")

    def loss(k):
        p = 1.0 / (1.0 + np.exp(-cp / k))
        p = np.clip(p, 1e-9, 1 - 1e-9)
        return float(np.sum(n * -(s * np.log(p) + (1 - s) * np.log(1 - p))))

    lo, hi = math.log(50.0), math.log(3000.0)
    g = (math.sqrt(5) - 1) / 2
    a, b = hi - g * (hi - lo), lo + g * (hi - lo)
    fa, fb = loss(math.exp(a)), loss(math.exp(b))
    for _ in range(80):
        if fa < fb:
            hi, b, fb = b, a, fa
            a = hi - g * (hi - lo)
            fa = loss(math.exp(a))
        else:
            lo, a, fa = a, b, fb
            b = lo + g * (hi - lo)
            fb = loss(math.exp(b))
    return math.exp((lo + hi) / 2)


def analyse(files, limit, dup_sample):
    maps = open_maps(files)
    total_avail = sum(len(m) for m in maps)
    n = 0
    mate = 0
    res = np.zeros(4, dtype=np.int64)
    eval_edges = np.array([-3001, -1500, -1000, -500, -300, -100, -25, 25, 100, 300, 500, 1000, 1500, 3001])
    eval_hist = np.zeros(len(eval_edges) - 1, dtype=np.int64)
    abs_ge = Counter()
    out_bucket = np.zeros(8, dtype=np.int64)
    kb4 = np.zeros((2, 4), dtype=np.int64)
    kb8 = np.zeros((2, 8), dtype=np.int64)
    nbins = 2 * CAL_MAX // CAL_BIN + 1
    cal_n = np.zeros((8, nbins), dtype=np.int64)
    cal_s = np.zeros((8, nbins), dtype=np.float64)
    sign_big = 0
    sign_bad = 0

    for chunk in iter_chunks(maps, limit):
        B = len(chunk)
        n += B
        flags = chunk[:, 67]
        is_mate = (flags & 1).astype(bool)
        mate += int(is_mate.sum())
        rc = (flags >> 1) & 3
        res += np.bincount(rc, minlength=4)

        stm = chunk[:, 64]
        cpw = np.ascontiguousarray(chunk[:, 65:67]).view("<i2").reshape(B).astype(np.int64)
        cp = np.where(stm == 0, cpw, -cpw)
        eval_hist += np.histogram(cp, bins=eval_edges)[0]
        a = np.abs(cp)
        for t in (500, 1500, 3000):
            abs_ge[t] += int((a >= t).sum())

        board = chunk[:, :64]
        pieces = (board != 0).sum(axis=1)
        ob = np.clip((pieces - 2) // 4, 0, 7)
        out_bucket += np.bincount(ob, minlength=8)

        wk = np.argmax(board == 6, axis=1)
        bk = np.argmax(board == 12, axis=1) ^ 56
        kb4[0] += np.bincount(KB4[wk], minlength=4)
        kb4[1] += np.bincount(KB4[bk], minlength=4)
        kb8[0] += np.bincount(KB8[wk], minlength=8)
        kb8[1] += np.bincount(KB8[bk], minlength=8)

        has = (rc > 0) & ~is_mate
        if has.any():
            score_w = np.array([0.0, 1.0, 0.5, 0.0])[rc[has]]
            score = np.where(stm[has] == 0, score_w, 1.0 - score_w)
            c = np.clip(cp[has], -CAL_MAX, CAL_MAX)
            bi = (c + CAL_MAX + CAL_BIN // 2) // CAL_BIN
            np.add.at(cal_n, (ob[has], bi), 1)
            np.add.at(cal_s, (ob[has], bi), score)
            big = np.abs(cp[has]) > 300
            sign_big += int(big.sum())
            sign_bad += int((big & (np.sign(cp[has]) != np.sign(score - 0.5)) & (score != 0.5)).sum())

    print(f"records analysed     : {n:,} of {total_avail:,} in {len(files)} file(s)")
    print(f"mate-flagged         : {mate:,} ({pct(mate, n):.2f}%)")
    print(f"result per record    : none {pct(res[0], n):.1f}%  W {pct(res[1], n):.1f}%  "
          f"D {pct(res[2], n):.1f}%  B {pct(res[3], n):.1f}%")

    print("\nstm eval histogram (cp):")
    for i in range(len(eval_hist)):
        lo, hi = eval_edges[i], eval_edges[i + 1]
        print(f"  [{lo:6d}, {hi:6d})  {eval_hist[i]:>12,}  {pct(eval_hist[i], n):5.1f}%")
    print("  |eval| >= 500: {:.2f}%   >= 1500: {:.2f}%   >= 3000: {:.2f}%".format(
        pct(abs_ge[500], n), pct(abs_ge[1500], n), pct(abs_ge[3000], n)))

    print("\noutput bucket (by piece count):")
    print("  " + "  ".join(f"{b}:{pct(out_bucket[b], n):5.1f}%" for b in range(8)))
    print("king buckets, current 4-bucket layout (white / black perspective):")
    for p, name in ((0, "white"), (1, "black")):
        print(f"  {name}: " + "  ".join(f"{b}:{pct(kb4[p, b], n):5.1f}%" for b in range(4)))
    print("king buckets, proposed 8-bucket layout (plan 13.3):")
    for p, name in ((0, "white"), (1, "black")):
        print(f"  {name}: " + "  ".join(f"{b}:{pct(kb8[p, b], n):5.1f}%" for b in range(8)))
    both8 = kb8.sum(axis=0)
    low = [b for b in range(8) if both8[b] < 0.01 * 2 * n]
    print(f"  buckets under 1% of perspectives: {low if low else 'none'}")

    # duplicates on a random sample
    k = min(dup_sample, total_avail)
    if k > 0:
        rng = np.random.default_rng(0)
        hashes = []
        sizes = np.array([len(m) for m in maps])
        picks = np.sort(rng.choice(total_avail, size=k, replace=False))
        starts = np.concatenate([[0], np.cumsum(sizes)])
        for i, m in enumerate(maps):
            sel = picks[(picks >= starts[i]) & (picks < starts[i + 1])] - starts[i]
            for s in range(0, len(sel), CHUNK):
                hashes.append(record_hash(np.asarray(m[sel[s:s + CHUNK]])))
        h = np.concatenate(hashes)
        uniq = len(np.unique(h))
        print(f"\nduplicates in a {k:,}-record random sample: {k - uniq:,} ({pct(k - uniq, k):.2f}%)")

    tot = cal_n.sum()
    if tot == 0:
        print("\nno game results in these records: calibration skipped")
        return
    centers = (np.arange(nbins) * CAL_BIN - CAL_MAX).astype(np.float64)
    print(f"\ncalibration on {tot:,} non-mate records with a result")
    print("  bin (cp)          n     mean score   sigmoid(cp/400)")
    agg_n = cal_n.sum(axis=0)
    agg_s = cal_s.sum(axis=0)
    for lo in range(-1000, 1000, 50):
        sel = (centers >= lo) & (centers < lo + 50)
        nn = int(agg_n[sel].sum())
        if nn == 0:
            continue
        mean = agg_s[sel].sum() / nn
        mid = lo + 25
        print(f"  [{lo:5d}, {lo + 50:5d})  {nn:>10,}   {mean:8.3f}      {1 / (1 + math.exp(-mid / CP_SCALE)):8.3f}")
    mean_s = np.divide(agg_s, agg_n, out=np.zeros_like(agg_s), where=agg_n > 0)
    k_all = fit_k(centers, agg_n, mean_s)
    print(f"  fitted K (overall)   : {k_all:7.1f}   (net target uses {CP_SCALE:.0f})")
    for b in range(8):
        nb = cal_n[b]
        if nb.sum() < 10_000:
            continue
        mb = np.divide(cal_s[b], nb, out=np.zeros_like(cal_s[b]), where=nb > 0)
        print(f"  fitted K (bucket {b})  : {fit_k(centers, nb, mb):7.1f}   ({nb.sum():,} records)")
    print(f"  sign disagreement    : {sign_bad:,} of {sign_big:,} records with |eval| > 300 lost by the "
          f"side the eval favours ({pct(sign_bad, sign_big):.2f}%; draws count as agreeing)")


LINE = re.compile(r"^Game\s+(\d+) \| Pos:\s+(\d+) \(\+(\d+)\) \| (\S+)\s+\((.*?)\) \|")
RATE = re.compile(r"Total Time\s+:\s+([\d.]+) s \(([\d.]+) pos/s\)")


def analyse_logs(log_dir):
    logs = sorted(glob.glob(os.path.join(log_dir, "gen_w*.log")))
    if not logs:
        print(f"\nno gen_w*.log files in {log_dir}")
        return
    games = 0
    recs = []
    reasons = Counter()
    results = Counter()
    rates = []
    short_resign = 0
    for path in logs:
        with open(path, "r", errors="replace") as f:
            for line in f:
                m = LINE.match(line)
                if m:
                    games += 1
                    r = int(m.group(3))
                    recs.append(r)
                    reason = re.sub(r" \((White|Black) winning\)", "", m.group(5))
                    reasons[reason] += 1
                    results[m.group(4)] += 1
                    if reason == "resignation" and r <= 20:
                        short_resign += 1
                    continue
                m = RATE.search(line)
                if m:
                    rates.append(float(m.group(2)))
    print(f"\ndatagen logs: {len(logs)} worker(s), {games:,} games")
    if not games:
        return
    r = np.array(recs)
    print(f"  records per game     : mean {r.mean():.1f}  median {np.median(r):.0f}  "
          f"p10 {np.percentile(r, 10):.0f}  p90 {np.percentile(r, 90):.0f}")
    print("  result per game      : " + "  ".join(
        f"{k} {pct(v, games):.1f}%" for k, v in sorted(results.items())))
    print("  adjudication reasons :")
    for k, v in reasons.most_common():
        print(f"    {k:<32} {v:>9,}  {pct(v, games):5.1f}%")
    print(f"  resigned with <= 20 records: {short_resign:,} ({pct(short_resign, games):.1f}%)"
          "  (records <= plies, so this bounds 'resigned within 20 plies' from above)")
    if rates:
        print(f"  positions / s        : {sum(rates):.0f} total over {len(rates)} finished worker(s)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--data", action="append", required=True, help="record file, directory or glob (repeatable)")
    ap.add_argument("--sample-limit", type=int, default=0, help="analyse at most this many records (0 = all)")
    ap.add_argument("--dup-sample", type=int, default=5_000_000, help="random sample size for the duplicate rate")
    ap.add_argument("--log-dir", default=None, help="directory with datagen gen_w*.log files")
    args = ap.parse_args()
    files = input_files(args.data)
    analyse(files, args.sample_limit, args.dup_sample)
    if args.log_dir:
        analyse_logs(args.log_dir)


if __name__ == "__main__":
    sys.exit(main())
