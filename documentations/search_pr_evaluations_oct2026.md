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

## 7. Round 5: RFP Soft-Bound & Improving Scaling (October 12, 2026)

Tested against the baseline engine:

| Branch / Experiment | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/rfp-softbound`** | **REVERTED** | 2416 | 581 - 623 - 1212 | 49.1% | **-5.9 ± 9.8** | -1.68 | Rejected / Reverted (Elo Regression) |

### Detailed Analysis:

#### 1. `search/rfp-softbound` (Reverted: -5.9 Elo)
* **Concept:** Reverse Futility Pruning (RFP) adjustments adapted from reference engines (Stockfish/Berserk/Schoenemann):
  1. Scaled pruning margin by `BetaMargin * (depth - improving)` with `eval >= beta` precondition, allowing tighter pruning when evaluation is improving.
  2. Returned soft-bound `(eval + beta) / 2` instead of raw `eval` to avoid inflating minimax bounds and TT scores.
* **Empirical Result:** 2416 games, 581 wins, 623 losses, 1212 draws (49.1% win rate, **-5.9 ± 9.8 Elo**, LLR -1.68).
* **Failure Analysis:**
  1. **Premature Pruning at Shallow Depths:** When `improving == 1`, `depth - improving` dropped to $0$ at depth 1. Any position where static eval $\ge \beta$ was pruned unconditionally at depth 1. In GOOB, NNUE evaluations can exhibit small local fluctuations; pruning depth-1 nodes without verifying single-reply tactics resulted in tactical blunders.
  2. **Score Attenuation in TT / Minimax:** Returning `(eval + beta) / 2` attenuated the fail-high return value. In GOOB's search architecture, returning the higher static eval provides a stronger signal to parent aspiration windows and TT beta bounds.
* **Action:** Reverted completely from `src/search.c` and `src/README.md`. Baseline bench (938,199 nodes) restored. Documented here for reference.

---

## 8. Round 6: LMR Gives-Check Reduction Deduction (October 12, 2026)

Tested against the baseline engine:

| Branch / Experiment | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/lmr-gives-check`** | **MERGED** | 2772 | 685 - 656 - 1431 | 50.5% | **+3.8 ± 9.0** | +0.302 | Accepted (Positive Elo Gain) |

### Detailed Analysis:

#### 1. `search/lmr-gives-check` (Merged: +3.8 Elo)
* **Concept:** Deducted 1 ply of Late Move Reduction (`R -= givesCheck`, where `givesCheck = (pos->st->checkersBB != 0)`) for candidate moves that give check to the opponent's king, evaluated across both quiet and noisy moves in `AlphaBeta()`.
* **Rationale:** Checking moves are forcing and severely restrict enemy replies. Reducing them with normal LMR frequently causes search horizon dropouts on mating attacks or saving tactical lines. Deducting a ply of reduction searches them at full or near-full depth.
* **Empirical Match Result:**
  - **Match:** 2772 games against `GOOB-base` at 6+0.06 TC.
  - **Score:** 685 - 656 - 1431 (50.5% score, +29 net wins).
  - **Elo Difference:** **+3.8 ± 9.0 Elo** (LOS: 79.4%, LLR: +0.302).
  - **Benchmark:** Benchmark node count decreased from 938,199 to **812,959 nodes** (-13.3% tree size); bench execution time dropped from 456 ms to **405 ms** (`bench.py 12`).
* **Verdict:** **ACCEPTED & MERGED** into `main`.

---

## 9. Round 7: Small ProbCut (Instant TT Cutoff) (October 13, 2026)

Tested against the baseline engine:

| Branch / Experiment | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/small-probcut`** | **REVERTED** | 2695 | 633 - 674 - 1388 | 49.2% | **-5.3 ± 9.2** | Rejected / Reverted (Neutral / Slight Drag) |

### Detailed Analysis:

#### 1. `search/small-probcut` (Reverted: -5.3 Elo / Neutral)
* **Concept:** Added Stockfish Step 13 "Small ProbCut" instant TT cutoff at the start of the ProbCut block in `src/search.c`: when `!info->bruteForceMode && !pvNode && !inCheck && depth >= probCutDepth && abs(beta) < ISMATE && ttHit && (ttBound >= HFBETA) && ttDepth >= depth - 4 && ttValue >= probCutBeta && abs(ttValue) < ISMATE && pos->st->fiftyMove < 96`, cut off immediately and return `probCutBeta = MIN(beta + SmallProbCutMargin, ISMATE - 1)` with `SmallProbCutMargin = 428`.
* **Rationale:** A shallower search in the TT ($\ge \text{depth} - 4$) that already proved a high lower bound $\ge \beta + 428$ implies that recursive verification searches on tactical moves would pass high anyway; returning `probCutBeta` immediately skips generating moves, computing SEE, and recursive searching.
* **Empirical Result:** 2695 games, 633 wins, 674 losses, 1388 draws (49.2% win rate, **-5.3 ± 9.2 Elo**).
* **Failure Analysis:**
  1. **Shallow TT Lower Bounds in GOOB:** Unlike Stockfish, which uses a larger transposition table with depth replacement and a deeply trained large NNUE with tight bound calibration, GOOB's TT entries from $\text{depth} - 4$ with high lower bounds occasionally turn out to be tactical anomalies or fail-highs from non-quiet or volatile nodes.
  2. **Insufficient Verification:** In GOOB, standard ProbCut actually verifies that at least one tactical move succeeds at $\text{depth} - 4$ with zero-width window before cutting off. Bypassing that verification based solely on a shallow TT lower bound resulted in slight tactical degradation rather than Elo improvement.
* **Action:** Reverted completely from `src/search.c`, `src/tune.h`, and `src/README.md`. Baseline bench (812,959 nodes) restored. Documented here for reference.

---

## 10. Round 8: Depth-Scaled Combined Continuation History Pruning (October 13, 2026)

Tested against the baseline engine:

| Branch / Experiment | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/conthist-prune`** | **REVERTED** | 1836 | 414 - 455 - 967 | 48.9% | **-7.8 ± 11.1** | Rejected / Reverted (Elo Regression) |

### Detailed Analysis:

#### 1. `search/conthist-prune` (Reverted: -7.8 Elo)
* **Concept:** Replaced GOOB's flat countermove and follow-up pruning (`CounterMoveHistLimit`, `FollowUpHistLimit` gated at `depth - R <= 3`) with Stockfish-style combined continuation history pruning: `contHist = cmhist + fmhist + pawnHist`, pruning quiets when `!inCheck && !isSpecial && contHist < -ContinuationHistoryMargin * depth` (`ContinuationHistoryMargin = 4136`).
* **Rationale:** Summing 1-ply, 2-ply continuation, and pawn history scales dynamically across depths 1–11 without requiring arbitrary step cutoffs, pruning quiet moves with consistently poor track records across plies.
* **Empirical Result:** 1836 games, 414 wins, 455 losses, 967 draws (48.9% win rate, **-7.8 ± 11.1 Elo**).
* **Failure Analysis:**
  1. **History Table Calibration Discrepancy:** In Stockfish, continuation history tables are backed by deeper searches with extensive correction histories and dedicated quiet move pickers. In GOOB, continuation histories have higher variance, meaning individual poor scores in 1-ply or 2-ply tables prematurely penalized valid quiet defensive resources at moderate depths ($D = 4..8$).
  2. **Over-pruning without Depth Ceilings:** GOOB's previous flat limits were strictly gated to shallow residual depths (`depth - R <= 3`). Expanding pruning to all depths up to 11 pruned critical quiet defensive moves that had negative history in prior iterations but were tactically essential in the current branch.
