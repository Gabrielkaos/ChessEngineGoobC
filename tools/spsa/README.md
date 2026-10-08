# SPSA Search Tuner (`tools/spsa/`)

Tunes the search constants in [`src/tune.h`](../../src/tune.h) by self-play,
using SPSA (simultaneous perturbation stochastic approximation), the method
OpenBench uses for search parameters.

* [`spsa.py`](spsa.py): the tuner (Python 3, standard library only, drives `cutechess-cli`).
* `runs/<name>/`: one directory per tuning run (git-ignored): `state.json`
  (current values, game counts, settings), `history.csv` (every parameter after
  every iteration) and `engine` (a frozen copy of the binary being tuned).

## How it works

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

One SPSA iteration:

1. Pick a random sign `d = +-1` for every parameter, and form
   `theta+ = theta + c_k*d` and `theta- = theta - c_k*d` (rounded, clamped).
2. Play `--pairs` game pairs (same opening, colours swapped) of `theta+` against
   `theta-` with cutechess-cli.
3. Update `theta += (a_k / c_k) * (wins - losses of theta+) * d`.

The gains follow OpenBench's schedule, with `N = --iterations`, `A = 0.1 N`:
`c_k = c_end * N^gamma / (k+1)^gamma` (so the perturbation shrinks to `c_end`)
and `a_k = r_end * c_end^2 * (A+N)^alpha / (A+k+1)^alpha`, with `alpha = 0.602`,
`gamma = 0.101`, `r_end = 0.002`.

`--workers` iterations run at the same time, one cutechess-cli process each
(one game at a time per process). Each starts from the latest theta, as in
OpenBench, so no CPU sits idle waiting for the slowest game of a batch.

## Usage

```bash
# 1. Build the tunable engine
make -C src tune

# 2. Start a run (Ctrl-C pauses it; the same command resumes it)
python3 tools/spsa/spsa.py run pruning1 --tc 5+0.05 --workers 6 --iterations 5000

# 3. Look at the values at any time (also works while it runs)
python3 tools/spsa/spsa.py show pruning1

# 4. Keep a baseline binary, write the tuned values into src/tune.h, rebuild
make -C src native && cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-base
python3 tools/spsa/spsa.py apply pruning1
make -C src native && cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-tuned

# 5. Verify the tuned values like any other change (AGENTS.md)
python3 tools/sprt/bench.py tools/sprt/bin/GOOB-tuned 12
tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-tuned tools/sprt/bin/GOOB-base spsa-pruning1 6+0.06 6
```

`apply` only rewrites the default column of `src/tune.h`, so `git diff`
shows the tuned values and `git checkout src/tune.h` undoes them.

### `run` options

| Option | Default | Meaning |
| :--- | :--- | :--- |
| `--engine` | `src/bin/linux/GOOB-2.2-BETA-native-tune` | `make tune` binary; copied into the run directory on the first start |
| `--iterations` | `5000` | SPSA iterations `N` (fixes the gain schedule) |
| `--pairs` | `2` | game pairs per iteration (4 games) |
| `--tc` | `5+0.05` | cutechess time control |
| `--workers` | CPUs - 2 | iterations played in parallel |
| `--hash` | `16` | hash MB per engine (1 thread each) |
| `--book` | `tools/book.epd` | EPD openings, random order |
| `--include REGEX` | all | only tune matching parameters (others stay at their defaults) |
| `--exclude REGEX` | none | skip matching parameters |
| `--r-end` | `0.002` | final learning rate for every parameter |
| `--alpha`, `--gamma`, `--a-ratio` | `0.602`, `0.101`, `0.1` | SPSA schedule |
| `--cutechess` | `cutechess-cli` on `PATH`, else `~/.local/bin/cutechess-cli` | |

When a run resumes, every setting except `--workers` and `--cutechess` comes
from `state.json`, and the frozen `runs/<name>/engine` is used. Games in
progress when you press Ctrl-C are discarded. Adjudication matches
`tools/sprt/run_sprt.sh` (draw after move 40 at |score| <= 10 for 8 moves,
resign at 400 cp for 3 moves).

## Practical notes

* **Run size.** SPSA needs many games: OpenBench tunes of 20-60 parameters
  usually use 20k-100k games. 5000 iterations x 4 games = 20k games. Tuning a
  related group at a time (`--include 'Futility|SEE|Razor|BetaMargin'`) converges
  faster than all 50+ parameters at once.
* **Depth limits** (`...Depth`, `EVAL_MOVE_LIMIT`) are small integers with
  `c_end = 1`; they move slowly and cost a lot when wrong. Leave them out with
  `--exclude '(Depth|LIMIT)$'` unless you want them tuned.
* **Time control.** Short games are fine for SPSA; the result still has to
  pass an SPRT at the normal test time control before it is kept.
* **The `tune` binary is slightly slower** (the parameters are loads, not
  immediates). Both sides of every SPSA game use it, so the comparison is fair,
  but never SPRT or release with it: `apply`, rebuild normally, then SPRT.
* **Adding a parameter**: add a `TP(...)` line to `src/tune.h` and use the name
  in the code instead of the literal. Pick `[min, max]` around the sensible
  range and `c_end` of roughly `(max - min) / 20`. Constants that are read only
  at startup (like the LMR table) must be recomputed in `tuneSetOption()`
  (`src/tune.c`).
* NNUE files are never touched: the engine only reads the network embedded at
  build time, and the tuner only writes under `tools/spsa/runs/` (and
  `src/tune.h` for `apply`).
