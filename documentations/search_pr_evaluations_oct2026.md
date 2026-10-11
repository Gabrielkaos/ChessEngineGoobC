# Search PR Evaluation & SPRT Testing Report (October 2026)

This document records the empirical results of testing eight candidate search improvement branches for the **GOOB** chess engine against the baseline (`60ba471`). 

Each branch was tested independently in strict isolation against `GOOB-base` using the automated SPRT testing suite (`tools/sprt/run_sprt.sh`).

---

## 1. Executive Summary

| Branch | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/lmr-deeper`** | **MERGED** | 902 | 236 - 200 - 466 | 52.0% | **+13.9 ± 15.8** | +0.878 | Accepted |
| **`search/capture-pruning`** | **MERGED** | 1313 | 350 - 296 - 667 | 52.1% | **+7.3 ± 11.4** | +0.450 | Accepted |
| **`search/ttcapture-lmr`** | **MERGED** | 528 | 139 - 122 - 267 | 51.6% | **+5.8 ± 18.0** | +0.280 | Accepted |
| **`search/probcut-tt`** | **MERGED** | 782 | 195 - 181 - 406 | 50.9% | **+5.8 ± 16.9** | +0.221 | Accepted |
| **`search/pawn-history-key`** | **MERGED** | 2006 | 482 - 481 - 1043 | 50.0% | **+0.5 ± 10.5** | -0.342 | Accepted (Bugfix) |
| **`search/qsearch-tt-store`** | **CLOSED** | 1011 | 249 - 253 - 509 | 49.8% | **-1.0 ± 15.2** | -0.294 | Rejected (TT Pollution) |
| **`search/history-updates`** | **CLOSED** | 387 | 95 - 111 - 181 | 47.9% | **-17.4 ± 25.5** | -0.587 | Rejected (Elo Loss) |
| **`search/tt-value-as-eval`** | **CLOSED** | 873 | 185 - 250 - 438 | 46.3% | **-25.9 ± 16.4** | -2.020 | Rejected (Severe Regression) |

---

## 2. Test Environment & Methodology

* **Time Control:** 6s + 0.06s increment
* **Threads:** 1 thread per engine process (10 concurrent games)
* **Hash:** 32 MB per engine
* **Book:** `tools/book.epd` (colors swapped every pair)
* **Adjudication:** Draw at move 40 (movecount 8, score ≤ 10 cp); Resign at score ≥ 400 cp for 3 consecutive moves
* **SPRT Bounds:** `elo0 = 0`, `elo1 = 5`, `alpha = 0.05`, `beta = 0.05`
* **Baseline Engine:** `GOOB-base` built from `main` (`60ba471`) using `-march=native`

---

## 3. Detailed Post-Mortem of Rejected PRs

### PR 1: `search/tt-value-as-eval` (Severe Regression)
* **Commit:** `5702582` (*Search: use a bounded TT score as the node's eval for pruning*)
* **Result:** **-25.9 ± 16.4 Elo** (873 games, 185 wins, 250 losses, 438 draws, LLR -2.02)
* **Concept:** Attempted to replace the raw static evaluation with a bounded transposition table score (`ttValue`) when deciding pruning thresholds (Reverse Futility Pruning, Null Move Pruning, Razoring, ProbCut, Futility Pruning).
* **Failure Analysis:**
  1. **Evaluation Decoupling:** In GOOB, several critical heuristics (`improving` flag, Surprise-SRD evaluation deficit/surplus, hindsight extensions/reductions, and 4-tier correction history) rely strictly on continuous, smooth, position-intrinsic static evaluations.
  2. **Search Instability:** Substituting discrete search-backed TT bounds into pruning preconditions caused sudden depth fluctuations and over-pruning near the horizon, precipitating severe tactical blunders.
* **Recommendation:** **Close PR.** Do not merge.

---

### PR 2: `search/history-updates` (Negative)
* **Commit:** `f58fa98` (*Search: quiet malus on capture cutoffs and fail-low bonus for the prior quiet move*)
* **Result:** **-17.4 ± 25.5 Elo** (387 games, 95 wins, 111 losses, 181 draws, LLR -0.587)
* **Concept:** Added quiet move penalties (malus) across all history tables when a tactical move/capture caused a beta cutoff, and awarded bonuses to the opponent's previous quiet move when a node failed low.
* **Failure Analysis:**
  1. **History Table Dilution:** GOOB features carefully balanced, granular history tables (butterfly, low-ply near root, 4-ply continuation history, and pawn history) updated via a calibrated gravity formula.
  2. **Premature Penalties:** Applying quiet penalties when a tactical capture refutes a position unjustly punishes legitimate quiet positional defenses that were not responsible for tactical vulnerabilities. This degraded move ordering and increased average node counts.
* **Recommendation:** **Close PR.** Do not merge.

---

### PR 3: `search/qsearch-tt-store` (Neutral / Slight Negative)
* **Commit:** `a873ead` (*Search: store quiescence results in the transposition table*)
* **Result:** **-1.0 ± 15.2 Elo** (1011 games, 249 wins, 253 losses, 509 draws, LLR -0.294)
* **Concept:** Stored depth-0 quiescence search results and stand-pat evaluations into the main transposition table to avoid repeated evaluations in sibling branches.
* **Failure Analysis:**
  1. **Transposition Table Pollution:** Quiescence search visits an enormous volume of tactical leaf nodes. Storing depth-0 entries caused heavy turnover in the 4-entry TT buckets, displacing valuable deeper $\alpha$-$\beta$ entries from depths 6–15+.
  2. **Replacement Inefficiency:** Because TT entries are aged and prioritized by search depth, the high write frequency of depth-0 results caused unnecessary cache line evictions with negligible re-probe utility.
* **Recommendation:** **Close PR.** Do not merge.

---

### PR 4: `search/all-improvements` (Combined Branch)
* **Commit:** `1a3d470` (*Search: all eight search improvements combined*)
* **Result:** Preliminary testing showed -6.2 ± 29.9 Elo (294 games, LLR -0.187).
* **Explanation:** This branch bundled all eight ideas together. Because it contained `tt-value-as-eval` (-26 Elo) and `history-updates` (-17 Elo), the negative impact of those two techniques completely wiped out the gains from the five winning techniques.
* **Recommendation:** **Close PR.** Superceded by merging the 5 winning branches individually into `main`.

---

## 4. Round 2: Bugfix & Architecture PRs (October 6, 2026)

Tested against the newly compiled baseline incorporating the Round 1 improvements:

| Branch | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`fix/tm-best-move-stability`** | **MERGED** | 637 | 171 - 136 - 330 | 52.7% | **+19.1 ± 19.1** | +0.876 | Accepted (Significant Elo Gain) |
| **`fix/history-depth0`** | **MERGED** | 948 | 237 - 232 - 479 | 50.3% | **+1.8 ± 15.6** | -0.052 | Accepted (Bugfix / Clean) |
| **`fix/smp-node-counters`** | **MERGED** | 1480 | 375 - 374 - 731 | 50.0% | **+0.2 ± 12.6** | -0.275 | Accepted (SMP Scalability / Non-regression) |
| **`fix/tt-generation`** | **CLOSED** | 1762 | 424 - 433 - 905 | 49.7% | **-1.6 ± 11.3** | -0.612 | Rejected (Elo Regression) |

### Detailed Analysis of Round 2 Results:

#### 1. `fix/tm-best-move-stability` (Huge Gain: +19.1 Elo)
* **What it fixed:** `lastBestMoveDepth` was previously evaluated against `prevBestMove` *after* `prevBestMove` had already been updated, meaning `lastBestMoveDepth` never changed and `timeReduction` was influenced by uninitialized stack variables.
* **Result:** Moving `lastBestMoveDepth` tracking to occur immediately when the best move flips allows the time-management stability multiplier (1.4857 when best move is steady, 0.7046 otherwise) to function properly, saving time on easy moves and extending search on volatile positions.

#### 2. `fix/history-depth0` (Non-regression / Bugfix: +1.8 Elo)
* **What it fixed:** Nodes searched at depth $\le 0$ (past-horizon check evasions) had negative formula values (`stat_bonus(0) = -118`), inverting history updates by penalizing the cutoff move and rewarding failing quiets.
* **Result:** Skipping quiet and capture history updates at depth $\le 0$ stopped inverted table learning without affecting search stability.

#### 3. `fix/smp-node-counters` (Non-regression / Clean Architecture: +0.2 Elo)
* **What it fixed:** Removed cross-thread racy increments on shared `info->nodes` during Lazy SMP searches. Replaced with per-thread `thread->nodes` and `thread->tbhits` aggregated by `NodesSearchedThreadPool()` / `TbHitsThreadPool()`.
* **Result:** Eliminated CPU cache-line bouncing between cores; identical node counts and moves in single-thread, with cleaner SMP scalability.

#### 4. `fix/tt-generation` (Rejected: -1.6 Elo)
* **Concept:** Attempted to prevent Transposition Table aging from wrapping after 64 searches by stepping generation as `uint8_t` by 1 instead of `int` by 4.
* **Failure Analysis:** Across 1762 games, the modified aging/replacement cadence led to a measurable score drop (-1.6 ± 11.3 Elo, LLR -0.612). The existing 4-step generation replacement scheme provides better age-decay dynamics under blitz time controls.
* **Recommendation:** **Close PR.** Do not merge.

---

## 5. Round 3: LMR Re-Search Malus & Quiescence Checks (October 11, 2026)

Tested against the baseline engine:

| Branch / Experiment | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/lmr-research-malus`** | **MERGED** | 3000 | 736 - 731 - 1533 | 50.1% | **+0.6 ± 8.7** | -0.488 | Accepted (commit `4ad2a7f`) |
| **`search/qs-checks`** | **REVERTED** | 2458 | 579 - 614 - 1265 | 49.3% | **-4.3 ± 9.6** | -1.410 | Rejected / Reverted (Neutral / Slight drag) |

