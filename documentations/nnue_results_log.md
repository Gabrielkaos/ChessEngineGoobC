# NNUE Results Log (Round 2)

Results for [`NNUE_IMPROVEMENT_PLAN_ROUND2.md`](NNUE_IMPROVEMENT_PLAN_ROUND2.md).
Every block lists what was trained, on what, and how it was tested. Elo
claims come only from completed SPRTs (`tools/sprt/run_sprt.sh`).

---

## Phase 0: State, freeze, book split (2026-11-06)

### Real starting state (differs from the plan's assumptions)

| Plan assumption | Actual state on the training machine |
| :--- | :--- |
| A1: production net is D1 | Production `src/weights/quantised.bin` (sha256 `657ae4c5…`) is **`exp_datagen_d1_50m`**: D1's recipe (warm start from D1, 25% self-play, λ 0.75, AdamW, lr 1e-4, 8 epochs, seed 42, best-by-Lichess-val = epoch 7) on `data_mixed/train_d1_50m.bin` (199.8M records = all 50M Gen-2 records + 150M Lichess). Its STC test vs the previous engine (`nnue-d1-stc.log`) ended around +10 ± 19 Elo, inconclusive, and the net was promoted anyway. |
| Phase 3 trains on Gen-2 for the first time | The production net already trained on all of Gen-2 at a 25% share. An earlier Round 2 attempt (run A: 50% Gen-2, warm start from `d1_50m`, 320M samples) **lost** its SPRT (user report; its files were deleted). Phase 3 on Gen-2 is therefore not repeated. |
| A2/A5: Gen-1 (10M) still exists | The 10M Gen-1 set is gone. The generated pool is Gen-2 only (`data_gen/`, 12 workers, 50,000,404 records, 5000 nodes/move, `tools/book.epd` + 4 random plies). |
| Training runs take 15–20 h on the CPU | Training runs on the GPU (GTX 1660 SUPER, 6 GB) at about **270k samples/s**: 400M samples take about 25 minutes. CPU work (SPRTs, datagen) sets the pace. |
| A6: SPSA after a net change | Done on the production net: the full 66-parameter SPSA retune (search Round 24, +13.4 Elo) ran with `d1_50m` embedded. |

### Freeze

* SHA-256 manifest of every file in `data/`, `data_gen/`, `data_mixed/`, `checkpoints/`, `data_selfplay/` and `src/weights/` (74 files): `tools/nnue_project/r2/phase0_manifest.sha256`. Checked with `sha256sum -c --quiet` after every phase.
* Baseline engine: `tools/sprt/bin/GOOB-main-norazor` (main `7315a36`, production net, `bench.py 12` 1,083,787).

### Book split (F6)

`tools/book_test.epd` (every 10th line, 24,167 openings) for SPRTs of new nets,
`tools/book_datagen.epd` (the other 217,503) for datagen from Gen-3 on.

**`tools/book.epd` is an unbalanced (UHO-style) book.** A 20k-node search of 150
random openings gives White a mean of +97 cp (median +97.5); 68 are above +100
and none below −100. This explains Gen-2's game results (below).

---

## Phase 1: Data audit (2026-11-06)

Tool: `tools/nnue_project/scripts/data_stats.py` (read-only). Full reports in
`tools/nnue_project/r2/audit_{gen2,lichess,d1_50m_mix}.txt`.

| Statistic | Gen-2 (50.0M) | Lichess sample (5M of 231.7M) |
| :--- | :--- | :--- |
| Mate-flagged | 0.47% | 0% |
| W / D / B per record | 55.6% / 24.9% / 19.5% | no results |
| \|eval\| ≥ 500 / 1500 / 3000 | 37.9% / 3.3% / 0.5% | 9.8% / 0.2% / 0.0% |
| Output buckets 0–7 | 5.9 15.6 15.0 14.4 14.4 15.4 14.4 4.9 | 1.7 5.5 8.7 11.4 14.7 19.7 22.5 15.9 |
| 8-bucket king layout (13.3), smallest bucket | 3.8% (bucket 7) | 1.4% (bucket 7) |
| Duplicates (5M random sample) | 0.67% | 0.00% |
| **Fitted scale K** (overall) | **141** (per bucket 133–155) | n/a |
| Sign disagreement (\|eval\| > 300, lost by the favoured side) | 0.42% | n/a |

