#!/usr/bin/env python3
"""
Tests for the datagen v2 flags (NNUE_IMPROVEMENT_PLAN_ROUND2.md 12.2).

    python3 tools/test_datagen_v2.py --v1 <datagen built from the v1 source> [--v2 tools/datagen]

Writes only into a new temporary directory, which it deletes afterwards.
  1. every v2 flag off: output byte-identical to v1 for the same seed
  2. --max-fifty: same games as the v1 run (the flag only drops records), the
     dropped records are exactly the ones counted as skipped
  3. --max-open-eval: some openings rejected; no game resigns in its first 8 plies
  4. --index: record counts sum to the file size / 68, offsets are contiguous
"""

import argparse
import csv
import os
import re
import shutil
import subprocess
import sys
import tempfile

import numpy as np

RS = 68
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def gen(binary, out, *extra, games=40, seed=7):
    cmd = [binary, "--out", out, "--games", str(games), "--nodes", "1500", "--seed", str(seed),
           "--book", "tools/book_datagen.epd" if os.path.exists(os.path.join(ROOT, "tools/book_datagen.epd")) else "tools/book.epd"]
    r = subprocess.run(cmd + list(extra), capture_output=True, text=True, cwd=ROOT)
    if r.returncode:
        raise SystemExit(f"{cmd} failed:\n{r.stderr}")
    return r.stdout


def records(path):
    return np.fromfile(path, dtype=np.uint8).reshape(-1, RS)


def games_line(stdout):
    return [re.sub(r"Pos:\s+\d+ \(\+\s*\d+\) \| ", "", l).rsplit("|", 1)[0]
            for l in stdout.splitlines() if l.startswith("Game ")]


failed = 0


def check(name, ok, detail=""):
    global failed
    print(f"[{'PASS' if ok else 'FAIL'}] {name}{(': ' + detail) if detail else ''}")
    failed += 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--v1", required=True)
    ap.add_argument("--v2", default=os.path.join(ROOT, "tools", "datagen"))
    a = ap.parse_args()
    tmp = tempfile.mkdtemp(prefix="datagen_v2_test_")
    t = lambda n: os.path.join(tmp, n)
    try:
        out1 = gen(a.v1, t("v1.bin"))
        out2 = gen(a.v2, t("v2.bin"))
        check("flags off: byte-identical to v1", open(t("v1.bin"), "rb").read() == open(t("v2.bin"), "rb").read()
              and os.path.getsize(t("v1.bin")) > 0, f"{os.path.getsize(t('v1.bin'))} bytes")

        out3 = gen(a.v2, t("f30.bin"), "--max-fifty", "30", "--index", t("f30.csv"))
        v1 = records(t("v1.bin"))
        f30 = records(t("f30.bin"))
        skipped = int(re.search(r"Fifty Skipped\s+:\s+(\d+)", out3).group(1))
        check("--max-fifty 30: same games as v1", games_line(out1) == games_line(out3))
        check("--max-fifty 30: dropped records == skipped count",
              len(v1) - len(f30) == skipped, f"{len(v1)} - {len(f30)} vs skipped {skipped}")
        # the kept records are v1's records in order with some removed
        i = 0
        for r in f30:
            while i < len(v1) and not np.array_equal(v1[i, :67], r[:67]):
                i += 1
            i += 1
        check("--max-fifty 30: kept records are an ordered subset of v1", i <= len(v1))

        out0 = gen(a.v2, t("f0.bin"), "--max-fifty", "0", games=10)
        check("--max-fifty 0: fewer records than v1 and a non-zero skip count",
              len(records(t("f0.bin"))) < len(records(t("v1.bin"))) and int(re.search(r"Fifty Skipped\s+:\s+(\d+)", out0).group(1)) > 0)

        outo = gen(a.v2, t("oe.bin"), "--max-open-eval", "150", "--index", t("oe.csv"))
        rej = int(re.search(r"Openings Rejected:\s+(\d+)", outo).group(1))
        check("--max-open-eval 150: some openings rejected", rej > 0, f"{rej} rejected")
        rows = list(csv.DictReader(open(t("oe.csv"))))
        early = [r for r in rows if r["reason"].startswith("resignation") and int(r["plies"]) <= 8]
        check("--max-open-eval 150: no resignation within 8 plies", not early, f"{len(early)} early resignations")

        for name in ("f30", "oe"):
            rows = list(csv.DictReader(open(t(f"{name}.csv"))))
            total = sum(int(r["records"]) for r in rows)
            contiguous = all(int(rows[k]["first_record"]) == sum(int(r["records"]) for r in rows[:k]) for k in range(len(rows)))
            check(f"--index ({name}): one row per game, counts sum to file size / 68, offsets contiguous",
                  len(rows) == 40 and total == os.path.getsize(t(f"{name}.bin")) // RS and contiguous,
                  f"{len(rows)} games, {total} records")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"\n{'ALL PASSED' if not failed else f'{failed} FAILED'}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