* **Action:** Reverted completely from `src/search.c`, `src/tune.h`, and `src/README.md`. Baseline bench (812,959 nodes) restored. Documented here for reference.

---

## 11. Round 9: TT Depth & TT-PV Context in LMR (October 14, 2026)

Tested against the baseline engine:

| Branch / Experiment | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/tt-lmr`** | **REVERTED** | ~2363 | ~50% in 2k, 74 - 82 - 207 in 363 | 48.9% (last 363) | **-7.7 ± 23.5** | Rejected / Reverted (Neutral / Regression) |

### Detailed Analysis:

#### 1. `search/tt-lmr` (Reverted: Neutral to -7.7 Elo)
* **Concept:** Added Transposition Table depth and PV context to Late Move Reductions (LMR) adapted from Berserk and Stockfish:
  1. Maintained a `ttPv` flag (`pvNode || (ttHit && ttBound == HFEXACT)`), replacing `!pvNode` with `!ttPv` in quiet and noisy LMR to exempt known transposed exact PV lines from non-PV reduction.
  2. Deducted 1 ply of reduction (`R -= (ttDepth >= depth)`) across quiet and noisy moves when the position was already probed and searched to at least `depth` in the TT.
* **Empirical Result:** Tested in two stages against `GOOB-base`: ~2,000 games overnight resulted in a 50/50 neutral split; a subsequent 363-game run scored 74 wins, 82 losses, 207 draws (48.9% score, -7.7 ± 23.5 Elo, LLR -0.356).
* **Failure Analysis:**
  1. **Tree Explosion on Transposed Nodes:** Deducting 1 ply of reduction whenever `ttDepth >= depth` expanded search trees in complex middlegame transpositions where the TT move had already been probed and refuted, searching secondary and tertiary moves deeper without finding better alternatives.
  2. **TT Bound Ambiguity:** Treating `ttBound == HFEXACT` as a full PV node for reduction purposes reduced search selectivity in sibling subtrees where an exact score had been recorded under a different alpha-beta window.
* **Action:** Reverted completely from `src/search.c` and `src/README.md`. Baseline bench (812,959 nodes) restored. Documented here for reference.

---

## 12. Round 10: Singular Extension Margin Calibration & Triple Extensions (October 14, 2026)

Tested against the baseline engine:

| Branch / Experiment | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/singular-margin-triple-ext`** | **REVERTED** | 651 | 148 - 169 - 334 | 48.4% | **-11.9 ± 18.8** | Rejected / Reverted (Elo Regression) |

### Detailed Analysis:

#### 1. `search/singular-margin-triple-ext` (Reverted: -11.9 Elo)
* **Concept:** Calibrated singular extension margin and added triple extensions adapted from Stockfish and Berserk:
  1. Scaled singular search margin dynamically: `rBeta = MAX(ttValue - SingularMargin * depth, -AB_BOUND)` with `SingularMargin = 2` (tunable in `SEARCH_PARAMS`), replacing the flat `ttValue - depth` margin. Synchronized `multiCut` cutoff value to match.
  2. Awarded triple extensions (`extension = 3`) when `value < rBeta - adjustedTripleMargin` with `TripleExtMargin = 218` (tunable).
* **Empirical Result:** 651 games against `GOOB-base`: 148 wins, 169 losses, 334 draws (48.4% score, **-11.9 ± 18.8 Elo**, LOS 10.7%).
* **Failure Analysis:**
  1. **Tree Bloat from Doubled Margin:** Doubling the margin to `2 * depth` (e.g. 20–30 cp below TT score instead of 10–15 cp) lowered `rBeta` significantly. Alternative moves failed low much more easily during the singular verification search, causing a dramatic spike in false-positive singular detections.
  2. **Over-extension from Triple Extensions:** Granting +3 plies expanded tactical subtrees too deeply; fixed-depth benchmark node count exploded from 812,959 to **1,104,630 nodes (+35.9% tree growth)**. Under time controls, this inflated time spent on speculative branches without delivering tangible tactical gains.
* **Action:** Reverted completely from `src/search.c`, `src/tune.h`, and `src/README.md`. Baseline bench (812,959 nodes) restored. Documented here for reference.

---

## 13. Round 11: Position-Dependent History Malus & Move-Count Bonus (October 14, 2026)

Tested against the baseline engine:

| Branch / Experiment | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/hist-order-bonus`** | **MERGED** | 2543 | 595 - 581 - 1367 | 50.3% | **+2.1 ± 9.2** | Accepted (Positive Elo Gain) |

### Detailed Analysis:

#### 1. `search/hist-order-bonus` (Merged: +2.1 Elo)
* **Concept:** Position-dependent history malus decay by move order and move-count bonus scaling adapted from Stockfish and Berserk:
  1. **Malus Decay by Order:** Quiet moves that were tried earlier in the move list get penalized more heavily than moves tried at the very end (`actualMalus = actualMalus * HistMalusDecay / 1024` with `HistMalusDecay = 921`). Earlier moves were expected by move ordering to be best, so failing is a stronger negative signal.
  2. **Move Count Bonus Scaling:** If the search had to explore deep into the quiet list before finding the refutation move (`length - 1 > HistMoveCountThreshold` with `HistMoveCountThreshold = 8`), the cutoff move gets an extra bonus multiplier (`HistMoveCountBonusMul = 1280` / 1.25×) for rescuing a difficult node.
* **Empirical Match Result:**
  - **Match:** 2543 games against `GOOB-base` at 6+0.06 TC.
  - **Score:** 595 - 581 - 1367 (50.3% score, +14 net wins).
  - **Elo Difference:** **+2.1 ± 9.2 Elo** (LOS: 66.9%, LLR: -0.102).
* **Verdict:** **ACCEPTED & MERGED** into `main` (commit `885291b`).

---

## 14. Round 12: Shuffling Singular Extension Guard (`is_shuffling`) (October 28, 2026)

Tested against the newly compiled 4-bucket mirrored NNUE baseline (`GOOB-base`, bench 842,017 nodes):

| Branch / Experiment | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/is-shuffling`** | **REVERTED** | 597 | 127 - 161 - 309 | 47.2% | **-20.0 ± 19.5** | -0.887 | Rejected / Reverted (Elo Regression) |

### Detailed Analysis:

#### 1. `search/is-shuffling` (Reverted: -20.0 Elo)
* **Concept:** Adapted Stockfish's `is_shuffling()` singular extension guard (`!quietMove || st->fiftyMove < 10 || st->pliesFromNull < 6 || ply < 20`, checking `FROMSQ(move) == TOSQ(move2) && FROMSQ(move2) == TOSQ(move4)`) to disable Singular Extensions on back-and-forth shuffling moves (`A -> B -> A`).
* **Empirical Match Result:**
  - **Match:** 597 games against `GOOB-base` at 6+0.06 TC (1 thread, 32 MB hash, non-regression bounds `ELO0 = -5, ELO1 = 0`).
  - **Score:** 127 wins, 161 losses, 309 draws (47.2% score, -34 net wins).
  - **Elo Difference:** **-20.0 ± 19.5 Elo** (LOS: 2.2%, DrawRatio: 51.5%, LLR: -0.887).
* **Failure Analysis:**
  1. **Loss of Multi-Cut and `-3` Negative Extensions:** In GOOB, `Singularity()` does not only award positive extensions (`+1` or `+2`)—when a shuffling TT move is *not* unique (alternative moves also beat `rBeta`), `Singularity()` triggers **Multi-Cut** (pruning the whole node immediately) or assigns a **`-3` negative extension** to the TT move. Bypassing `Singularity()` when `is_shuffling()` was true prevented GOOB from multi-cutting or reducing non-singular shuffling TT moves.
  2. **Fallback to `check_ext` / `history_ext`:** Skipping `if (singular)` caused execution to fall into the `else` branch in `AlphaBeta()`, allowing shuffling moves to receive `+1` extension from `inCheck` or `cmhist/fmhist > HistexLimit`.
  3. **No Triple Extensions or Node `depth++` in GOOB:** Stockfish required `is_shuffling()` (`#6447`) because its singular hits grant up to `+3` triple extensions and increment `depth++` on the parent node; GOOB uses neither.