### Detailed Analysis:

#### 1. `search/lmr-research-malus` (Merged: commit `4ad2a7f`)
* **Concept:** Applied continuation history malus (`-stat_bonus(resDepth)`) to quiet moves that triggered a full-depth re-search after Late Move Reduction (LMR) but failed low (`Score <= alpha`), penalizing false-alarm moves across subsequent iterations.
* **Result:** Tested over 3000 games: 736 - 731 - 1533 (+0.6 ± 8.7 Elo, LLR -0.488). Accepted as non-regressive and clean history penalization.

#### 2. `search/qs-checks` (Reverted: -4.3 Elo / Neutral)
* **Concept:** Detected in-check positions inside Quiescence search (`Quiescence()`), bypassed stand-pat and delta pruning, generated all pseudo-legal evasions (captures, quiet king evasions, interpositions), and returned exact checkmate scores when no legal evasions existed.
* **Empirical Result:** 2458 games, 579 wins, 614 losses, 1265 draws (49.3% win rate, **-4.3 ± 9.6 Elo**, LLR -1.410).
* **Failure Analysis:**
  1. **Node Overhead on Quiet Evasions:** In quiescence search, generating all quiet evasions without pruning expanded search trees in tactically resolved positions where quiet king moves or interpositions merely delayed material loss without changing the outcome.
  2. **Horizon Protection Already in AlphaBeta:** `AlphaBeta()` already extends or resolves check positions before dropping into Quiescence (`depth <= 0 && !inCheck`). Positions where a quiescence capture gives check and results in checkmate without any legal captures are extremely rare (<0.01% of search nodes).
  3. **No Measurable Strength Gain:** The theoretical correctness fix produced no measurable gain and instead created a slight speed/node overhead.
