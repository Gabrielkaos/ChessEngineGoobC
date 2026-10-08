#!/usr/bin/env python3
"""SPSA tuner for GOOB's search constants (src/tune.h).

Each iteration perturbs every parameter by +-c_k in a random direction, plays a
few game pairs of theta+ against theta- with cutechess-cli, and moves theta
along the direction that scored better. Several iterations run in parallel
(one cutechess-cli process per worker), each starting from the latest theta,
the same asynchronous scheme as OpenBench's SPSA.

  python3 tools/spsa/spsa.py run   <name> [options]   start or resume a run
  python3 tools/spsa/spsa.py show  <name>             current values vs defaults
  python3 tools/spsa/spsa.py apply <name>             write the values into src/tune.h

The engine must be a `make tune` build (src/bin/linux/GOOB-2.2-BETA-native-tune).
Run state lives in tools/spsa/runs/<name>/ (state.json, history.csv, engine copy).
See tools/spsa/README.md.
"""
import argparse
import csv
import json
import math
import os
import random
import re
import shutil
import signal
import subprocess
import sys
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RUNS = os.path.join(ROOT, "tools", "spsa", "runs")
DEFAULT_ENGINE = os.path.join(ROOT, "src", "bin", "linux", "GOOB-2.2-BETA-native-tune")
DEFAULT_BOOK = os.path.join(ROOT, "tools", "book.epd")
TUNE_H = os.path.join(ROOT, "src", "tune.h")
SCORE_RE = re.compile(r"Score of plus vs minus: (\d+) - (\d+) - (\d+)")


# --------------------------------------------------------------------------
# engine parameter table
# --------------------------------------------------------------------------
def read_engine_params(engine):
    """Ask the engine for its tunables (`spsa` command, OpenBench format)."""
    try:
        out = subprocess.run([engine], input="uci\nspsa\nquit\n", capture_output=True,
                             text=True, timeout=60).stdout
    except (OSError, subprocess.TimeoutExpired) as e:
        sys.exit(f"could not run {engine}: {e}")
    params = []
    for line in out.splitlines():
        f = [x.strip() for x in line.split(",")]
        if len(f) == 7 and f[1] == "int":
            params.append({"name": f[0], "default": int(f[2]), "min": int(f[3]),
                           "max": int(f[4]), "c_end": float(f[5]), "r_end": float(f[6])})
    if not params:
        sys.exit(f"{engine} printed no tunable parameters; build it with `make tune` in src/")
    return params


def select_params(params, include, exclude):
    sel = [p for p in params
           if (not include or re.search(include, p["name"]))
           and not (exclude and re.search(exclude, p["name"]))]
    if not sel:
        sys.exit("no parameters left after --include/--exclude")
    return sel


# --------------------------------------------------------------------------
# run state
# --------------------------------------------------------------------------
def run_dir(name):
    return os.path.join(RUNS, name)


def load_state(name):
    path = os.path.join(run_dir(name), "state.json")
    if not os.path.exists(path):
        return None
    with open(path) as f:
        return json.load(f)


def save_state(name, state):
    path = os.path.join(run_dir(name), "state.json")
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(state, f, indent=1)
    os.replace(tmp, path)


def new_state(args, params):
    n = args.iterations
    return {
        "config": {
            "iterations": n, "tc": args.tc, "pairs": args.pairs, "hash": args.hash,
            "book": os.path.abspath(args.book), "alpha": args.alpha, "gamma": args.gamma,
            "A": args.a_ratio * n, "include": args.include, "exclude": args.exclude,
        },
        "params": [dict(p, theta=float(p["default"]),
                        r_end=args.r_end if args.r_end is not None else p["r_end"])
                   for p in params],
        "iter": 0, "wins": 0, "losses": 0, "draws": 0, "seconds": 0.0,
    }


def gains(cfg, p, k):
    """c_k and a_k for parameter p at iteration k (0-based), OpenBench schedule."""
    n, A, alpha, gamma = cfg["iterations"], cfg["A"], cfg["alpha"], cfg["gamma"]
    c = p["c_end"] * n ** gamma
    a = p["r_end"] * p["c_end"] ** 2 * (A + n) ** alpha
    return c / (k + 1) ** gamma, a / (A + k + 1) ** alpha


def clamp(v, p):
    return max(p["min"], min(p["max"], v))


# --------------------------------------------------------------------------
# games
# --------------------------------------------------------------------------
class FatalError(Exception):
    pass


