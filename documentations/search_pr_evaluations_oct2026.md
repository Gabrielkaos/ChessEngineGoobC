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

## 4. Closing Instructions for GitHub PRs

For each of the rejected PRs on GitHub:

1. Navigate to the Pull Request on GitHub:
   - PR for `search/tt-value-as-eval`
   - PR for `search/history-updates`
   - PR for `search/qsearch-tt-store`
   - PR for `search/all-improvements`
2. Leave a closing comment summarizing the test data (template below).
3. Click **Close pull request** (do not delete the branch if you wish to keep the commit history for reference).

### Suggested Closing Comment Template

```markdown
Closed following empirical SPRT testing against GOOB-base (60ba471) at 6+0.06s:
- **Result:** [Insert score / Elo]
- **SPRT:** [Insert LLR]
- **Conclusion:** Tested in isolation; demonstrated regression / failed to show improvement. Documented in `documentations/search_pr_evaluations_oct2026.md`.
```
