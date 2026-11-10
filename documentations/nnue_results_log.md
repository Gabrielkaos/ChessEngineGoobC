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
### Gen-3 net (`checkpoints/exp_r2_gen3/A`)

**Training file** `r2/train_gen3.bin`, 239,789,892 records, built with `mix_data.py --gen-data ../data_gen3
--gen-data ../data_gen:10000000 --exclude ../r2/val_gen3.bin --exclude ../data_mixed/val_gen_50m.bin
--base-data ../data/train.bin --gen-share 0.25 --seed 3`:

| Source | Records | Share |
| :--- | ---: | ---: |
| Gen-3 (minus the hold-out and its 3,069 duplicates) | 49,947,473 | 20.8% |
| Gen-2 (random 10M, minus `val_gen_50m.bin`) | 10,000,000 | 4.2% |
| Lichess (`data/train.bin`) | 179,842,419 | 75.0% |

The first build stalled (random-order reads of `data/train.bin` with too little free page cache: 970 GB
read for about 1 GB written in 38 minutes). `mix_data.py` now reads the sampled base records in file order
(commit `8c969d1`; same records for the same seed, only the output order differs); the rebuild took 20 minutes.

**Training:** warm start from the production net, the Phase 4 recipe with λ 0.75: `--total-samples 400000000`
(1.67 epochs), AdamW (weight decay 0, bucket decay 1e-4), batch 8192, step-cosine 1e-4 → 1e-5, `--select-by last`,
seed 0, 25 minutes on the GPU.

| Validation MSE | after 16M samples | final |
| :--- | ---: | ---: |
| Lichess `data/val.bin` (curve subset) | 0.003892 | 0.003817 |
| `r2/val_gen3.bin` blended / pure eval | 0.007782 / 0.002839 | 0.007308 / 0.002685 |
| `data_mixed/val_gen_50m.bin` (Gen-2) blended / pure eval | 0.005571 / 0.002825 | 0.005698 / 0.002898 |

Full-set Lichess val MSE at the end: 0.004681. Export and `check_net.py --pipeline` clean (no clip or
overflow warnings). Engine `GOOB-r2-gen3-A` (clean build with `EVALFILE`), `bench.py 12` 887,490; mates,
TT tests and UCI normal.

| Run | Games | Record (W - L - D) | Score | Elo | LLR | Verdict |
| :--- | ---: | :---: | ---: | :---: | :---: | :--- |
| **Gen-3 net vs production (STC)** | 7012 | 1789 - 1650 - 3573 | 50.99% | **+6.89 ± 4.58** | +2.95 | **Accepted (H1)** |

Gainer SPRT `[0, 3]` vs `GOOB-main-norazor`, fastchess pentanomial, 5+0.05, 1 thread, 32 MB hash,
10 concurrent games, `tools/book_test.epd`, 2 h 22 min. Pentanomial (0-2) `[59, 801, 1655, 924, 67]`,
nElo +12.23 ± 8.13, LOS 99.84%. Log: `tools/sprt/logs/r2-gen3-A.log`.

* **First Elo gain of Round 2.** Fresh labels from the stronger production net, balanced openings and the
  v2 filters gain about +7 Elo at the same recipe and step count that gave nothing for λ changes.
* **LTC confirmation cancelled by the user** after 744 games (`MODE=nonreg` `[-3, 1]`, 30+0.3, same conditions
  otherwise): 193 - 184 - 367 (50.6%), last report +4.83 ± 13.57 Elo, LLR +0.49 at 720 games, no verdict.
  The user accepted the net on its STC result. Log: `tools/sprt/logs/r2-gen3-A-ltc.log`.
* **Accepted net of Round 2 so far.** `src/weights/quantised.bin` is unchanged; the Gen-3 net is used through
  `EVALFILE=` (plan Phase 8 rule 1). It generates Gen-4.

## Phase 8: Generation loop, Gen-4 (2026-11-09/10)

**Gen-4 data**, generated from the accepted Gen-3 net (verified embedded in a clean `make -C src datagen
EVALFILE=…` build, frozen as `r2/datagen_gen4`). Same v2 flags as Gen-3 with `--seed 20261109`, into `data_gen4/`.
It took 14.6 h for 50,000,415 records (958 positions/s). The files are read-only and hashed in `r2/gen4_manifest.sha256`.

