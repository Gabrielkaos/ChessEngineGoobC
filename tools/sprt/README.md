# Testing an engine change with SPRT

Everything you need to decide whether a branch is worth merging. Past results are recorded in
[`documentations/search_pr_evaluations_oct2026.md`](../../documentations/search_pr_evaluations_oct2026.md).

## 1. Build the two binaries

```bash
git checkout main && make -C src native -j8 && cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-base
git checkout search/my-idea && make -C src native -j8 && cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-my-idea
```

Always compare against a `GOOB-base` built from the same `main` the branch started from, with
the same compiler flags and the same net. Never compare a `native` build against a `universal` one.
`tools/sprt/bin/` and `tools/sprt/logs/` are git-ignored.

To build an older commit without touching your checkout, use a worktree and point `EVALFILE`
at the net (building only reads it):

```bash
git worktree add --detach /tmp/goob-old a150239
make -C /tmp/goob-old/src native -j8 EVALFILE=$PWD/src/weights/quantised.bin
```

## 2. Run the match

```bash
tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-my-idea tools/sprt/bin/GOOB-base
```

The script uses [fastchess](https://github.com/Disservin/fastchess) when it is installed
(`fastchess` on `PATH` or `~/.local/bin/fastchess`) and falls back to `cutechess-cli` otherwise.
fastchess is preferred because it scores the two games of each opening as one pair
(pentanomial statistics). `tools/book.epd` is an unbalanced book (White scores about 70%), so the
two games of a pair are strongly anti-correlated; cutechess's per-game (trinomial) SPRT
ignores that, overestimates the variance and needs several times more games for a verdict.

fastchess is built from source (it needs `g++` and `make`):

```bash
git clone --depth 1 --branch v1.8.2-alpha https://github.com/Disservin/fastchess.git
make -C fastchess -j8 && install -m 755 fastchess/fastchess ~/.local/bin/fastchess
```

### Modes

| `MODE` | What it answers | Test | Use it for |
|---|---|---|---|
| `gainer` (default) | Does the change gain Elo? | SPRT [0, 3] | new search ideas, tuning results |
| `nonreg` | Is the change at least not worse? | SPRT [-3, 1] | bug fixes, simplifications, cleanups, speedups that change the tree |
| `fixed` | How far apart are two versions? | `ROUNDS` game pairs, no SPRT | regression checks between versions |

Bounds are logistic Elo (`MODEL=normalized` switches to nElo). `ELO0`/`ELO1` override the
bounds of the mode. An SPRT runs until it reaches a verdict (`ROUNDS` defaults to 20000 pairs
as a safety cap); `MODE=fixed` plays 2000 pairs unless `ROUNDS` says otherwise.

Other settings, positionally or through environment variables:

```bash
# bug fix: only show that it does not lose Elo
MODE=nonreg tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-fix tools/sprt/bin/GOOB-base

# longer time control confirmation with 6 parallel games
MODE=nonreg tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-my-idea tools/sprt/bin/GOOB-base my-idea-ltc 30+0.3 6

# regression check: 4000 games between HEAD and an older commit
MODE=fixed ROUNDS=2000 tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-head tools/sprt/bin/GOOB-old regress

# Lazy SMP change: both engines search with 2 threads (concurrency defaults to CPUs / 2)
THREADS=2 tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-smp tools/sprt/bin/GOOB-base

# old cutechess-cli runner
RUNNER=cutechess tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-my-idea tools/sprt/bin/GOOB-base
```

Defaults: 6+0.06, 10 games in parallel (capped at CPUs / `THREADS`), 1 search thread,
32 MB hash, `tools/book.epd` in random order with colors swapped every pair, draw adjudication
after move 40 (8 moves within 10 cp), resignation at 400 cp for 3 moves.

The log is `tools/sprt/logs/<name>.log`, the games `<name>.pgn`. With fastchess an interrupted
match can be resumed from `<name>.state.json` (`fastchess -config file=tools/sprt/logs/<name>.state.json`).

Leave the machine alone while it runs. Browsers, builds or a second match on the same
cores slow both engines unevenly and the result becomes noise.

## 3. Read the result

```bash
grep -E "Elo:|LLR|Ptnml|Games" tools/sprt/logs/my-idea.log | tail -5
```

fastchess prints a block like:

```
Elo: 4.21 +/- 6.10, nElo: 7.02 +/- 10.16
LOS: 91.12 %, DrawRatio: 48.20 %, PairsRatio: 1.12
Games: 4000, Wins: 1102, Losses: 1053, Draws: 1845, Points: 2024.5 (50.61 %)
Ptnml(0-2): [12, 401, 964, 607, 16], WL/DD Ratio: 2.61
LLR: 2.95 (100.2%) (-2.94, 2.94) [0.00, 3.00]
```

`Ptnml(0-2)` counts the game pairs by the points the new engine took from them (0, 0.5 ... 2).

## 4. Acceptance rules

1. **Merge only when the SPRT accepts H1** (LLR reaches +2.94, "H1 was accepted"). For a
   `nonreg` test that means the change is not worse than -3 Elo; for a `gainer` test it
   means it is very likely positive.
2. **Reject when the SPRT accepts H0** (LLR reaches -2.94).
3. **Never stop early and never decide on the Elo number.** A test stopped at LLR -0.3 or
   +0.9 has shown nothing; record it as *inconclusive*, not as a gain or a loss. While the
   `+/-` is larger than the effect you are looking for, the Elo number means nothing.
4. **One idea per test.** If a bundle fails, the parts are still untested. Test each part.
5. **Search changes that pass at 6+0.06 get a `MODE=nonreg` confirmation at a longer time
   control** (for example 30+0.3) before they are considered final.
6. **After a handful of merges, run a `MODE=fixed` regression check** of HEAD against the
   last commit whose strength you trust, so many small changes cannot add up to a loss
   unnoticed.
7. Record games, W/L/D, Elo ± error, LLR (with bounds), pentanomial counts, time control,
   threads, hash and runner in `documentations/`.

## 5. Sanity checks before a long match

A quick fixed-depth bench catches crashes and gross node-count regressions in a minute:

```bash
python3 tools/sprt/bench.py tools/sprt/bin/GOOB-my-idea 12
```

It prints best move, score, nodes and time per position and a total at the end. A search
change should keep the best moves mostly stable and usually lowers total nodes. A crash or a
missing `bestmove` shows up as the script exiting with "engine died".

### Measuring pure NPS performance changes (A/B testing)

When making optimizations aimed at speed/NPS without altering search trees (node-identical changes), use `compare_nps.py` for interleaved multi-round statistical comparison:

```bash
python3 tools/sprt/compare_nps.py <base_binary> <new_binary> [depth=12] [rounds=10]
```

This interleaves engine executions across multiple rounds, computing mean NPS, standard deviations, and percentage delta.

## Notes

- `GOOB` quits as soon as its stdin closes, so `echo "go depth 10" | ./GOOB` prints nothing.
  Use the bench script or an interactive session.
- The binaries `cutechess-cli` and `fastchess` can be overridden with `CUTECHESS=/path/...`
  and `FASTCHESS=/path/...`.