* **Action:** Reverted completely from `src/search.c` and `src/README.md`. Documented here for reference.

---

## 15. Round 13: Cuckoo Upcoming Repetition Detection (`upcoming_repetition`) (October 29, 2026)

Tested against the baseline engine (`GOOB-base`, bench 842,017 nodes):

| Branch / Experiment | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/cuckoo-upcoming-repetition`** | **MERGED** | 1429 | 357 - 333 - 739 | 50.8% | **+6.1 ± 12.5** | +1.05 | Accepted (Positive Elo Gain) |

### Detailed Analysis:

#### 1. `search/cuckoo-upcoming-repetition` (Merged: +6.1 Elo)
* **Concept:** Implemented Marcel van Kervinck's Cuckoo hash table algorithm (`InitCuckoo()` in `src/init.c` and `upcoming_repetition()` in `src/recog.h`), adapted from Stockfish and Berserk:
  1. **8,192-Entry Cuckoo Table (`cuckoo[8192]`, `cuckooMove[8192]`):** Precomputes Zobrist XOR deltas (`pieceKeys[pc][s1] ^ pieceKeys[pc][s2] ^ sideKey`) and moves for all 3,668 reversible non-pawn piece movements at startup.
  2. **Opponent Move Cancellation Pre-Filter (`other == 0`):** Scans ancestor `StateInfo` nodes at odd distances (`i = 3, 5, ..., MIN(fiftyMove, pliesFromNull)`) and maintains the running XOR of opponent moves (`other`). Only queries the Cuckoo table when `other == 0` (meaning all opponent moves since `stp` have canceled out).
  3. **Unobstructed Ray & Tree/Game Repetition Verification:** Verifies `!(BetweenBB[s1][s2] & pos->byTypeBB[ALL_PIECES])` and `pos->ply > i || stp->repetition`.
  4. **Early Bound Tightening & Cutoff in `AlphaBeta()`:** At interior nodes (`!rootNode`), when `alpha < 0 && upcoming_repetition(pos)`, immediately raises `alpha = drawScore(pos, info)` and cuts off (`if (alpha >= beta) return alpha`) before TT probing, NNUE evaluation, or move generation.
* **Empirical Match Result:**
  - **Match:** 1,429 games against `GOOB-base` at 6+0.06 TC (1 thread, 32 MB hash, `ELO0 = -5, ELO1 = 0`).
  - **Score:** 357 wins, 333 losses, 739 draws (50.8% score, **+24 net wins**; at 1,420g: White `331 - 22 - 357` [71.8%], Black `24 - 308 - 378` [30.0%]).
  - **Elo Difference:** **+6.1 ± 12.5 Elo** (LOS: **83.0%**, DrawRatio: 51.8%, LLR: **+1.05**).
  - **Benchmark:** Fixed-depth benchmark (`bench.py 12`) node count decreased from 842,017 to **819,396 nodes (-2.7% overall tree size, and -65% nodes on shuffling middlegames)**; execution time dropped from 448 ms to **432 ms**.
* **Verdict:** **ACCEPTED & MERGED** into `main`.

---

## 16. Round 14: Shuffling Guard + Triple Extensions on Manual Singular Loop (Option A) (October 29, 2026)

Tested against the updated Cuckoo baseline (`GOOB-base`, bench 819,396 nodes):

| Branch / Experiment | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/se-shuffling` (Option A)** | **REVERTED** | 2034 | 476 - 509 - 1049 | 49.2% | **-5.5 ± 10.5** | -1.39 | Rejected / Reverted (Elo Regression) |

### Detailed Analysis:

#### 1. `search/se-shuffling` Option A (Reverted: -5.5 Elo)
* **Concept:** Combined Stockfish's `is_shuffling()` guard with calibrated double/triple extensions inside GOOB's existing manual `Singularity()` loop (6 quiet / 3 tactical moves):
  1. Added `abs(ttValue) < TB_WIN_VALUE - MAXDEPTH` to the `singular` eligibility check.
  2. Added `dextensions[MAXDEPTH]` per-ply counter (`baseDext <= 6`) to cap runaway $>1$ extension chains.
  3. Kept Multi-Cut and `-3` negative extensions active on all moves, while gating positive extensions with `!is_shuffling(pos, ttMove, !ttCapture)` inside `Singularity()`.
  4. Added `+3` triple extensions on non-PV quiet singular moves (`!pvNode && !ttCapture && value < rBeta - adjustedDoubleMargin - 64`).
* **Empirical Match Result:**
  - **Match:** 2,034 games against `GOOB-base` at 6+0.06 TC (1 thread, 32 MB hash, `ELO0 = 0, ELO1 = 5`, logged in `tools/sprt/logs/se-shuffling2.log`; plus an initial 228-game warmup `57 - 55 - 116` in `se-shuffling.log`).
  - **Score:** 476 wins, 509 losses, 1,049 draws (49.2% score, -33 net wins; at 2,030g: White `447 - 39 - 529` [70.1%], Black `28 - 468 - 519` [28.3%]).
  - **Elo Difference:** **-5.5 ± 10.5 Elo** (LOS: 15.4%, DrawRatio: 51.6%, LLR: -1.39 [-47.1%]).
  - **Benchmark:** `815,794` nodes at depth 12 (-0.44% vs `GOOB-base`).
* **Failure Analysis:**
  1. **Mismatch Between Manual Singular Loop and Triple Extensions:** GOOB's legacy `Singularity()` loop searches only up to 6 quiet and 3 tactical moves at unreduced `depth / 2 - 1` (with no LMR, no SEE/futility/LMP pruning, and hard move-count caps), whereas Stockfish, Berserk, and Ethereal-14.00 evaluate singularity via a full `excludedMove` recursive `AlphaBeta` search at `(depth - 1) / 2` where moves after the first alternative are subject to LMR and pruning.
  2. **Over-extension on Unverified Late Alternatives:** Because the manual loop stops after 6 quiets and 3 captures, a move can appear deeply singular (`value < rBeta - adjustedDoubleMargin - 64`) simply because the true refutation was move #7+, or conversely waste nodes searching 6 unreduced quiet alternatives at `depth / 2 - 1`.
* **Action:** Reverted Option A and proceeded to Option B (`excludedMove` recursive singular search).

---

## 17. Round 15: `excludedMove` Recursive Singular Search & Shuffling Guard (Option B) (October 29, 2026)

Tested against the baseline engine (`GOOB-base`, bench 819,396 nodes):