Datagen logs (769,644 games): 1-0 62.7%, 0-1 17.4%, draws 19.9%; resignation
79.3%, eval draw 14.3%, repetition 4.2%, mate 0.9%; records per game mean 64.9;
resigned with ≤ 20 records 4.5%; 1031 positions/s on 12 workers.

**Decisions (plan 7.3):**

* **K = 141 is far from 400.** At +300 cp the side to move scores 0.90, while `sigmoid(300 / 400)` = 0.68: GOOB's search scores are much more decisive than the eval target's scale assumes (the 1500 cp resignation rule adds to this). Phase 4 (λ) moves up, and both λ 1.0 and 0.5 are trained. `NNUE_SCALE` is not changed.
* Early resignations (≤ 4.5%) are under the 10% threshold, so F2 is not urgent; `--max-open-eval` is still used for Gen-3.
* The per-game draw rate (19.9%) is far below the SPRT draw rate (about 48%): the unbalanced book and the 1500 cp resignation rule both push games to decisive results.
* Duplicates are below a few percent: no dedupe.
* No 8-bucket king bucket is under 1%: the Phase 7 layout needs no merge.

---

## Phase 2: Trainer, mixer and datagen v2 (2026-11-06)

Branch `nnue/r2-tools`. Every new flag defaults to the old behaviour.

* `train.py`: `--total-samples`, `--lr-schedule step-cosine`, `--lr-min`, `--select-by {val,last}`, `--val-extra` (blended and pure-eval MSE), `--val-every` learning-curve CSV, `global_step` in checkpoints.
* `mix_data.py`: repeatable `--gen-data PATH[:cap]`, `--exclude <hold-out file>`, `--gen-share 0`.
* `datagen.c`: `--max-fifty`, `--max-open-eval`, `--draw-abs`, `--index` (F1, F2, F3, F5). F4 needed no flag: `resetContinuationTable()` already clears every history table before each game. `datagen.c` also no longer compiled (the engine had dropped `EloNodelimit`); fixed. `run_datagen.py` passes the new flags through.
* Tests: `test_r2_tools.py` 12/12 (default flags reproduce the old trainer bit for bit; `--total-samples` stops at the right step; an interrupted run resumes to the same weights; mixer base-only/exclude/caps), `test_datagen_v2.py` 9/9 (flags off byte-identical to v1), `test_datagen.py` passes.
* Finding: `train.py` never seeds the model's random initialisation, so two from-scratch runs differ. Every Round 2 run warm-starts with `--init`, which is bit-exact on the CPU.

**Best vs last check (8.2): skipped.** The production net is epoch 7 of 8
(best Lichess val); last is epoch 8, the final low-LR epoch. The two differ too
little for an SPRT to resolve within the 20k-game cap, so data experiments use
`--select-by last` as the plan's default.

---

## Phase 4: WDL lambda (2026-11-06/07)

Both runs warm-start from the production net `exp_datagen_d1_50m/nnue.pt` on its
own training set `data_mixed/train_d1_50m.bin`, so λ is the only change:
400M samples, `--lr-schedule step-cosine` 1e-4 → 1e-5, AdamW (weight decay 0,
bucket decay 1e-4), batch 8192, `--select-by last`, seed 0, extra validation set
`data_mixed/val_gen_50m.bin`. About 25 minutes each on the GPU. Output:
`checkpoints/exp_r2_lambda/{L10,L05}/`. Engines are built with
`EVALFILE=<quantised.bin>` after `make -C src clean`.