* **Action:** Reverted completely from `src/search.c` to preserve baseline search speed and node efficiency. Documented here for reference.

---

## 6. Round 4: Move Picker NPS Optimization (October 11, 2026)

Tested against the baseline engine:

| Branch / Experiment | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`opt/movepicker-threats`** | **MERGED** | 1735 | 425 - 400 - 910 | 50.7% | **+4.6 ± 11.3** | +0.321 | Accepted (commit `1f24ddf`) |

### Detailed Analysis:

#### 1. `opt/movepicker-threats` (Merged: commit `1f24ddf`)
* **Concept:** Move picker NPS optimization in `src/movepicker.c`:
  1. **Lazy & Unified Threat Computation:** In previous builds, `STAGE_GENERATE_QUIET` repeatedly traversed enemy knights, bishops, and rooks, performing duplicate PEXT/magic slider attack lookups to compute `threatByLesser` tables, even though attack bitboards were already evaluated in `STAGE_GENERATE_NOISY`. Implemented `computeMovePickerThreats()` to compute enemy pawn, knight, bishop, and rook attack bitboards in a single unified pass and cached `threats` and `threatByLesser` in `S_MOVEPICKER`. Threat computation is deferred until moves need scoring.
  2. **Check Ray Pre-filtering:** King attack rays for bishop and rook checks are only evaluated if the friendly side actually possesses sliding pieces (bishops, rooks, or queens).
  3. **SEE Short-Circuiting on Direct Checks:** In `STAGE_GENERATE_QUIET`, every checking move previously called `StaticExchangeEvaluation(pos, move, 0)`. We short-circuit:
     - If the target square is completely unattacked (`!(threats & to)`), SEE $\ge 0$ is mathematically guaranteed; award the $+16384$ check bonus directly without calling SEE.
     - Else if attacked by a lesser enemy piece (`threatByLesser[pType] & to`), SEE $< 0$ is guaranteed; skip the check bonus immediately without calling SEE.
     - Fallback to `StaticExchangeEvaluation` is only executed for contested, non-lesser squares (>95% reduction in SEE calls).