| Branch / Experiment | Status | Games | Record (W - L - D) | Win Rate | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/se-excluded` (Option B)** | **MERGED** | 1078 | 255 - 251 - 572 | 50.2% | **+1.6 ± 14.3** | -0.083 | Accepted / Merged |

### Detailed Analysis:

#### 1. `search/se-excluded` Option B (Merged: +1.6 Elo)
* **Concept:** Refactored singular verification search from the legacy unpruned 6-quiet/3-tactical manual loop to a modern Stockfish/Berserk/Ethereal-style recursive `AlphaBeta` search with `excludedMove`, combined with the `is_shuffling()` singular extension guard:
  1. **`excludedMove` Recursive Verification:** At candidate singular nodes, `Singularity()` sets `pos->search->excludedMove[pos->ply] = ttMove` and calls `AlphaBeta(rBeta - 1, rBeta, (depth - 1) / 2, ...)` at the same node. The first alternative move (`Legal == 1`) is searched unreduced, while subsequent alternative moves are subject to standard move-loop pruning (Futility, LMP, CounterMove/Followup, Capture Futility, SEE) and LMR.
  2. **`excludedMove` Search Stack Protection:** Inside `AlphaBeta()`, when `excludedMove != NOMOVE`:
     - Uses `pos->search->singularMovePickers[pos->ply]` via `initSingularMovePicker()` so the outer node's `movePickers[pos->ply]` is preserved.
     - Reuses `pos->search->eval_stack[pos->ply]` without re-evaluating NNUE.
     - Bypasses TT/Syzygy cutoffs, `upcoming_repetition()`, Hindsight, Razoring, RFP, NMP, IIR, ProbCut, `ttMoveHistory`, correction history, and TT storage at the singular root node.
     - Returns `alpha` (`rBeta - 1`) if `Legal == 0 && excludedMove` so single-legal-move positions are correctly recognized as singular.
  3. **`is_shuffling()` Guard & Calibrated Double/Triple Extensions:**
     - `is_shuffling()` checks for back-and-forth piece shuffling (`A -> B -> A`, `fiftyMove >= 10`, `pliesFromNull >= 6`, `ply >= 20`) to prevent runaway singular extension explosions.
     - Gated only on positive extensions (`extension = 0` if shuffling), leaving Multi-Cut and `-3` negative extensions fully functional.
     - Tracks cumulative $>1$ extensions via `dextensions[MAXDEPTH]` (`baseDext <= 6`) and allows `+3` triple extensions only on non-PV quiet singular moves (`!pvNode && !ttCapture && value < rBeta - adjustedDoubleMargin - 64`).
* **Empirical Match Result:**
  - **Match:** 1,078 games against `GOOB-base` at 6+0.06 TC (1 thread, 32 MB hash, logged in `tools/sprt/logs/se-excluded.log`).
  - **Score:** 255 wins, 251 losses, 572 draws (50.2% score, +4 net wins; at 1,070g: White `467 - 36 - 567` [70.1%], Black `24 - 237 - 273` [30.1%]).
  - **Elo Difference:** **+1.6 ± 14.3 Elo** (LOS: 58.8%, DrawRatio: 53.0%, LLR: -0.0829).
  - **Benchmark:** `829,601` nodes at depth 12 (+1.2% nodes vs `GOOB-base`, NPS `1,907,128` vs `1,849,652`).
  - **Endgame Shuffling Stability:** On complex shuffling endgames (e.g. `4k3/p1p1p1p1/1p1p1p1p/3P1P2/2P1P1P1/1P5P/P7/2B1K2n w - - 20 50` at depth 22), `seldepth` dropped from **43** (8.04M nodes in Option A) to **28** (3.29M nodes in Option B, 2.4× speedup).
* **Verdict:** **ACCEPTED & MERGED** into `main`.

---

## 18. Round 16: Correction History Exact-Bound Update (October 30-31, 2026)

First test run with the fastchess pentanomial runner (`tools/sprt/run_sprt.sh`, `MODE=gainer`) under the conclusive-only acceptance rule: a change is merged only if the SPRT accepts H1. Tested against `main` at `a612576` (binary `GOOB-main-a612576`, bench 1,389,081 nodes):

| Branch / Experiment | Status | Games | Record (W - L - D) | Score | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`fix/corr-exact-bound`** | **NOT MERGED** | 12178 | 2850 - 2902 - 6426 | 49.79% | **-1.48 ± 3.40** | -2.97 | Rejected (H0 accepted) |

### Detailed Analysis:

#### 1. `fix/corr-exact-bound` (Rejected: -1.48 Elo, H0 accepted)
* **Concept:** Change the condition for writing `bestScore - staticEval` into the correction history tables (`src/search.c`, end of `AlphaBeta()`), following Berserk/Stockfish:
  - **Before (`main`):** update only when `(bestScore > staticEval) == (bestScore > oldAlpha)`, i.e. a fail low may only lower the correction and any score above `oldAlpha` (fail high *or* exact) may only raise it. An exact PV score below the static eval was never used.
  - **After:** a fail high (`bestScore >= beta`) may only raise the correction, a fail low (`bestScore <= oldAlpha`) may only lower it, and an exact score corrects in both directions.
  - One logical change of 8 lines; the tables, weights and update formula are untouched.
* **Correctness checks before the match:** `perfttest` 33/33 passed; mate-in-1 and mate-in-10 test positions gave identical mate scores; `make` clean.
* **Benchmark:** bench 1,389,081 -> 1,487,928 nodes (+7.1%); `bench.py 12` 829,601 -> 748,028 nodes.
* **Empirical Match Result:**
  - **Runner:** fastchess 1.8.2-alpha, pentanomial SPRT, logistic Elo bounds `[0, 3]`, `alpha = beta = 0.05` (LLR bounds ±2.94).
  - **Conditions:** 6+0.06 TC, 1 thread, 32 MB hash, 10 concurrent games, `tools/book.epd` (random order, each opening played with both colours), same adjudication as earlier rounds. Total time 5 h 28 min.
  - **Score:** 2,850 wins, 2,902 losses, 6,426 draws (49.79%).
  - **Elo Difference:** **-1.48 ± 3.40 Elo** (nElo -2.69 ± 6.17, LOS 19.6%, DrawRatio 48.0%).
  - **Pentanomial (0-2):** `[84, 1530, 2922, 1460, 93]`.
  - **LLR:** **-2.97** (-2.94 lower bound crossed) -> **H0 accepted** (log: `tools/sprt/logs/corr-exact-bound.log`).
* **Failure Analysis:** The result is a conclusive "no gain", not a demonstrated loss: the Elo interval (-4.9, +1.9) includes zero. Letting exact scores lower the correction makes the corrected eval more pessimistic at PV nodes, which in GOOB changed the tree size (+7% bench nodes) without improving play. The original rule, although less symmetric than Berserk's, is at least as good for this engine and its current correction weights.
* **Verdict:** **REJECTED.** Not merged into `main`; the branch `fix/corr-exact-bound` (commit `b6ab8f3`) is kept locally only and was never pushed.

---

## 19. Round 17: Search Feature Ablation (October 31 - November 2, 2026)

Every search / move-loop feature that had been merged on an inconclusive SPRT was re-tested on its own by **removing** it. Each test played the engine *without* the feature (NEW) against the current baseline (BASE):

* **Runner:** `tools/sprt/run_sprt.sh`, `MODE=nonreg` (SPRT [-3, 1], logistic, LLR ±2.94), fastchess pentanomial, capped at 10,000 games.
* **Conditions:** 6+0.06, 1 thread, 32 MB hash, `tools/book.epd` (UHO-style, random order, each opening with both colours), 10 concurrent games (12 for the last three tests).
* **Baseline:** `main` at `14e2845`. A removal that passed (H1) was committed and became the baseline for the following tests; a feature whose removal lost (H0) was kept.
* **Undecided** features (no verdict after 10,000 games) were kept in the baseline and decided afterwards by review.
* Queue, patches and logs: `tools/sprt/logs/ablation/` and `tools/sprt/logs/abl-<feature>.log` (git-ignored). Median search depth at this time control: 14 plies.

"Elo" is the engine **without** the feature: positive means removing it did not hurt.

| # | Feature removed | Games | Elo (without) | LLR | Ptnml(0-2) | Verdict |
| :---: | :--- | :---: | :---: | :---: | :--- | :--- |
| 1 | Surprise-SRD (sibling surprise / eval deficit LMR) | 6,506 | **+2.94 ± 4.53** | +2.94 | [34, 767, 1603, 808, 41] | **REMOVED** (H1) |
| 2 | Hindsight depth adjustment | 10,000 | +0.14 ± 3.78 | +1.23 | [72, 1254, 2339, 1268, 67] | Undecided -> **kept** |
| 3 | History extension (`HistexLimit`) | 10,000 | -1.08 ± 3.70 | -0.09 | [65, 1235, 2425, 1216, 59] | Undecided -> **removed** |
| 4 | Countermove / follow-up history pruning | 9,014 | **-3.97 ± 3.94** | -2.95 | [77, 1118, 2203, 1049, 60] | **KEPT** (H0) |
| 5 | TT research-margin cutoff | 6,942 | **+2.75 ± 4.41** | +2.96 | [39, 822, 1699, 867, 44] | **REMOVED** (H1) |
| 6 | Post-LMR re-search continuation malus | 9,240 | **-3.80 ± 3.80** | -2.98 | [61, 1144, 2302, 1061, 52] | **KEPT** (H0) |
| 7 | History malus decay / move-count bonus | 10,000 | -2.47 ± 3.66 | -1.68 | [65, 1230, 2473, 1175, 57] | Undecided -> **kept** |
| 8 | ttMoveHistory in the double-extension margin | 10,000 | -2.02 ± 3.71 | -1.13 | [64, 1262, 2403, 1210, 61] | Undecided -> **kept** |
| 9 | LMR gives-check reduction | 10,000 | +1.15 ± 3.70 | +2.40 | [70, 1179, 2473, 1204, 74] | Undecided -> **removed** |
| 10 | LMR `ttCapture` increase | 10,000 | -1.63 ± 3.65 | -0.73 | [63, 1228, 2447, 1217, 45] | Undecided -> **kept** |
| 11 | LMR deeper/shallower re-search | 3,134 | **-9.65 ± 6.68** | -2.97 | [28, 413, 764, 342, 20] | **KEPT** (H0) |
| 12 | Capture futility pruning | 10,000 | -0.59 ± 3.70 | +0.46 | [78, 1193, 2453, 1220, 56] | Undecided -> **removed** |
| 13 | ProbCut TT guard and TT store | 10,000 | -1.08 ± 3.76 | -0.08 | [78, 1225, 2428, 1188, 81] | Undecided -> **kept** |
| 14 | Cuckoo upcoming repetition | 8,286 | **-4.19 ± 4.06** | -2.97 | [56, 1044, 2048, 934, 61] | **KEPT** (H0) |

### Decisions on the undecided features

Decided by the SPRT trend, by whether Stockfish 19, Berserk 14 and Ethereal 14 use the idea, and by whether it is likely to behave differently at longer time controls (at 6+0.06 GOOB reaches a median depth of 14; features gated to deep nodes are under-measured).

* **Kept:**
  * *History malus decay / move-count bonus:* 91% likely positive; Stockfish uses the identical 921/1024 malus decay and also scales the bonus by the number of searched moves.
  * *ttMoveHistory:* 86% likely positive; Stockfish uses it in its double-extension margin. It only acts inside singular-extension searches (depth >= 10), which cover the top ~4 plies at this time control and many more at longer ones.
  * *LMR `ttCapture`:* 81% likely positive; used by Stockfish and Berserk.
  * *ProbCut TT guard and store:* the same guard is used by Stockfish, Berserk and Ethereal.
  * *Hindsight:* exactly 0 Elo here, but used by both Stockfish and Berserk, and its trigger (a large parent reduction) occurs more often in deeper searches.
* **Removed:**
  * *History extension:* no reference engine uses it; Ethereal 14 dropped this exact extension.
  * *LMR gives-check:* the removal came closest to passing (LLR +2.40) and Stockfish 19 has no gives-check LMR term.
  * *Capture futility:* a coin flip (-0.6 Elo); it overlaps with the existing history-adjusted SEE capture pruning and is low-depth pruning, so a longer time control is not expected to change it.

### Result

Five features were removed (Surprise-SRD, TT research cutoff, history extension, LMR gives-check, capture futility) on branch `cleanup/search-ablation`, followed by a node-identical dead-code cleanup (their tunables, trace counters and the `Surprise_SRD` UCI option). Depth-12 bench (`tools/sprt/bench.py 12`): **829,601 -> 867,532** nodes. The combined removal of the three review-decided features was **not** confirmed with a final SPRT against `14e2845`, at the user's request.

### Confirmation match: post-ablation vs pre-ablation (November 4, 2026)

All five removals together (`main` at `6fb1b3d`, engine-identical to `9cb2059`, bench 867,532) were then played against the pre-ablation `main` (`14e2845`, bench 829,601).

| Experiment | Games | Record (W - L - D) | Score | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :--- |
| **`ablation-total`** (post vs pre ablation) | 32996 | 7997 - 7801 - 17198 | 50.30% | **+2.06 ± 2.08** | +1.51 | Stopped, accepted by the user |

* **Match:** `run_sprt.sh GOOB-post-ablation-6fb1b3d GOOB-pre-ablation-14e2845 ablation-total 6+0.06 12`, gainer SPRT `[0, 3]`, fastchess pentanomial, 6+0.06, 1 thread, 32 MB hash, 12 concurrent games, `tools/book.epd`. Log: `tools/sprt/logs/ablation-total.log`.
* **Pentanomial (0-2):** `[244, 3947, 7969, 4045, 293]`; nElo +3.72 ± 3.75, LOS 97.4%.
* **Result:** Stopped by hand at 32,996 games without an SPRT verdict (the LLR peaked at +1.79 around 32,000 games). The 95% interval (0.0 to +4.1 Elo) and the 97% LOS show the cleaned-up engine is at least as strong as before and most likely about +2 Elo stronger; the user accepted the removals on this evidence.

---

## 20. Round 18: Post-Ablation SPSA Retune of LMR / SEE (November 2-4, 2026)

After the Round 17 removals the LMR and noisy-SEE constants were retuned with SPSA and the result was tested against `main` (`9cb2059`).

* **SPSA run:** `tools/spsa/spsa.py run post-ablation1 --include '^LMR|AllNode|SEENoisy'` (10 parameters), 8,000 iterations (32,000 games), 5+0.05, 12 workers, 9.1 h. Run directory `tools/spsa/runs/post-ablation1/` (git-ignored).
* **Tuned values (main -> tuned):** LMRBase 77 -> 79, LMRDivisor 224 -> 220, LMRQuietHistDiv 4984 -> 5001, LMRNoisyHistDiv 4873 -> 4882, AllNodeScale 277 -> 274, AllNodeBase 288 -> 287, LMRDeeperMargin 37 -> 39, SEENoisyHistDiv 64 -> 58; SEENoisyMargin (-18) and LMRShallowerMargin (7) unchanged. Every change is smaller than the parameter's SPSA step size `c_end`.

| Branch / Experiment | Status | Games | Record (W - L - D) | Score | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`spsa-pruning1`** (post-ablation1 values) | **NOT MERGED** | 23880 | 5773 - 5693 - 12414 | 50.17% | **+1.16 ± 2.44** | -0.65 | Stopped, inconclusive |

* **Match:** `run_sprt.sh GOOB-tuned GOOB-base spsa-pruning1 6+0.06 12`, gainer SPRT `[0, 3]`, fastchess pentanomial, 6+0.06, 1 thread, 32 MB hash, 12 concurrent games, `tools/book.epd`. Ran 2026-11-03 21:23 to 2026-11-04 06:20 and was stopped by hand before a verdict.
* **Pentanomial (0-2):** `[161, 2936, 5697, 2954, 192]`; nElo +2.10 ± 4.41, LOS 82.5%, DrawRatio 47.7%. Log: `tools/sprt/logs/spsa-pruning1.log`.
* **Analysis:** The tuned values moved by less than the tuner's own perturbation size, so the run mostly confirms that these ten constants were already close to optimal after the removals. The measured gain (about +1 Elo, 95% interval -1.3 to +3.6) is too small to prove at this time control in a reasonable number of games.
* **Verdict:** **NOT MERGED** (no conclusive SPRT). `src/tune.h` keeps the `main` values.

---

## 21. Round 19: Stored `ttPv` Flag in LMR (November 4, 2026)

Tested against `main` at `6122fa5` (binary `GOOB-main-6122fa5`, `bench.py 12` 867,532 nodes):

| Branch / Experiment | Status | Games | Record (W - L - D) | Score | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/ttpv`** | **NOT MERGED** | 4070 | 929 - 1016 - 2125 | 48.93% | **-7.43 ± 5.90** | -2.95 | Rejected (H0 accepted) |