| Run | λ | Lichess val | Gen-2 val | `bench.py 12` | Games | Record (W - L - D) | Elo | LLR | Verdict |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :--- |
| L10 | 1.0 (pure eval) | 0.004522 | 0.002157 | 1,081,236 | 20000 (cap) | 4913 - 4916 - 10171 | -0.05 ± 2.70 | -2.45 | No verdict at the cap: **no gain, rejected** |
| L05 | 0.5 | 0.004934 | 0.003514 (eval MSE) | 1,064,435 | 1688 | 361 - 463 - 864 | **-21.02 ± 9.34** | -2.94 | **Rejected (H0 accepted)** |

SPRT conditions: gainer `[0, 3]` vs `GOOB-main-norazor`, fastchess
pentanomial, 5+0.05, 1 thread, 32 MB hash, 10 concurrent games,
`tools/book_test.epd`. Pentanomial (0-2): L10 `[157, 2488, 4747, 2417, 191]`, L05 `[23, 246, 393, 174, 8]`.

* **λ 1.0 changes nothing measurable.** Despite K = 141 (Phase 1), training on search scores alone is worth 0.0 ± 2.7 Elo, so the WDL share at 0.75 is not costing strength.
* **λ 0.5 is clearly worse** (-21 Elo). More weight on game results hurts: Gen-2's results are skewed by the unbalanced book (62.7% White wins) and the 1500 cp resignation rule, and Lichess records have no result at all, so a larger WDL share mostly adds label noise.
* **Decision:** keep λ 0.75. `NNUE_SCALE` stays 400. The production net generates Gen-3.
* The L05 SPRT was paused at 766 games on 2026-11-07 for a power brownout (the machine then rebooted) and resumed from fastchess's autosave. The Phase 0 manifest and both engines' benches were checked unchanged after the reboot.

---

## Phase 6: Gen-3 datagen (2026-11-07/08)

Generated from the production net (verified embedded in a clean `make -C src datagen` build, frozen as
`r2/datagen_gen3`) with the v2 flags: `run_datagen.py --workers 12 --positions 50000000 --nodes 5000
--seed 20261107 --book tools/book_datagen.epd --max-fifty 30 --max-open-eval 500 --draw-abs --index`,
into `data_gen3/`. 50,000,542 records in 15.1 h (921 positions/s). Files made read-only and hashed
(`r2/gen3_manifest.sha256`). v2 is judged as part of the Gen-3 net's SPRT; the separate 10M v1-vs-v2
measurement (plan 12.4) was not done.

| Statistic | Gen-3 | Gen-2 |
| :--- | :--- | :--- |
| Games: 1-0 / 0-1 / draw | 50.8% / 20.9% / 28.3% | 62.7% / 17.4% / 19.9% |
| W / D / B per record | 48.8% / 29.6% / 21.7% | 55.6% / 24.9% / 19.5% |
| \|eval\| >= 500 / 1500 | 29.8% / 2.7% | 37.9% / 3.3% |
| Resigned with <= 20 records | 0.7% | 4.5% |
| Records per game (mean) | 69.9 | 64.9 |
| Fitted scale K | 152.5 (per bucket 145-163) | 141 |
| Duplicates (5M sample) | 0.54% | 0.67% |
| Smallest 8-bucket king bucket | 3.3% | 3.8% |
| Openings rejected (`--max-open-eval 500`) | 295,759 (29% of attempts) | n/a |
| Quiet positions dropped (`--max-fifty 30`) | 2,581,319 | n/a |

Full report: `r2/audit_gen3.txt`. Hold-out: `r2/val_gen3.bin` (50,000 random Gen-3 records, seed 20261108).
Training mix (planned, not yet built): all Gen-3 minus the hold-out + 10M Gen-2 (minus `val_gen_50m.bin`)
at a 25% generated share with Lichess, 239.8M records, `r2/train_gen3.bin`; same recipe as Phase 4 with λ 0.75.