def play(args, cfg, engine, plus, minus, procs, lock):
    """Play cfg['pairs'] game pairs of plus vs minus; return (W, L, D) for plus, or None."""
    cmd = [args.cutechess,
           "-engine", f"cmd={engine}", "name=plus", *[f"option.{k}={v}" for k, v in plus.items()],
           "-engine", f"cmd={engine}", "name=minus", *[f"option.{k}={v}" for k, v in minus.items()],
           "-each", "proto=uci", f"tc={cfg['tc']}", f"option.Hash={cfg['hash']}", "option.Threads=1",
           "-openings", f"file={cfg['book']}", "format=epd", "order=random", "-repeat",
           "-rounds", str(cfg["pairs"]), "-games", "2", "-concurrency", "1",
           "-srand", str(random.getrandbits(31)),
           "-draw", "movenumber=40", "movecount=8", "score=10",
           "-resign", "movecount=3", "score=400", "-recover"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            stdin=subprocess.DEVNULL, text=True, start_new_session=True)
    with lock:
        procs.add(proc)
    out, _ = proc.communicate()
    with lock:
        procs.discard(proc)
    if "doesn't have option" in out or "Unknown option" in out:
        raise FatalError(out + "\nengine rejected a tuning option; is it a `make tune` build?")
    scores = SCORE_RE.findall(out)
    if proc.returncode != 0 or not scores:
        return None
    w, l, d = map(int, scores[-1])
    return (w, l, d) if w + l + d == 2 * cfg["pairs"] else None


def worker(args, name, state, engine, lock, stop, procs, history, t0):
    cfg = state["config"]
    while not stop.is_set():
        with lock:
            if state["_next"] >= cfg["iterations"]:
                return
            k = state["_next"]
            state["_next"] += 1
            plus, minus, step = {}, {}, []
            for p in state["params"]:
                c_k, a_k = gains(cfg, p, k)
                d = random.choice((-1, 1))
                plus[p["name"]] = round(clamp(p["theta"] + c_k * d, p))
                minus[p["name"]] = round(clamp(p["theta"] - c_k * d, p))
                step.append(a_k / c_k * d)

        try:
            res = play(args, cfg, engine, plus, minus, procs, lock)
        except FatalError as e:
            print(e, file=sys.stderr, flush=True)
            state["_fatal"] = True
            stop.set()
            return
        if stop.is_set():
            return
        if res is None:
            print(f"iteration {k}: cutechess failed, result discarded", flush=True)
            continue

        w, l, d = res
        with lock:
            for p, s in zip(state["params"], step):
                p["theta"] = clamp(p["theta"] + s * (w - l), p)
            state["iter"] += 1
            state["wins"] += w
            state["losses"] += l
            state["draws"] += d
            state["seconds"] = state["_seconds0"] + time.time() - t0
            history.writerow([state["iter"], w, l, d] + [round(p["theta"], 3) for p in state["params"]])
            history.flush_file()
            save_state(name, {k2: v for k2, v in state.items() if not k2.startswith("_")})
            report(state)


def report(state):
    cfg = state["config"]
    done, n = state["iter"], cfg["iterations"]
    games = state["wins"] + state["losses"] + state["draws"]
    rate = done / state["seconds"] if state["seconds"] > 0 else 0
    eta = (n - done) / rate / 3600 if rate > 0 else float("nan")
    print(f"iter {done}/{n}  games {games}  plus-minus W/L/D {state['wins']}/{state['losses']}/{state['draws']}"
          f"  {rate * 3600:.0f} it/h  eta {eta:.1f}h", flush=True)


class History:
    def __init__(self, path, names):
        new = not os.path.exists(path)
        self.f = open(path, "a", newline="")
        self.w = csv.writer(self.f)
        if new:
            self.w.writerow(["iter", "W", "L", "D"] + names)

    def writerow(self, row):
        self.w.writerow(row)

    def flush_file(self):
        self.f.flush()


