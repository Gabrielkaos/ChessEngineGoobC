# Testing a search change with SPRT

Everything you need to decide whether a `search/*` branch is worth merging.

## 0. What is on the table

Nine branches, each with its own pull request. Eight are single ideas, one is all of them together.

| Branch | What it changes | Where |
|---|---|---|
| `search/history-updates` | Quiet malus when a capture causes the cutoff, bonus for the opponent's prior quiet move on fail-low | `history.c`, end of `AlphaBeta` |
| `search/qsearch-tt-store` | Quiescence stores its result (and best capture) in the TT at depth 0 | `Quiescence` |
| `search/tt-value-as-eval` | A bounded TT score replaces the static eval for pruning decisions | `Quiescence`, `AlphaBeta` eval setup |
| `search/probcut-tt` | Skip probcut when the TT already refutes it; store probcut hits | probcut block |
| `search/lmr-deeper` | Re-search one ply deeper / shallower after a reduced search beats alpha | LMR block, `search.h` |
| `search/pawn-history-key` | Pawn history keyed on pawns only (kings stripped out of `pkHash`) | `history.c` |
| `search/capture-pruning` | Capture futility pruning plus history-aware SEE margin | move loop, `search.h` |
| `search/ttcapture-lmr` | One extra ply of reduction when the TT move is a capture | LMR block |
| `search/all-improvements` | All eight together | |

Prebuilt native binaries for each branch are in `tools/sprt/bin/GOOB-<branch-name>` on the
test machine, next to `GOOB-base` (built from `main`). They are not committed; rebuild them
with step 1 if they are missing.

## 1. Build the two binaries

```bash
git checkout main && make -C src native -j8 && cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-base
git checkout search/history-updates && make -C src native -j8 && cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-history-updates
```

Always compare against a `GOOB-base` built from the same `main` the branch started from, with
the same compiler flags. Never compare a `native` build against a `universal` one.

## 2. Run the match

```bash
tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-history-updates tools/sprt/bin/GOOB-base
```

Defaults: 6+0.06 time control, 6 games in parallel, SPRT bounds [0, 5] Elo, 32 MB hash,
`tools/book.epd` openings with colors swapped every pair. Change them positionally or
through environment variables:

```bash
# slower, more careful: 12+0.1 with 4 parallel games (what SURPRISE_SRD.md used)
tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-lmr-deeper tools/sprt/bin/GOOB-base lmr-deeper 12+0.1 4

# non-regression test (is the change at least not worse?)
ELO0=-5 ELO1=0 tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-pawn-history-key tools/sprt/bin/GOOB-base
```

The log is written to `tools/sprt/logs/<name>.log`. The match stops on its own when the SPRT
reaches a verdict, or after 3000 games.

Leave the machine alone while it runs. Browsers, builds or a second match on the same
cores slow both engines unevenly and the result becomes noise.

## 3. Read the result

```bash
grep -E "Score of|Elo diff|SPRT" tools/sprt/logs/GOOB-history-updates.log | tail -3
```

You will see something like:

```
Score of NEW vs BASE: 78 - 80 - 136  [0.497] 294
Elo difference: -6.2 +/- 29.9, LOS: 34.2 %, DrawRatio: 46.1 %
SPRT: llr -0.187 (-6.4%), lbound -2.94, ubound 2.94
```

- **LLR reaches +2.94** ("H1 was accepted"): the change is very likely worth at least `ELO0`
  and probably close to `ELO1`. Merge the PR.
- **LLR reaches -2.94** ("H0 was accepted"): the change is not an improvement. Close the PR,
  or try different constants.
- **Neither after 3000 games**: the effect is smaller than the bounds can resolve. Treat it
  as neutral; keep it only if it simplifies the code.

The `+/-` on the Elo line is the 95% error bar. While it is larger than the effect you are
looking for, the Elo number itself means nothing. A few hundred games always look like this;
expect 1000 to 3000 games for a 5 Elo change.

## 4. Suggested order

1. `search/all-improvements` first. If it passes, merge it and you are done.
2. If it fails or stays neutral, test the eight single branches. Merge the ones that pass,
   then rebase and re-test the combination of the winners.
3. Patches that only add pruning (`capture-pruning`, `ttcapture-lmr`, `lmr-deeper`) are the
   most likely to need their constants tuned. The constants are all at the top of `search.h`.

Partial result already in hand (measured against the previous main, b33ac56, before the history-formula and tuning commits) for the first five ideas combined (`history-updates`,
`qsearch-tt-store`, `tt-value-as-eval`, `probcut-tt`, `lmr-deeper`) at 6+0.06:
294 games, 78-80-136, -6 ± 30 Elo, LLR -0.19. Inconclusive.

## 5. Sanity checks before a long match

A quick fixed-depth bench catches crashes and gross node-count regressions in a minute:

```bash
python3 tools/sprt/bench.py tools/sprt/bin/GOOB-history-updates 12
```

It prints best move, score, nodes and time per position and a total at the end. A search
change should keep the best moves mostly stable and usually lowers total nodes. A crash or a
missing `bestmove` shows up as the script exiting with "engine died".

## Notes

- `GOOB` quits as soon as its stdin closes, so `echo "go depth 10" | ./GOOB` prints nothing.
  Use the bench script or an interactive session.
- With `tools/book.epd` at fast time controls white wins far more often than black. The
  colors are swapped for every opening so the comparison stays fair, but a balanced book
  (for example a UHO or 8-move book) would cut the noise and finish faster.
- `cutechess-cli` is expected on `PATH` or at `~/.local/bin/cutechess-cli`; override with
  `CUTECHESS=/path/to/cutechess-cli`.
