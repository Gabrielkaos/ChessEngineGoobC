#!/usr/bin/env python3
"""
Tests for the Round 2 trainer and mixer flags (NNUE_IMPROVEMENT_PLAN_ROUND2.md 8.6).

Reads a few hundred thousand records from an existing self-play file and
writes everything else into --work (a new, empty scratch directory). Runs on
the CPU so results are bit-exact (CUDA embedding_bag backward is not).

    python3 test_r2_tools.py --gen ../data_gen/gen_w0_1792446417.bin \
        --base ../data/train.bin --old-train /path/to/train_before_r2.py --work /tmp/r2test
"""

import argparse
import os
import signal
import subprocess
import sys
import time

import numpy as np
import torch

HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
RS = 68


def run(cmd, **kw):
    env = dict(os.environ, CUDA_VISIBLE_DEVICES="")
    return subprocess.run(cmd, capture_output=True, text=True, env=env, cwd=HERE, **kw)


def weights(path):
    return torch.load(path, map_location="cpu")["model_state_dict"]


def same(a, b):
    return all(torch.equal(a[k], b[k]) for k in a)


def check(name, ok, detail=""):
    print(f"[{'PASS' if ok else 'FAIL'}] {name}{(': ' + detail) if detail else ''}")
    if not ok:
        check.failed += 1