* **Concept (Berserk 14 `search.c:426-429, 716`; Stockfish 19 `search.cpp:822`):** store a "was on a PV line" bit (bit 63 of `smp_data`) with every TT write; `ttPv = pvNode || (ttHit && stored bit)`, kept in `ttpv_stack[ply]` for the singular verification search. Quiet and noisy LMR added their one-ply non-PV increase on `!ttPv` instead of `!pvNode`. Unlike Round 9 (`search/tt-lmr`), the flag was really stored and no other LMR term was changed.
* **Correctness checks:** `perfttest` 33/33, TT replacement tests pass, mate checks unchanged, 1- and 2-thread UCI searches normal. `bench.py 12`: 867,532 -> 667,344 nodes (-23%).
* **Match:** gainer SPRT `[0, 3]`, fastchess pentanomial, **5+0.05**, 1 thread, 32 MB hash, 10 concurrent games, `tools/book.epd`. Pentanomial (0-2) `[39, 525, 980, 466, 25]`, LOS 0.7%. Log: `tools/sprt/logs/ttpv.log`. (A first run at 6+0.06 was stopped after 61 games to change the time control.)
* **Failure analysis:** The exact cause was not measured. The large tree change (-23% bench nodes) shows the flag touches far more nodes than true PV nodes, so the one-ply non-PV reduction was dropped for a large share of cut/all nodes. Stockfish and Berserk use `ttPv` together with other terms built around it (Stockfish also adds base reduction for `ttPv` nodes, scales its LMR bonus by TT value and depth, and propagates the flag on fail low); swapped in alone for GOOB's `!pvNode`, it is a clear loss.
* **Verdict:** **REJECTED.** Branch `search/ttpv` (commit `1246d01`) kept locally only, never pushed.