| Statistic | Gen-4 | Gen-3 |
| :--- | :--- | :--- |
| Games: 1-0 / 0-1 / draw | 49.7% / 20.9% / 29.4% | 50.8% / 20.9% / 28.3% |
| W / D / B per record | 47.4% / 31.1% / 21.5% | 48.8% / 29.6% / 21.7% |
| \|eval\| >= 500 / 1500 | 29.9% / 2.7% | 29.8% / 2.7% |
| Resigned with <= 20 records | 0.7% | 0.7% |
| Records per game (mean) | 69.2 | 69.9 |
| Fitted scale K | 164.2 | 152.5 |
| Duplicates (5M sample) | 0.51% | 0.54% |
| Openings rejected / quiet positions dropped | 318,335 / 2,755,540 | 295,759 / 2,581,319 |

Full report: `r2/audit_gen4.txt`. Hold-out: `r2/val_gen4.bin` (50,000 random Gen-4 records, seed 20261110).

### Gen-4 net (`checkpoints/exp_r2_gen4/A`)

**Training file** `r2/train_gen4.bin`, 259,795,112 records (`mix_data.py --gen-data ../data_gen4
--gen-data ../data_gen3:10000000 --gen-data ../data_gen:5000000`, excluding all three hold-outs,
`--gen-share 0.25 --seed 4`):

| Source | Records | Share |
| :--- | ---: | ---: |
| Gen-4 (minus the hold-out and its duplicates) | 49,948,778 | 19.2% |
| Gen-3 (random 10M) | 10,000,000 | 3.8% |
| Gen-2 (random 5M) | 5,000,000 | 1.9% |
| Lichess (`data/train.bin`) | 194,846,334 | 75.0% |

**Training:** warm start from the accepted Gen-3 net with the same recipe as Gen-3 (400M samples, 1.54 epochs).

| Validation MSE | after 16M samples | final |
| :--- | ---: | ---: |
| Lichess `data/val.bin` (curve subset) | 0.003870 | 0.003815 |
| `r2/val_gen4.bin` pure eval | 0.002979 | 0.002848 |
| `r2/val_gen3.bin` pure eval | 0.002790 | 0.002791 |

Full-set Lichess val MSE at the end was 0.004711. Export and `check_net.py --pipeline` are clean.
Engine `GOOB-r2-gen4-A`: `bench.py 12` 1,020,799; mates, TT tests and UCI are normal.

| Run | Games | Record (W - L - D) | Score | Elo | LLR | Verdict |
| :--- | ---: | :---: | ---: | :---: | :---: | :--- |
| **Gen-4 net vs Gen-3 net (STC)** | 17526 | 4149 - 4142 - 9235 | 50.02% | **+0.14 ± 2.89** | −1.88 | **Rejected (stopped, no verdict)** |

Gainer SPRT `[0, 3]` vs `GOOB-r2-gen3-A`: fastchess pentanomial, 5+0.05, 1 thread, 32 MB hash, 10 concurrent
games, `tools/book_test.epd`. It ran 6 h 0 min. Pentanomial (0-2) `[149, 2152, 4167, 2133, 162]`, nElo +0.25 ± 5.14,
LOS 53.75%. Log: `tools/sprt/logs/r2-gen4-A.log`.

* **No gain.** The match was stopped by the user at 17,526 games, short of the 20k cap. The LLR had drifted toward H0
  (lowest −2.20) and the Elo sat at 0.0 ± 2.9. At this point a +3 Elo net is very unlikely. **Not accepted.**
* The second generation from the same recipe gave nothing, after Gen-3 gave +6.9. The likely reason is that one
  more round of 50M self-play positions at 25% share and 5,000 nodes no longer adds information the Gen-3 net
  lacks. The net fits Gen-4 (hold-out eval MSE 0.00298 → 0.00285) without playing better.
* **Decision (user):** do not accept Gen-4, and move to Phase 7 (8 king buckets) on top of the Gen-3 net.
  The planned cheap Gen-4 training variants and Gen-5 are not run. The Gen-3 net stays the accepted net.