check.failed = 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", required=True)
    ap.add_argument("--base", required=True)
    ap.add_argument("--old-train", required=True, help="train.py as it was before the Round 2 flags")
    ap.add_argument("--work", required=True)
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=False)
    w = lambda *p: os.path.join(a.work, *p)

    gen = np.memmap(a.gen, dtype=np.uint8, mode="r").reshape(-1, RS)
    base = np.memmap(a.base, dtype=np.uint8, mode="r").reshape(-1, RS)
    np.asarray(gen[:300_000]).tofile(w("train.bin"))
    np.asarray(gen[300_000:320_000]).tofile(w("val_gen.bin"))
    np.asarray(base[:20_000]).tofile(w("val.bin"))

    # train.py does not seed the model's random init, so every run warm-starts
    # from one saved checkpoint (as every Round 2 run does with --init)
    sys.path.insert(0, HERE)
    from model import NNUE
    torch.manual_seed(0)
    torch.save({"model_state_dict": NNUE(mirror=True, king_buckets=4).state_dict()}, w("init.pt"))

    common = ["--init", w("init.pt"), "--train", w("train.bin"), "--val", w("val.bin"), "--batch-size", "1024",
              "--workers", "0", "--mirror", "--king-buckets", "4", "--optimizer", "adamw",
              "--bucket-decay", "1e-4", "--lr", "1e-3", "--seed", "3", "--save-interval", "50"]

    # 1. default flags reproduce the old trainer exactly (2 epochs = 584 batches)
    r_old = run([PY, a.old_train] + common + ["--epochs", "2", "--out-best", w("old_best.pt"), "--out-last", w("old_last.pt")])
    r_new = run([PY, "train.py"] + common + ["--epochs", "2", "--out-best", w("new_best.pt"), "--out-last", w("new_last.pt")])
    lines = lambda r: [l for l in r.stdout.splitlines() if "complete" in l or "Best val" in l]
    strip = lambda ls: [l.split("| Time")[0] for l in ls]
    check("default flags: same epoch losses as the old trainer", strip(lines(r_old)) == strip(lines(r_new)) and len(lines(r_new)) == 3,
          f"{strip(lines(r_new))} vs {strip(lines(r_old))}" if strip(lines(r_old)) != strip(lines(r_new)) else "")
    check("default flags: identical final weights", same(weights(w("old_last.pt")), weights(w("new_last.pt"))))
    check("default flags: identical best checkpoint", same(weights(w("old_best.pt")), weights(w("new_best.pt"))))

    # 2. --total-samples stops at the right step (400 steps = 1.37 epochs of 292 batches)
    budget = ["--total-samples", str(400 * 1024), "--lr-schedule", "step-cosine", "--select-by", "last",
              "--val-extra", w("val_gen.bin"), "--val-every", "100", "--curve-val-max", "5000"]
    r_full = run([PY, "train.py"] + common + budget + ["--out-best", w("full_best.pt"), "--out-last", w("full_last.pt")])
    ck = torch.load(w("full_last.pt"), map_location="cpu")
    check("--total-samples: stops after 400 steps", ck.get("global_step") == 400, f"global_step={ck.get('global_step')}")
    lr_end = ck["optimizer_state_dict"]["param_groups"][0]["lr"]
    check("step-cosine: LR annealed to --lr-min", abs(lr_end - 1e-5) < 1e-9, f"lr={lr_end}")
    check("--select-by last: best == last", same(weights(w("full_best.pt")), weights(w("full_last.pt"))))
    curve = open(w("full_last_curve.csv")).read().splitlines()
    check("--val-every: 4 curve rows with pure-eval column", len(curve) == 5 and "val_gen_eval_mse" in curve[0], curve[0])
    check("--val-extra: blended and eval MSE printed", "val_gen" in r_full.stdout and "(eval " in r_full.stdout)

    # 3. an interrupted run resumes to the same final weights
    env = dict(os.environ, CUDA_VISIBLE_DEVICES="")
    p = subprocess.Popen([PY, "train.py"] + common + budget + ["--out-best", w("int_best.pt"), "--out-last", w("int_last.pt")],
                         cwd=HERE, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    # wait until mid-way through the second epoch, then Ctrl-C
    deadline = time.time() + 600
    while time.time() < deadline:
        if os.path.exists(w("int_last.pt")):
            g = torch.load(w("int_last.pt"), map_location="cpu").get("global_step", 0)
            if g >= 300:
                break
        time.sleep(0.5)
    p.send_signal(signal.SIGINT)
    p.wait()
    g_int = torch.load(w("int_last.pt"), map_location="cpu").get("global_step")
    run([PY, "train.py"] + common + budget + ["--resume", w("int_last.pt"), "--out-best", w("int_best.pt"), "--out-last", w("int_last.pt")])
    check("mid-run resume: same final weights as uninterrupted", 300 <= (g_int or 0) < 400 and same(weights(w("int_last.pt")), weights(w("full_last.pt"))),
          f"interrupted at step {g_int}")

    # 4. mixer
    mix = [PY, "mix_data.py", "--base-data", w("val.bin"), "--seed", "1"]
    r = run(mix + ["--gen-share", "0", "--max-total", "15000", "--out", w("base_only.bin")])
    bo = np.fromfile(w("base_only.bin"), dtype=np.uint8).reshape(-1, RS)
    check("mix --gen-share 0: base-only file, no results", len(bo) == 15000 and not np.any((bo[:, 67] >> 1) & 3), r.stderr[-300:])
    r = run(mix + ["--gen-data", w("train.bin") + ":5000", "--gen-data", w("val_gen.bin"), "--exclude", w("val_gen.bin"),
                   "--gen-share", "0.5", "--max-total", "10000", "--out", w("mixed.bin")])
    mx = np.fromfile(w("mixed.bin"), dtype=np.uint8).reshape(-1, RS)
    held = {bytes(x) for x in np.fromfile(w("val_gen.bin"), dtype=np.uint8).reshape(-1, RS)}
    check("mix --exclude: no hold-out record in the output", not any(bytes(x) in held for x in mx), r.stdout[-200:])
    check("mix caps: 5000 gen records (capped source) + 5000 base", len(mx) == 10000 and int(np.sum((mx[:, 67] >> 1) & 3 > 0)) == 5000,
          r.stdout[-400:] + r.stderr[-400:])

    print(f"\n{'ALL PASSED' if not check.failed else f'{check.failed} FAILED'}")
    return 1 if check.failed else 0


if __name__ == "__main__":
    sys.exit(main())