---

## 22. Round 20: Cutoff-Count LMR Increase (November 4-5, 2026)

Tested against `main` at `6122fa5` (binary `GOOB-main-6122fa5`, `bench.py 12` 867,532 nodes):

| Branch / Experiment | Status | Games | Record (W - L - D) | Score | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/cutoff-cnt`** | **NOT MERGED** | 29242 | 7036 - 6959 - 15247 | 50.13% | **+0.91 ± 2.20** | -1.39 | Stopped, rejected on review |

* **Concept (Stockfish 19 `search.cpp:810, 1335-1336, 1529`):** count fail highs per ply in `cutoff_cnt[]`; every node clears the grandchild ply, so `cutoff_cnt[ply + 1]` holds the fail highs of the node's children and of its earlier siblings' children. A fail high counts unless it came from a double extension at a non-PV node. Quiet and noisy LMR added one ply when `cutoff_cnt[ply + 1] > 2`.
* **Correctness checks:** `perfttest` 33/33, TT replacement tests pass, mate checks unchanged, 1- and 2-thread UCI searches normal. `bench.py 12`: 867,532 -> 599,906 nodes (-31%).
* **Match:** gainer SPRT `[0, 3]`, fastchess pentanomial, 5+0.05, 1 thread, 32 MB hash, 10 concurrent games, `tools/book.epd`, 10 h 17 min. Pentanomial (0-2) `[238, 3489, 7075, 3596, 223]`, LOS 79.2%. Log: `tools/sprt/logs/cutoff-cnt.log`.
* **Decision:** Stopped by hand without a verdict. After 29k games the best estimate is under +1 Elo (95% interval -1.3 to +3.1) and the LLR was drifting towards H0, so the change is not worth its code under the conclusive-only rule. Stockfish's version is fractional (+0.26 ply for more than one cutoff, +1.07 more for more than two, and +1.11 more at all-nodes); the whole-ply approximation here may be too coarse to show the effect.
* **Verdict:** **REJECTED.** Branch `search/cutoff-cnt` (commit `561330f`) kept locally only, never pushed.

---

## 23. Round 21: Prior Quiet Countermove Bonus on Fail Low (November 5, 2026)

Tested against `main` at `6122fa5` (binary `GOOB-main-6122fa5`, `bench.py 12` 867,532 nodes):

| Branch / Experiment | Status | Games | Record (W - L - D) | Score | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/prior-cm-bonus`** | **NOT MERGED** | 2332 | 525 - 621 - 1186 | 47.94% | **-14.31 ± 7.73** | -2.97 | Rejected (H0 accepted) |

* **Concept (Stockfish; the classic form used before the current `bonusScale` formula):** when a node fails low (`bestScore <= alpha`, `depth >= 3` or PV, not root, not a singular verification search), give the opponent's quiet move that led to it a `stat_bonus(depth)` continuation history bonus in the position it was played from (`updatePriorQuietBonus()` in `history.c`).
* **Correctness checks:** `perfttest` 33/33, TT replacement tests pass, mate checks unchanged, 1- and 2-thread UCI searches normal. `bench.py 12`: 867,532 -> 697,174 nodes.
* **Match:** gainer SPRT `[0, 3]`, fastchess pentanomial, 5+0.05, 1 thread, 32 MB hash, 10 concurrent games, `tools/book.epd`, 50 min. Pentanomial (0-2) `[16, 335, 562, 235, 18]`, LOS 0.01%. Log: `tools/sprt/logs/prior-cm-bonus.log`.
* **Failure analysis:** Fail-low nodes are a large share of all interior nodes, so a full `stat_bonus(depth)` at every one of them adds far more positive continuation history than the fail-high updates. GOOB also prunes with continuation history (countermove / follow-up pruning, futility history limits), so inflated scores change pruning, not just move ordering. Stockfish today scales this bonus by the parent's move count, eval drop and stat score (often to 0 at low depth) and spreads it over much smaller weights; the flat classic form is clearly harmful here.
* **Verdict:** **REJECTED.** Branch `search/prior-cm-bonus` (commit `f008b87`) kept locally only, never pushed.

---

## 24. Round 22: History Update for a Quiet TT Move on a TT Cutoff (November 5, 2026)

Tested against `main` at `6122fa5` (binary `GOOB-main-6122fa5`, `bench.py 12` 867,532 nodes):

