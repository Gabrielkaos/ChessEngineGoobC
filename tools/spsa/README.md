# SPSA Search Tuner (`tools/spsa/`)

Tunes the search constants in [`src/tune.h`](../../src/tune.h) (pruning
margins, depth limits, NMP/LMR formula terms, ...) by self-play, using SPSA
(simultaneous perturbation stochastic approximation), the method OpenBench uses
for search parameters.

* [`spsa.py`](spsa.py): the tuner (Python 3, standard library only, drives `cutechess-cli`).
* `runs/<name>/`: one directory per tuning run (git-ignored), see
  [Where the tuned constants are](#where-the-tuned-constants-are).

Contents:

1. [Quick start](#quick-start)
2. [How the tuning works](#how-the-tuning-works)
3. [Where the tuned constants are](#where-the-tuned-constants-are)
4. [Applying them to the engine](#applying-them-to-the-engine)
5. [Choosing what to tune](#choosing-what-to-tune)
6. [`run` options](#run-options)
7. [Engine side: `tune.h` and `make tune`](#engine-side-tuneh-and-make-tune)
8. [Practical notes](#practical-notes)

---

## Quick start

```bash
# 1. Build the tunable engine (src/bin/linux/GOOB-2.2-BETA-native-tune)
make -C src tune

# 2. Start a run named "pruning1" (Ctrl-C pauses it; the same command resumes it)
python3 tools/spsa/spsa.py run pruning1 --tc 5+0.05 --workers 6 --iterations 5000

# 3. Look at the tuned values at any time (also works while it runs)
python3 tools/spsa/spsa.py show pruning1

# 4. Apply them and SPRT against the old engine (see "Applying them to the engine")
```

While it runs it prints one line per finished iteration (example):

```
iter 812/5000  games 3248  plus-minus W/L/D 1013/998/1237  470 it/h  eta 8.9h
```

`it/h` and `eta` tell you how long the run will take on this machine.

---

## How the tuning works

The idea: make the engine play many short games against a slightly different
copy of itself, and nudge every constant toward whichever version won.

Each **iteration**:

1. For every constant `theta`, pick a random direction: up or down.
2. Build two engine settings:
   * **plus**: every constant shifted a little in its random direction,
     e.g. `FutilityMargin 65 -> 70`, `LMRBase 75 -> 69`, ...
   * **minus**: the opposite shift,
     e.g. `FutilityMargin 65 -> 60`, `LMRBase 75 -> 81`, ...
3. Let plus and minus play `--pairs` game pairs (default 2 pairs = 4 games)
   with cutechess-cli. Each pair is one opening played with both colours, so
   neither side gets a luckier opening.
4. Move every constant:
   * plus won more games: every constant moves a little toward its plus value;
   * minus won more games: every constant moves toward its minus value;
   * equal: nothing moves.

A single iteration is very noisy (4 games say almost nothing), but the
directions are random each time. Over thousands of iterations the noise
cancels out and each constant drifts toward the values that win more. All
constants are tuned at once from the same games; that is the "simultaneous
perturbation" in SPSA.

Over the run both step sizes shrink, so the values settle instead of
wandering:

* the **shift** (how far plus/minus differ from `theta`) starts at about 2.4x
  the constant's `c_end` from `tune.h` and shrinks to `c_end` by the end;
* the **learning rate** (how far one won iteration moves `theta`) shrinks too.

With `--workers 6`, six iterations run at the same time (six cutechess-cli
processes, one game at a time each). Each one starts from the newest values,
as in OpenBench, so no CPU core waits for the slowest game of a batch.

### The exact formulas

With `N = --iterations`, `A = 0.1 N`, `alpha = 0.602`, `gamma = 0.101`,
`r_end = 0.002`, and `c_end` per parameter (OpenBench's schedule):

```
c_k = c_end * N^gamma / (k+1)^gamma                          shift at iteration k
a_k = r_end * c_end^2 * (A+N)^alpha / (A+k+1)^alpha          gain at iteration k

d       = random +-1 per parameter
theta+  = round(clamp(theta + c_k * d))
theta-  = round(clamp(theta - c_k * d))
theta  += (a_k / c_k) * (wins - losses of theta+) * d        then clamped to [min, max]
```

---

## Where the tuned constants are

Each run keeps everything in `tools/spsa/runs/<name>/`:

| File | Contents |
| :--- | :--- |
| `state.json` | current tuned values (`theta`), game counts and run settings; rewritten after every iteration |
| `history.csv` | every constant after every iteration; plot it to see whether the values have settled |
| `engine` | frozen copy of the binary being tuned, so rebuilding `src/` during a run changes nothing |

The easy way to read them is `show`, which works during or after a run (example output):

```bash
python3 tools/spsa/spsa.py show pruning1
```

```
iterations 5000/5000, 20000 games, tc 5+0.05, 11.2h

parameter                  default   tuned     theta   change   range
FutilityMargin                  65      71     70.84       +6   [30, 130]
LMRBase                         75      72     72.31       -3   [25, 150]
...

cutechess options for the tuned values:
option.FutilityMargin=71 option.LMRBase=72 ...
```

* `tuned` is the value to use (`theta` rounded to an integer).
* `change` is the difference from the current default in `tune.h`.
* The last line is the same values as cutechess-cli options, for testing them
  on a `make tune` binary without editing any source.

---

## Applying them to the engine

The defaults live in `src/tune.h`, so applying a run means changing that file
and rebuilding the normal engine. Then it has to pass an SPRT like any other
change (AGENTS.md).

```bash
# 1. BEFORE applying: build the current engine as the baseline
make -C src native && cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-base

# 2. Write the tuned values into src/tune.h
python3 tools/spsa/spsa.py apply pruning1

# 3. Rebuild the normal engine with them
make -C src native && cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-tuned

# 4. Sanity bench, then SPRT tuned vs baseline
python3 tools/sprt/bench.py tools/sprt/bin/GOOB-tuned 12
tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-tuned tools/sprt/bin/GOOB-base spsa-pruning1 6+0.06 6
```

`apply` only changes the default column of `src/tune.h`, e.g.

```diff
-    TP(FutilityMargin,              65,    30,   130,    5) \
+    TP(FutilityMargin,              71,    30,   130,    5) \
```

so `git diff src/tune.h` shows exactly what changed. If the SPRT passes,
commit `src/tune.h`; if it fails, `git checkout src/tune.h` undoes it.

Do not SPRT or release the `-tune` binary: it is only for tuning and is
slightly slower. The tuned values reach the real engine through `apply` and a
normal build.

---

## Choosing what to tune

By default every constant in `tune.h` is tuned. Fewer constants converge with
fewer games, so it usually pays to tune one group at a time:

```bash
# only the pruning margins (the --exclude drops FutilityPruningDepth, RazoringDepth, ...)
python3 tools/spsa/spsa.py run pruning1 --include 'Futility|SEE|Razor|BetaMargin|probCutMargin' --exclude 'Depth$'

# only LMR
python3 tools/spsa/spsa.py run lmr1 --include '^LMR|AllNode'

# everything except the depth limits
python3 tools/spsa/spsa.py run all1 --exclude '(Depth|LIMIT)$'
```

`--include` / `--exclude` are regular expressions on the parameter names in
`tune.h`. Constants that are not selected stay at their defaults during the run
and are not touched by `apply`.

Depth limits (`...Depth`, `EVAL_MOVE_LIMIT`) are small integers with
`c_end = 1`: they move slowly and cost a lot when wrong, so leaving them out is
a reasonable default.

---

## `run` options

| Option | Default | Meaning |
| :--- | :--- | :--- |
| `--engine` | `src/bin/linux/GOOB-2.2-BETA-native-tune` | `make tune` binary; copied into the run directory on the first start |
| `--iterations` | `5000` | SPSA iterations `N` (fixes the gain schedule) |
| `--pairs` | `2` | game pairs per iteration (4 games) |
| `--tc` | `5+0.05` | cutechess time control |
| `--workers` | CPUs - 2 | iterations played in parallel |
| `--hash` | `16` | hash MB per engine (1 thread each) |
| `--book` | `tools/book.epd` | EPD openings, random order |
| `--include REGEX` | all | only tune matching parameters |
| `--exclude REGEX` | none | skip matching parameters |
| `--r-end` | `0.002` | final learning rate for every parameter |
| `--alpha`, `--gamma`, `--a-ratio` | `0.602`, `0.101`, `0.1` | SPSA schedule |
| `--cutechess` | `cutechess-cli` on `PATH`, else `~/.local/bin/cutechess-cli` | |

When a run resumes, every setting except `--workers` and `--cutechess` comes
from `state.json`, and the frozen `runs/<name>/engine` is used. Games in
progress when you press Ctrl-C are discarded. Adjudication matches
`tools/sprt/run_sprt.sh` (draw after move 40 at |score| <= 10 for 8 moves,
resign at 400 cp for 3 moves).

To start over, delete `tools/spsa/runs/<name>/` or use a new name.

---

## Engine side: `tune.h` and `make tune`

`src/tune.h` lists every tunable as `TP(name, default, min, max, c_end)`.

* Normal builds (`make native`, `make universal`, ...) turn each entry into a
  compile-time constant (an `enum`), so release binaries are unaffected.
* `make tune` (native build with `-DTUNE`, binary
  `src/bin/linux/GOOB-2.2-BETA-native-tune`) turns them into globals and adds:
  * one UCI `spin` option per parameter (`setoption name LMRBase value 80`),
    clamped to `[min, max]`. `LMRBase`/`LMRDivisor` rebuild the LMR table;
  * the `spsa` command (after `uci`), which prints the table in OpenBench's
    SPSA format: `name, int, value, min, max, c_end, r_end`. The same output
    can be pasted into an OpenBench SPSA test.

**Adding a parameter**: add a `TP(...)` line to `src/tune.h` and use the name
in the code instead of the literal. Pick `[min, max]` around the sensible range
and `c_end` of roughly `(max - min) / 20`. Constants that are read only at
startup (like the LMR table) must be recomputed in `tuneSetOption()`
(`src/tune.c`). Rebuild with `make tune` and start a new run.

---

## Practical notes

* **Run size.** SPSA needs many games: OpenBench tunes of 20-60 parameters
  usually use 20k-100k games. 5000 iterations x 4 games = 20k games.
* **Time control.** Short games are fine for SPSA; the result still has to
  pass an SPRT at the normal test time control before it is kept.
* **Has it converged?** Plot columns of `history.csv`; values that are still
  trending at the end of the run would move further with a longer run.
* **CPU.** `--workers` games run at once; keep it below the number of CPU
  threads (6 on the 8-thread dev laptop) so engines do not share cores.
* **NNUE files are never touched**: the engine only reads the network embedded
  at build time, and the tuner only writes under `tools/spsa/runs/` (and
  `src/tune.h` for `apply`).