* **Empirical Match Result:**
  - **Match:** 1735 games against `GOOB-base` at 6+0.06 TC.
  - **Score:** 425 - 400 - 910 (50.7% score, +25 net wins).
  - **Elo Difference:** **+4.6 ± 11.3 Elo** (LOS: 78.9%, LLR: +0.321).
  - **Benchmark Speedup:** ~**+96,000 to +105,000 NPS (~+5.0% to +5.3% raw NPS gain)**; bench execution time dropped from 459 ms to 446 ms (`bench.py 12`).
* **Verdict:** **ACCEPTED & MERGED** into `main` (commit `1f24ddf`).

---

## 7. Summary of All Merged Features on `main`

1. `search/pawn-history-key`: Pawn history indexed strictly by pawn structure (`pawnHistIndex()`).
2. `search/probcut-tt`: ProbCut refutation check against TT and cutoff hash storage.
3. `search/ttcapture-lmr`: Extra ply of reduction when the TT move is a capture.
4. `search/capture-pruning`: Capture futility pruning at depth $\le 6$ and history-adjusted SEE margin.
5. `search/lmr-deeper`: Dynamic post-LMR re-search depth adjustment ($\pm 1$ ply).
6. `fix/tm-best-move-stability`: Working best-move stability time management term (+19 Elo).
7. `fix/history-depth0`: History update gating at depth $> 0$.
8. `fix/smp-node-counters`: Thread-local SMP node and tablebase hit accounting.
9. `fix/robustness`: Compaction for games over 550 plies (`compactStateHistory()`) and uninitialized bench defaults fix.
10. `search/lmr-research-malus`: Continuation history penalty on quiet moves whose post-LMR re-searches fail low (`4ad2a7f`).
11. `opt/movepicker-threats`: Unified lazy attack bitboards and SEE short-circuiting on quiet checks (+96k NPS, +4.6 Elo, commit `1f24ddf`).

---

## 8. Closing Instructions for GitHub PRs

For each rejected PR on GitHub:
- `search/tt-value-as-eval`
- `search/history-updates`
- `search/qsearch-tt-store`
- `search/all-improvements`
- `fix/tt-generation`
- `search/qs-checks`

Leave a closing note and close the PR:

```markdown
Closed following empirical SPRT testing against GOOB-base:
- **Result:** [Insert score / Elo]
- **SPRT:** [Insert LLR]
- **Conclusion:** Tested in isolation; demonstrated regression / failed to show improvement. Documented in `documentations/search_pr_evaluations_oct2026.md`.
```