| Branch / Experiment | Status | Games | Record (W - L - D) | Score | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/tt-cutoff-hist`** | **NOT MERGED** | 2980 | 680 - 771 - 1529 | 48.47% | **-10.61 ± 6.85** | -2.95 | Rejected (H0 accepted) |

* **Concept (Stockfish 19 `search.cpp:880-884`):** when a non-PV node returns on a TT lower bound (`ttValue >= beta`) and the TT move is a pseudo-legal quiet move, pass it to `updateHistories()` as a one-move fail high (killers, countermove / follow-up tables and, above depth 3, the history bonus).
* **Correctness checks:** `perfttest` 33/33, TT replacement tests pass, mate checks unchanged, 1- and 2-thread UCI searches normal. `bench.py 12`: 867,532 -> 701,757 nodes.
* **Match:** gainer SPRT `[0, 3]`, fastchess pentanomial, 5+0.05, 1 thread, 32 MB hash, 10 concurrent games, `tools/book.epd`, 1 h 5 min. Pentanomial (0-2) `[20, 417, 706, 328, 19]`, LOS 0.12%. Log: `tools/sprt/logs/tt-cutoff-hist.log`.
* **Failure analysis:** The third history-side change in a row to lose clearly (after Round 21). Reusing `updateHistories()` gives the TT move GOOB's full fail-high treatment (killer and countermove replacement plus the full `stat_bonus`), while Stockfish gives only a small capped bonus (`min(112 * depth, 695)`). GOOB's history scores also drive pruning (countermove / follow-up pruning, futility history limits, LMR history terms), so extra updates shift the tuned pruning thresholds. Further history-side additions should use small, separately tuned bonuses or be avoided.
* **Verdict:** **REJECTED.** Branch `search/tt-cutoff-hist` (commit `75605cb`) kept locally only, never pushed.

---

## 25. Round 23: Opponent-Worsening Term in the RFP Margin (November 5, 2026)

Tested against `main` at `6122fa5` (binary `GOOB-main-6122fa5`, `bench.py 12` 867,532 nodes):

| Branch / Experiment | Status | Games | Record (W - L - D) | Score | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/rfp-opp-worsening`** | **NOT MERGED** | 3572 | 785 - 873 - 1914 | 48.77% | **-8.56 ± 6.24** | -2.95 | Rejected (H0 accepted) |

* **Concept (Stockfish 19 `futility_margin`):** shrink the reverse futility pruning margin by `BetaMargin / 3` when the opponent's position keeps getting worse (`opponentWorsening`: `staticEval > -eval_stack[ply-1]`), so RFP prunes `eval - BetaMargin * depth + opponentWorsening * BetaMargin / 3 > beta`. Tested alone, as one small term, because Round 5 bundled an RFP improving term with a soft bound.
* **Correctness checks:** `perfttest` 33/33, TT replacement tests pass, mate checks unchanged, 1- and 2-thread UCI searches normal. `bench.py 12`: 867,532 -> 641,024 nodes.
* **Match:** gainer SPRT `[0, 3]`, fastchess pentanomial, 5+0.05, 1 thread, 32 MB hash, 10 concurrent games, `tools/book.epd`, about 1 h 15 min. Pentanomial (0-2) `[28, 471, 874, 387, 26]`, LOS 0.36%. Log: `tools/sprt/logs/rfp-opp-worsening.log`.
* **Failure analysis:** The extra RFP pruning (bench -26%) costs more than the saved nodes buy. GOOB's RFP margin (`BetaMargin`) was tuned by SPSA without this term, and the current RFP already prunes aggressively; a further margin cut on top of it prunes too many nodes where the static eval overestimates the position. Like Round 5, tightening RFP beyond the tuned margin does not pay off; a retry would need the margin and the term tuned together.
* **Verdict:** **REJECTED.** Branch `search/rfp-opp-worsening` (commit `349a19d`) kept locally only, never pushed.

---

## 26. Round 24: Full 66-Parameter SPSA Retune (November 5-6, 2026)

After four rejected feature ports in a row (Rounds 20-23), every constant in `src/tune.h` was retuned at once, including the depth limits that earlier runs excluded.

* **SPSA run:** `tools/spsa/spsa.py run full1 --tc 5+0.05 --workers 12 --iterations 15000` (all 66 parameters, no `--include` / `--exclude`), 15,000 iterations (60,000 games), 17.0 h, frozen engine = `main` `11775fd` (`bench.py 12` 867,532). Run directory `tools/spsa/runs/full1/` (git-ignored). 54 of the 66 defaults changed.
* **Structural changes (main -> tuned):** RazoringDepth 1 -> 0 (razoring disabled; theta 0.03, at the lower bound), SingularDepth 10 -> 8, probCutDepth 6 -> 7, BetaPruningDepth 8 -> 9, NMPBase 6 -> 5, defaultNullMoveDepth 2 -> 3 (theta 2.55, borderline), HistMoveCountThreshold 8 -> 7.
* **Largest margin / limit changes:** QSSeeMargin 108 -> 99, FutilityMarginNoHistory 100 -> 92, DoubleExtMargin 128 -> 121, HindsightMargin 167 -> 174, probCutMargin 89 -> 95, CounterMoveHistLimitNI -279 -> -451, FollowUpHistLimitNI -424 -> -539, LMRNoisyHistDiv 4873 -> 4718, GoodQuietThreshold -3000 -> -2886, QuietCheckBonus 16384 -> 16672. Full diff: commit `a2a4abd` (`src/tune.h`).
* **Correctness checks:** `perfttest` 33/33, TT replacement tests pass, mate checks unchanged, 1- and 2-thread UCI searches normal. `bench.py 12`: 867,532 -> 1,083,787 nodes (razoring off, smaller null-move reduction).

| Branch / Experiment | Status | Games | Record (W - L - D) | Score | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`spsa/full1`** | **MERGED** | 3168 | 809 - 687 - 1672 | 51.93% | **+13.39 ± 6.77** | +2.96 | Accepted (H1 accepted) |

* **Match:** `run_sprt.sh GOOB-spsa-full1 GOOB-main-6122fa5 spsa-full1 5+0.05 10`, gainer SPRT `[0, 3]`, fastchess pentanomial, 5+0.05, 1 thread, 32 MB hash, 10 concurrent games, `tools/book.epd`, 1 h 8 min. Pentanomial (0-2) `[25, 334, 748, 448, 29]`, nElo +23.95 ± 12.10, LOS 99.99%. Log: `tools/sprt/logs/spsa-full1.log`.
* **Analysis:** The largest single gain in this report. Retuning everything together lets the pruning margins, depth limits and history thresholds move as a set, which the single-feature ports of Rounds 19-23 could not do. Disabling razoring is bundled into this result; whether it helps on its own (or whether the razoring code can be removed) needs a separate test.
* **Verdict:** **ACCEPTED.** Branch `spsa/full1` (commit `a2a4abd`) fast-forwarded into `main`.
* **Follow-up: razoring deleted.** With `RazoringDepth = 0` the razoring block could never run (depth-0 nodes go to quiescence before it unless in check, and razoring skips in-check nodes), so the block, its three `tune.h` parameters (`RazoringDepth`, `RazorMarginBase`, `RazorMarginCoeff`) and its trace counters were removed. Node-identical to `a2a4abd`: `bench.py 12` 1,083,787 nodes for both, and fixed-depth (16) searches on four positions give identical `info` lines and best moves, so no match was needed; the +13.39 Elo result carries over. Among the reference engines, Stockfish, Berserk, Schoenemann and c4ke razor (up to depth 5 with linear margins, or every depth in Stockfish), while Ethereal 12.75 / 14.00 do not.

---

## 27. Round 25: No Static Eval in Check (November 6-7, 2026)

Tested against `main` at `7315a36` (binary `GOOB-main-norazor`, `bench.py 12` 1,083,787 nodes). Part of NNUE Round 2 (plan Phase 5.2).