def cmd_run(args):
    os.makedirs(run_dir(args.name), exist_ok=True)
    engine_copy = os.path.join(run_dir(args.name), "engine")
    state = load_state(args.name)

    if state is None:
        if not os.path.exists(args.engine):
            sys.exit(f"engine not found: {args.engine} (run `make tune` in src/)")
        if not os.path.exists(args.book):
            sys.exit(f"opening book not found: {args.book}")
        params = select_params(read_engine_params(args.engine), args.include, args.exclude)
        # tune a frozen copy, so rebuilding src/ during a run changes nothing
        shutil.copy2(args.engine, engine_copy)
        state = new_state(args, params)
        save_state(args.name, state)
        print(f"new run '{args.name}': {len(params)} parameters, {args.iterations} iterations "
              f"x {2 * args.pairs} games, tc {args.tc}")
    else:
        print(f"resuming '{args.name}' at iteration {state['iter']}/{state['config']['iterations']} "
              f"(run options other than --workers/--cutechess come from the saved state)")
        # a params mismatch means the frozen engine is from another tune.h
        known = {p["name"] for p in read_engine_params(engine_copy)}
        missing = [p["name"] for p in state["params"] if p["name"] not in known]
        if missing:
            sys.exit(f"saved engine lacks parameters {missing}")

    if state["iter"] >= state["config"]["iterations"]:
        print("run already finished; see `show` / `apply`")
        return

    if not shutil.which(args.cutechess) and not os.path.exists(args.cutechess):
        sys.exit(f"cutechess-cli not found: {args.cutechess}")

    state["_next"] = state["iter"]
    state["_seconds0"] = state["seconds"]
    history = History(os.path.join(run_dir(args.name), "history.csv"),
                      [p["name"] for p in state["params"]])
    lock, stop, procs = threading.Lock(), threading.Event(), set()
    t0 = time.time()
    threads = [threading.Thread(target=worker, daemon=True,
                                args=(args, args.name, state, engine_copy, lock, stop, procs, history, t0))
               for _ in range(args.workers)]
    print(f"{args.workers} workers, Ctrl-C to pause (the run resumes with the same command)\n", flush=True)
    for t in threads:
        t.start()
    try:
        while any(t.is_alive() for t in threads):
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\nstopping; games in progress are discarded", flush=True)
        stop.set()
        with lock:
            for p in procs:
                try:
                    os.killpg(p.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
        for t in threads:
            t.join()
    with lock:
        save_state(args.name, {k: v for k, v in state.items() if not k.startswith("_")})
    if state.get("_fatal"):
        sys.exit(1)
    print()
    show(state)


# --------------------------------------------------------------------------
# show / apply
# --------------------------------------------------------------------------
def show(state):
    games = state["wins"] + state["losses"] + state["draws"]
    print(f"iterations {state['iter']}/{state['config']['iterations']}, {games} games, "
          f"tc {state['config']['tc']}, {state['seconds'] / 3600:.1f}h\n")
    print(f"{'parameter':<26}{'default':>8}{'tuned':>8}{'theta':>10}{'change':>9}   range")
    for p in state["params"]:
        v = round(p["theta"])
        diff = v - p["default"]
        mark = f"{diff:+d}" if diff else ""
        print(f"{p['name']:<26}{p['default']:>8}{v:>8}{p['theta']:>10.2f}{mark:>9}   [{p['min']}, {p['max']}]")
    print("\ncutechess options for the tuned values:")
    print(" ".join(f"option.{p['name']}={round(p['theta'])}" for p in state["params"]))


def cmd_show(args):
    state = load_state(args.name)
    if state is None:
        sys.exit(f"no run named '{args.name}' in {RUNS}")
    show(state)


def cmd_apply(args):
    state = load_state(args.name)
    if state is None:
        sys.exit(f"no run named '{args.name}' in {RUNS}")
    with open(args.tune_h) as f:
        text = f.read()
    changed = 0
    for p in state["params"]:
        pat = re.compile(r"(TP\(\s*" + re.escape(p["name"]) + r"\s*,\s*)(-?\d+)")
        m = pat.search(text)
        if not m:
            sys.exit(f"{p['name']} not found in {args.tune_h}")
        v = str(round(p["theta"]))
        if v != m.group(2):
            text = text[:m.start(2)] + v.rjust(len(m.group(2))) + text[m.end(2):]
            changed += 1
    with open(args.tune_h, "w") as f:
        f.write(text)
    print(f"updated {changed} defaults in {args.tune_h}; rebuild and SPRT against the old binary")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    r = sub.add_parser("run", help="start or resume a tuning run")
    r.add_argument("name")
    r.add_argument("--engine", default=DEFAULT_ENGINE, help="`make tune` binary (default: %(default)s)")
    r.add_argument("--iterations", type=int, default=5000, help="SPSA iterations (default: %(default)s)")
    r.add_argument("--pairs", type=int, default=2, help="game pairs per iteration (default: %(default)s)")
    r.add_argument("--tc", default="5+0.05", help="time control (default: %(default)s)")
    r.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 2),
                   help="parallel cutechess processes, each plays 1 game at a time (default: %(default)s)")
    r.add_argument("--hash", type=int, default=16, help="hash MB per engine (default: %(default)s)")
    r.add_argument("--book", default=DEFAULT_BOOK, help="EPD opening book (default: tools/book.epd)")
    r.add_argument("--include", help="only tune parameters matching this regex")
    r.add_argument("--exclude", help="do not tune parameters matching this regex")
    r.add_argument("--r-end", type=float, help="override every parameter's r_end (engine default 0.002)")
    r.add_argument("--alpha", type=float, default=0.602, help="a_k decay exponent (default: %(default)s)")
    r.add_argument("--gamma", type=float, default=0.101, help="c_k decay exponent (default: %(default)s)")
    r.add_argument("--a-ratio", type=float, default=0.1, help="A as a fraction of --iterations (default: %(default)s)")
    r.add_argument("--cutechess", default=shutil.which("cutechess-cli") or os.path.expanduser("~/.local/bin/cutechess-cli"))
    r.set_defaults(func=cmd_run)

    s = sub.add_parser("show", help="print the current values of a run")
    s.add_argument("name")
    s.set_defaults(func=cmd_show)

    a = sub.add_parser("apply", help="write a run's values into src/tune.h as the new defaults")
    a.add_argument("name")
    a.add_argument("--tune-h", default=TUNE_H)
    a.set_defaults(func=cmd_apply)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