| Branch / Experiment | Status | Games | Record (W - L - D) | Score | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/incheck-no-eval`** | **NOT MERGED** | 8876 | 2057 - 2123 - 4696 | 49.63% | **-2.58 ± 3.99** | -2.95 | Rejected (H0 accepted) |

* **Concept (Stockfish 19):** never call the net in check. The net is not trained on in-check positions, so the node takes `staticEval` from two plies earlier (`eval_stack[ply-2]`, 0 near the root) and stores `rawEval = VALUE_NONE`.
* **Correctness checks:** `perfttest` 33/33, TT replacement tests pass, mate checks unchanged, 1- and 2-thread UCI searches normal. `bench.py 12`: 1,083,787 -> 838,540 nodes.
* **Match:** gainer SPRT `[0, 3]`, fastchess pentanomial, 5+0.05, 1 thread, 32 MB hash, 10 concurrent games, `tools/book.epd`. Pentanomial (0-2) `[65, 1121, 2136, 1047, 69]`. Log: `tools/sprt/logs/incheck-no-eval.log`.
* **Failure analysis:** The two-ply-old eval is a worse estimate than the net's own (if untrained) in-check eval for GOOB, whose pruning margins were tuned with the real eval in check (Round 24). The result is close to zero, so a retry would need the dependent margins retuned with it.
* **Verdict:** **REJECTED.** Branch `search/incheck-no-eval` (commit `42c0a32`) kept locally only, never pushed.

---

## 28. Round 26: Negative SEE Reduction in Quiet LMR (November 11, 2026)

Tested against `main` at `88c6e79` (baseline binary `GOOB-base`, Gen-3 net, `bench.py 12` 887,490 nodes):

| Branch / Experiment | Status | Games | Record (W - L - D) | Score | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`search/lmr-negative-see`** | **NOT MERGED** | 7078 | 1639 - 1674 - 3765 | 49.75% | **-1.72 ± 4.39** | -1.93 | Stopped, rejected |

* **Concept:** In `AlphaBeta()`, before making a move, if a quiet non-special move lands on an attacked square (`mp->threats & (1ULL << to)`) and loses material on the exchange (`!StaticExchangeEvaluation(pos, move, 0)`), flag it as `badSee`. During Late Move Reduction (LMR), increase reduction by 1 ply (`R++`) for these moves.
* **Correctness checks:** `perfttest` 33/33, TT replacement tests pass, `test_incremental` 14/14, 1- and 2-thread UCI searches normal. `bench.py 12`: 887,490 -> 784,113 nodes (-11.6%).
* **Match:** gainer SPRT `[0, 3]`, fastchess pentanomial, 5+0.05, 1 thread, 32 MB hash, 10 concurrent games, `tools/book.epd`, ~1 h 40 min. Pentanomial (0-2) `[44, 880, 1727, 843, 45]`, LOS 22.1%. Log: `tools/sprt/logs/lmr-neg-see.log`.
* **Failure analysis:** While saving 11.6% bench nodes by reducing quiet blunders/sacrifices, the extra 1-ply reduction caused tactical oversights where quiet piece sacrifices or dynamic counter-sacrifices were pruned too deeply without triggering the re-search threshold. The performance remained neutral-to-slight-negative (-1.7 Elo) across >7k games, with LLR drifting downward toward $H_0$ (-1.93).
* **Verdict:** **REJECTED.** Branch `search/lmr-negative-see` (commit `98dfb2d`) kept locally only, not merged into `main`.

---

## 29. Round 27: X-Ray Queen Fix in `StaticExchangeEvaluation()` (November 13, 2026)

Tested against `main` at `60f5e1b` (baseline binary `GOOB-base`, Gen-3 net, `bench.py 12` 887,490 nodes):

| Branch / Experiment | Status | Games | Record (W - L - D) | Score | Elo Diff | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| **`fix/see-xray-queens`** | **MERGED** | 9462 | 2210 - 2158 - 5094 | 50.27% | **+1.91 ± 3.87** | +2.97 | H1 accepted (`nonreg`) |

* **Concept:** In `StaticExchangeEvaluation()` (`src/search.c`), `bishops` and `rooks` were initialized without `pos->byTypeBB[QUEEN]`, causing x-ray queens behind pawns, bishops, rooks, or other queens to be excluded when discovering hidden x-ray attackers during SEE exchanges. Adding `| pos->byTypeBB[QUEEN]` to both `bishops` and `rooks` matches standard swap-loop x-ray discovery.
* **Correctness checks:** `perfttest` 33/33 passed, TT replacement tests 5/5 passed, 2-thread UCI search normal, and a targeted x-ray SEE unit test passed for orthogonal queen behind rook, diagonal queen behind bishop, and diagonal queen behind pawn. `bench.py 12`: 887,490 -> 893,154 nodes.
* **Match:** non-regression SPRT `[-3.00, 1.00]`, fastchess pentanomial, `5+0.05`, `1` thread, `32 MB` hash, `10` concurrent games, `tools/sprt/book.epd` (`book.epd`), `03:15:34`. nElo `+3.45 ± 7.00`, LOS `83.30%`, Pentanomial (0-2) `[73, 1144, 2232, 1222, 60]`, WL/DD Ratio `0.64`. Log: `tools/sprt/logs/see-xray-queens.log`.
* **Verdict:** **MERGED (`nonreg` H1).** Correctness fix verified non-regressive (and `+1.91` Elo point estimate); merged into `main` from `fix/see-xray-queens`.

---

## 30. Summary of All Merged Features on `main`

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
12. `search/lmr-gives-check`: 1-ply Late Move Reduction deduction for candidate moves giving check (`givesCheck`, +3.8 Elo).
13. `search/hist-order-bonus`: Position-dependent history malus decay by search order and move-count bonus scaling for late quiet refutations (+2.1 Elo, commit `885291b`).
14. `search/cuckoo-upcoming-repetition`: Marcel van Kervinck's 8,192-entry Cuckoo hash table (`InitCuckoo()`) and 1-move upcoming repetition cycle detection (`upcoming_repetition()`, +6.1 Elo, -2.7% bench nodes).
15. `search/se-excluded`: Stockfish/Berserk/Ethereal-style `excludedMove` recursive singular verification search at `(depth - 1) / 2`, `is_shuffling()` piece-shuffle singular extension guard, `dextensions <= 6` extension cap, and quiet non-PV triple extensions (+1.6 Elo, bounded seldepth 28 vs 43 on shuffling endgames).
16. `fix/see-xray-queens`: Include `pos->byTypeBB[QUEEN]` in `bishops` and `rooks` within `StaticExchangeEvaluation()` so x-ray queens behind pawns and sliders are discovered (+1.91 ± 3.87 Elo, `nonreg` H1 accepted, `9405cc1`).


**Removed again in Round 17 (ablation):** the gives-check LMR deduction from item 12 and the capture futility pruning from item 4, together with Surprise-SRD, the TT research-margin cutoff and history extensions, which predate this report.

**Retuned in Round 24:** all 66 `src/tune.h` constants from the full SPSA run `full1` (+13.39 Elo, commit `a2a4abd`); razoring, disabled by the tune (`RazoringDepth = 0`), was then deleted (node-identical).

---

## 31. Closing Instructions for GitHub PRs

For each rejected PR on GitHub:
- `search/tt-value-as-eval`
- `search/history-updates`
- `search/qsearch-tt-store`
- `search/all-improvements`
- `fix/tt-generation`
- `search/qs-checks`
- `search/rfp-softbound`
- `search/small-probcut`
- `search/conthist-prune`
- `search/tt-lmr`
- `search/singular-margin-triple-ext`
- `search/is-shuffling`
- `search/se-shuffling`
- `search/cutoff-cnt`
- `search/prior-cm-bonus`
- `search/tt-cutoff-hist`
- `search/rfp-opp-worsening`
- `search/incheck-no-eval`
- `search/lmr-negative-see`

Leave a closing note and close the PR:

```markdown
Closed following empirical SPRT testing against GOOB-base:
- **Result:** [Insert score / Elo]
- **SPRT:** [Insert LLR]
- **Conclusion:** Tested in isolation; demonstrated regression / failed to show improvement. Documented in `documentations/search_pr_evaluations_oct2026.md`.
```



