# NNUE Improvement Plan – Round 2

Follows [`NNUE_IMPROVEMENT_PLAN.md`](NNUE_IMPROVEMENT_PLAN.md) (Round 1, called
**R1** below). This round starts when the Gen-2 self-play set is finished: 50M
positions generated with the D1 net by `tools/datagen.c`.

R1 changed the network's architecture: mirroring, factorized king buckets and
the accumulator cache. Round 2 is mostly about **data**. Self-play data now
exists, and D1 showed it helps. The open questions are how much of it to use,
how to weight it, how to make it better and how to keep the cycle running. One
architecture step (8 king buckets) comes after the data work, because more
data is what makes it pay off.

Everything in this round answers to one design target, stated in full in the
next section: **a net that reaches full strength on 200M–1B training positions,
trained on one home CPU.** That is the reason GOOB uses a Schoenemann-style
network rather than a Stockfish-style one that needs billions of positions.

The rules from R1 still apply: every change gets an SPRT against the previous
accepted engine, net changes get an LTC confirmation, and nothing existing in
`data/`, `data_gen/`, `checkpoints/` or `src/weights/` is ever overwritten
(`AGENTS.md`, NNUE Protection).

> **Status (fill in as phases complete):**
> - Phase 0 (sync, freeze, back-fill R1 results): pending
> - Phase 1 (Gen-2 data audit): pending
> - Phase 2 (trainer and mixer support): pending
> - Phase 3 (Gen-2 training, share at a fixed step count): pending
> - Phase 4 (WDL lambda): pending
> - Phase 5 (search re-tune after the net change): pending
> - Phase 6 (datagen v2, Gen-3): pending
> - Phase 7 (8 king buckets): pending
> - Phase 8 (generation loop): pending

---

## Design Target: Strong at 200M–1B Positions

### The target

GOOB's net must reach full strength when trained on **200M to 1B unique
positions in total**, Lichess and self-play together, on the home CPU. It must
not depend on the multi-billion-position datasets that Stockfish-style nets
are trained on. The budget counts **unique positions**. Several epochs over
the pool, and warm starts that reuse it, are fine.

### Why the current architecture fits it

The net is Schoenemann-style, `(768xK -> 1024)x2 -> 1x8` with SCReLU. With
about 300M Lichess positions it already produced a strong net. The reasons
(R1 Section 1) are properties to protect, not accidents:

- **One hidden layer, small input set.** The feature-transformer weight
  `ft[f][j]` gets a gradient every time feature `f` is active. Nothing in the
  net is so rare that it needs billions of positions to be estimated.
- **Structure that shares statistics.** Mirroring folds kingside and queenside
  into one set of features. Factorized king buckets learn a shared weight from
  every position and only a small per-bucket correction. Both add knowledge
  without dividing the data.
- **Good labels.** Deep evals and, now, self-play scores with game results.
  When data is limited, a better label is worth more than an extra row.
- **Exact warm starts.** Each accepted change so far started at its parent's
  strength, so the new net only had to learn the difference.

### Rules for this round

1. **Data quality before data volume.** Better labels (stronger generating
   nets, the datagen v2 filters, calibration) and a better distribution
   (self-play) come first. The training pool is capped at **1B unique
   positions**, Lichess included (Phase 8).
2. **Structure before capacity.** A change that shares statistics or adds a
   factorized, warm-started refinement (like Phase 7) is in scope. A change
   that only adds capacity (wider L1, extra layers, threat inputs) is out of
   scope unless it comes with an argument that it pays off inside the budget
   (Section 15).
3. **Win inside the budget or not at all.** Every candidate trains on data
   from the capped pool at the standard step budget. A change that would only
   win with more data than this fails its SPRT here, and that is the intended
   result.
4. **Watch for saturation.** Learning curves from every training run
   (Phase 2.1) show whether a run is limited by steps, by data or by the
   architecture. Only the last case justifies an architecture change.
5. **Stay trainable from zero.** A chain of warm starts can hide a net that
   cannot be reproduced. Once per round, a from-scratch run on the pool
   confirms the architecture still reaches its strength inside the budget
   (Phase 8, budget check).

---

## Table of Contents

- [Design Target: Strong at 200M–1B Positions](#design-target-strong-at-200m1b-positions)
1. [Assumptions to Verify First](#1-assumptions-to-verify-first)
2. [What R1 Established, and What It Did Not](#2-what-r1-established-and-what-it-did-not)
3. [Review of `tools/datagen.c`](#3-review-of-toolsdatagenc)
4. [Goals and Constraints](#4-goals-and-constraints)
5. [Strategy](#5-strategy)
6. [Phase 0 – Sync, Freeze, Back-fill](#6-phase-0--sync-freeze-back-fill)
7. [Phase 1 – Gen-2 Data Audit and WDL Calibration](#7-phase-1--gen-2-data-audit-and-wdl-calibration)
8. [Phase 2 – Trainer and Mixer Support](#8-phase-2--trainer-and-mixer-support)
9. [Phase 3 – Gen-2 Training: Share at a Fixed Step Count](#9-phase-3--gen-2-training-share-at-a-fixed-step-count)
10. [Phase 4 – WDL Lambda](#10-phase-4--wdl-lambda)
11. [Phase 5 – Search Re-Tune and In-Check Eval](#11-phase-5--search-re-tune-and-in-check-eval)
12. [Phase 6 – Datagen v2 and Gen-3](#12-phase-6--datagen-v2-and-gen-3)
13. [Phase 7 – 8 King Buckets with an Exact Warm Start](#13-phase-7--8-king-buckets-with-an-exact-warm-start)
14. [Phase 8 – The Generation Loop](#14-phase-8--the-generation-loop)
15. [Deferred Work](#15-deferred-work)
16. [Compute Budget](#16-compute-budget)
17. [Testing Checklist Additions](#17-testing-checklist-additions)
18. [Risks and Mitigations](#18-risks-and-mitigations)
19. [Open Questions](#19-open-questions)
20. [Housekeeping Found During the Review](#20-housekeeping-found-during-the-review)

---

## 1. Assumptions to Verify First

This plan was written from a checkout at commit `b9b1111` on a second machine.
The D-nets, the generated data and possibly some uncommitted code exist only
on the training machine. Confirm each row before Phase 0 ends, and fix the
plan wherever one is wrong.

| # | Assumed | Evidence on this checkout | How to confirm at home |
|---|---|---|---|
| A1 | The production net is **D1** (promoted per `PHASE2_DATAGEN_GUIDE.md` step 8) | Not here: `src/weights/quantised.bin` is still the `exp_4bkt` net (sha256 `09d4601f…`). Weights are gitignored | `sha256sum src/weights/quantised.bin` vs `exp_datagen_d1/quantised.bin` |
| A2 | D1 = warm start from `exp_4bkt/nnue.pt`, about 10M Gen-1 records plus about 30M Lichess records (25% share), 8 epochs, lr 1e-4, λ 0.75, AdamW (wd 0, bucket decay 1e-4), exported from `--out-best` | `PHASE2_DATAGEN_GUIDE.md` steps 3–5; `mix_data.py` uses all generated records and sizes the base sample to fit the share | the shell history or log of the D1 run |
| A3 | D2 = 10M Gen-1 + 10M Lichess, same recipe. D3 = **D2's file** with λ 0.5 | guide step 4 trains D3 on `train_d2.bin`, not on D1's file as R1 6.5 specified | same |
| A4 | D1 beat `exp_4bkt` by SPRT, and D1 beat D2 and D3 | user report | the cutechess logs: games, W/L/D, Elo, LLR, STC/LTC |
| A5 | Gen-2 = 50M records from D1 at 5000 nodes per move, `tools/book.epd` + 4 random plies, default adjudication | user report, `run_datagen.py` defaults | `gen_w*.log` headers in the output directory |
| A6 | R1 Phase 5 (SPSA re-tune after a net change) is done for **some** net | `auto-shutdown-after-spsa.sh` exists | the SPSA run name and the `tune.h` diff |
| A7 | R1 Phase 6 (recipe flags `--loss-power`, `--warmup-batches`) is **not** implemented | `train.py` has neither flag | `git status` at home |
| A8 | The R1 progress block lost its Phase 1 line by accident | `b9b1111` deleted "Phase 1 … accepted … `quantised_phase1_control.bin`" | restore it if Phase 1 was in fact accepted |

Uncommitted work at home: commit it (or note it in Phase 0) before any Round 2
code change, so Round 2 branches start from the real state
(`git fetch && git status` on both machines).

---

## 2. What R1 Established, and What It Did Not

### Accepted chain

```
base (~300M Lichess positions, Schoenemann-style 768 -> 1024 x2 -> 1 x8)
 -> R1 Phase 1 control fine-tune        accepted STC+LTC
 -> R1 Phase 3 mirroring                accepted STC+LTC
 -> R1 Phase 4 4 king buckets + cache   accepted
 -> D1 (25% self-play, λ 0.75)          accepted (A1, A4)
```

### What the D runs show

**Solid:** a warm-start fine-tune of the 4-bucket net on Lichess data mixed with
GOOB self-play data (25% share, λ 0.75) is stronger than the 4-bucket net. That
is the direction this round builds on.

**Not established**, because of how the runs were set up (A2, A3):

| Claim | Why it does not follow |
|---|---|
| "25% self-play is better than 50%" | D1's file had about 40M rows and D2's about 20M, both at 8 epochs. D1 got **twice the optimizer steps** and three times the Lichess rows. Share, step count and Lichess volume all changed at once |
| "λ 0.75 is better than λ 0.5" | D3 used D2's file, so D1 vs D3 changes share, steps and λ. Only D2 vs D3 isolates λ, and only at a 50% share |
| "The self-play data caused D1's gain" | There was no same-recipe Lichess-only control (warm start from `exp_4bkt`, 8 epochs over 40M Lichess rows). R1 Phase 1 showed that a fine-tune by itself gains Elo, so part of D1's gain may be the extra training |
| "The exported epoch was the best one" | `train.py` keeps the checkpoint with the lowest MSE on **Lichess** `val.bin` (`train.py:483–494`). With self-play in the mix, that picks the epoch that fits Lichess best, not the one that plays best. It may also have handicapped D2 and D3, whose files held less Lichess data |

None of this undoes D1. It decides how Round 2 runs its experiments:
**fixed step counts, a Lichess-only control, the last checkpoint exported, and
validation on both distributions.**

---

## 3. Review of `tools/datagen.c`

The `b9b1111` commit message says "not quite sure if implemented correctly".
A full read against the engine and trainer conventions found **no defect that
invalidates Gen-1 or Gen-2**. The details follow, so the 50M set can be used as
it is and the improvements go into datagen v2 (Phase 6).

### Verified correct

- **Score sign.** `info->bestScore` is `voteScore = pvScore[0]` from the last
  *completed* iteration (`search.c:1384`, exported at `search.c:1755`). That is
  the side-to-move score, and a node-limit stop in the middle of an iteration
  does not leak a partial score into it. `datagen.c:496` converts it to White's
  view, and `nnue_collate` converts it back with `stm`. The signs match.
- **Result codes.** On mate, if White is to move Black has won, so the code is 3
  (`datagen.c:448`). Codes are stamped into flag bits 1–2 after the game ends,
  as `fen_utils.py` and `nnue_collate` expect.
- **Mates.** Clipped to ±3000 with the mate flag set. The collate treats them as
  ordinary ±3000 targets (sigmoid ≈ 0.9994), which is fine.
- **Quiet filter.** Not in check, best move not tactical, best move does not
  give check (`datagen.c:503–514`). This is the same definition as
  `prepare_data_jsonl.py`, as R1 6.3 required.
- **Output safety.** `O_CREAT | O_EXCL`, a path deny-list, and records written
  only after a whole game is buffered. A crash loses at most the game in
  progress.
- **Engine hooks.** `info->silent` only suppresses output. The
  `bestMove`/`bestScore` export happens after the search, so UCI play is
  unchanged.

### Findings (quality, not correctness)

| # | Finding | Effect | Fix in |
|---|---|---|---|
| F1 | **Fifty-move deflation leaks into the labels.** `adjustEvalOnFmr` scales evals by `(200 - fiftyMove) / 200` (`search.c:144`). `useFiftyMoveRule` defaults to TRUE (`uci.c:619`), and the 68-byte record has no halfmove clock | At a clock of 40, labels are shrunk by 20% toward 0 in positions the net cannot tell apart from clock-0 positions. This is concentrated in endgames, where shuffling raises the clock | Phase 6, `--max-fifty` |
| F2 | **No check on opening balance.** Four random plies on top of a book line can hang material, and the game is played and recorded anyway | Games decided by a random blunder produce many lopsided records and few useful ones. Phase 1 measures how common this is | Phase 6, `--max-open-eval` |
| F3 | **Draw adjudication counts plies from the book position** (`hisPly`, `datagen.c:559`), not from move 1 | "After move 40" really means 40 moves *after the book line*. This is harmless but differs from `run_sprt.sh` | Phase 6 |
| F4 | **Search state carries across games.** Only the TT and continuation history are reset (`datagen.c:374–375`). Quiet, capture and correction histories persist | A file is still deterministic for a given seed, but one game cannot be reproduced on its own | Phase 6 (optional) |
| F5 | **No game boundaries in the output.** Game length and adjudication statistics exist only in the worker logs | Audits depend on keeping the logs | Phase 6, sidecar index |
| F6 | **Datagen and SPRT use the same book** (`tools/book.epd`) | The nets are trained on positions from the openings the SPRT then plays. A gain could be partly specific to those openings | Phase 0, book split |

---

## 4. Goals and Constraints

### Goals

- Elo by SPRT against the previous accepted net, confirmed at LTC.
- Stay inside the design target: the net stays at full strength when trained
  on 200M–1B unique positions.
- Make self-play data the main source of improvement, with a cycle (Phase 8)
  that improves label quality and does not just grow the pool.
- Keep both R1 strengths: every new net warm-starts from the previous one, and
  a training run stays under about a day on the home CPU.

### Hard constraints (same as R1, plus four new ones)

- NNUE protection: new experiment directories only. Generated data goes into
  new `data_gen*/` directories. `src/weights/quantised.bin` is replaced only by
  promoting an accepted net (`cp` after a backup copy, as in the Phase 2 guide).
- One logical change per experiment.
- **New: data budget.** The training pool never holds more than 1B unique
  positions (Lichess + all self-play generations). Architecture changes are
  judged only on data from that pool (Design Target, rules 2–3).
- **New: fixed step count.** Data experiments fix the number of training
  samples seen, not the number of epochs (Phase 2.3). R1's D runs show what
  happens otherwise.
- **New: export the last checkpoint** (`--out-last`) for data experiments,
  unless Phase 2 shows that best-by-validation plays better. Log validation MSE
  on both distributions either way.
- **New: no test-book overlap.** Datagen draws openings from a different part
  of the book than the one SPRT uses (Phase 0.4).
- READMEs (`tools/README.md`, `tools/nnue_project/README.md`, `src/README.md`)
  are updated with every change to `tools/` or `src/`.

---

## 5. Strategy

| Phase | Change | CPU cost | Expected effect | Decided by |
|---|---|---|---|---|
| 0 | Sync both machines, freeze D1 as the baseline, back-fill R1 results, split the book | minutes | none | checks |
| 1 | Read-only audit of Gen-2 plus WDL calibration | under 1 h | none; it steers Phases 3, 4 and 6 | numbers |
| 2 | Trainer: extra validation sets, fixed sample budget, per-step cosine. Mixer: base-only subsets, per-source caps, low-RAM writer | small code | none by itself | checks |
| 3 | Gen-2 training: 50% and 25% share at equal steps, plus a Lichess-only control | 3 runs, about 15–20 h each | **largest in this round** | SPRT, LTC |
| 4 | λ at the winning share: 1.0 (pure eval) and 0.5 | 2 runs | small to moderate | SPRT |
| 5 | SPSA re-tune on the new net; skip static eval in check (search) | SPSA hours + 2 SPRTs | small to moderate | SPRT |
| 6 | Datagen v2 (F1–F6), then Gen-3 from the Phase 3/4 winner | small code + 1–2 days datagen | moderate (data quality and volume) | SPRT of the next net |
| 7 | 8 factorized king buckets, warm-started exactly from the 4-bucket net | medium code; training 1.5–2x per step | moderate | SPRT vs same-recipe control, LTC |
| 8 | Generation loop: data from net *k* trains net *k+1*, with rules for mixing and stopping | ongoing | cumulative | SPRT each generation |

### If time is short

Phase 0 → Phase 1 → **Phase 3 run A alone, SPRT vs D1** → promote → Phase 6 →
Gen-3. The other runs answer research questions (is self-play still the cause,
which share, which λ). They are worth their cost because Phase 8 runs on those
answers, but none of them blocks shipping a better net.

---

## 6. Phase 0 – Sync, Freeze, Back-fill

### 6.1 Sync

Commit or record everything uncommitted at home (A7, A8). Push. Pull on the
laptop. Round 2 branches start from that commit.

### 6.2 Freeze the baseline

```bash
sha256sum src/weights/quantised.bin tools/nnue_project/checkpoints/exp_datagen_d1/*.pt
make -C src native -j8
cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-d1
python3 tools/sprt/bench.py tools/sprt/bin/GOOB-d1 12
```

Store the SHA-256 of every file in `data/`, `data_gen*/` and `checkpoints/` in
a text file outside those directories, and re-check them at the end of each
phase (R1 13, last item).

### 6.3 Back-fill R1 results

Fill in one R1 Section 14 results block each for D1, D2 and D3, and for any
SPSA run (A6). Put them at the end of R1 or in a new
`documentations/nnue_results_log.md`. The confounds in Section 2 belong in each
block's Notes field, so nobody reads "25% > 50%" from them later.

### 6.4 Split the opening book (F6)

Write two new files and leave `tools/book.epd` untouched:

```bash
# deterministic split: every 10th line goes to the test book
awk 'NR % 10 == 0' tools/book.epd > tools/book_test.epd
awk 'NR % 10 != 0' tools/book.epd > tools/book_datagen.epd
```

From Gen-3 on, `run_datagen.py --book tools/book_datagen.epd`, and every SPRT
runs with `BOOK=tools/book_test.epd`. That leaves about 24k test openings for
at most 1500 rounds. Gen-1 and Gen-2 were generated from the full book. That
cannot be undone, but every later generation is clean.

---

## 7. Phase 1 – Gen-2 Data Audit and WDL Calibration

Read-only. Run it on Gen-2 before any training run, because its numbers decide
the mix (Phase 3), how much λ matters (Phase 4) and which datagen fixes come
first (Phase 6).

### 7.1 New tool `tools/nnue_project/scripts/data_stats.py`

Opens record files with `np.memmap(mode="r")` and never writes beside them. It
prints:

| Statistic | Why |
|---|---|
| Record count, mate-flag share, W/D/L share **per record** | sanity check, and the WDL prior the net sees |
| Eval histogram (stm-relative) and the share with \|eval\| ≥ 500 / 1500 / 3000 | how many records come from games already decided (F2) |
| Piece-count histogram mapped to the 8 output buckets | whether any output bucket is starved |
| King-bucket histogram per perspective, for both the current 4-bucket layout and the Phase 7 8-bucket layout (13.3) | whether Phase 7's new buckets will have data |
| Duplicate rate (hash of bytes 0–64) on a 5M sample | how much the book plus 4 random plies repeats positions |
| **Calibration table:** bin by stm eval (for example 50 cp bins), and per bin give the mean game score from the result vs `sigmoid(cp / 400)` | whether GOOB's search scores and game outcomes agree on one scale |
| **Fitted scale K:** the K that minimizes log-loss of `sigmoid(cp / K)` against the results, overall and per output bucket | one number for the calibration |
| Sign disagreement: records with \|eval\| > 300 whose result has the opposite sign | label noise between the two targets |

`--log-dir` additionally parses the `gen_w*.log` files: games, positions per
game, adjudication reasons (checkmate, resignation, eval draw, repetition,
fifty-move, insufficient material, max ply), W/D/L **per game**, positions per
second, and the share of games resigned within 20 plies of the opening.

### 7.2 Run it on

- Gen-2 (all worker files)
- Gen-1 (the R1 10M set)
- A 5M sample of Lichess `train.bin` (no results, so only the eval and bucket
  statistics apply). This shows how far apart the two distributions are.

### 7.3 Decision rules

| Observation | Action |
|---|---|
| K within about ±20% of 400 | the λ blend mixes two targets on almost the same scale. λ is mainly a noise-versus-bias trade, and Phase 4 is low priority |
| K far from 400 | the eval target and the result target disagree about what a centipawn is worth. Phase 4 moves up. **Do not change `NNUE_SCALE`**: the search margins in `tune.h` are in the current centipawn unit |
| More than about 10% of games resigned within 20 plies of the opening | F2 (opening filter) goes first in Phase 6 |
| Draw share per game far from GOOB's self-SPRT draw rate | check the adjudication thresholds before generating Gen-3 |
| A Phase 7 bucket with under 1% of records | merge it with a neighbour in the 8-bucket layout before training |
| Duplicate rate above a few percent | dedupe in `mix_data.py` (Phase 2.4) |

Also run `check_net.py --pipeline` on a sample of Gen-2 records (R1 6.7).

Document the tool in `tools/nnue_project/README.md`.

---

## 8. Phase 2 – Trainer and Mixer Support

Code only. Every new flag defaults to today's behaviour, so the R1 commands
still reproduce R1 results.

### 8.1 Extra validation sets (`train.py`)

`--val-extra <file>`, repeatable. After each epoch, log MSE on each extra set
next to the main `--val` MSE. For extra sets with results, log **two** numbers:
MSE against the blended target (depends on λ) and MSE against the pure-eval
target (comparable across λ).

**Learning curves.** `--val-every N` (in batches, default off) also
evaluates every validation set every N batches and appends one CSV row per
evaluation (step, samples seen, train loss, each validation MSE) to a file
beside `--out-last`. Use 20 to 50 evaluations per run on a 200k-record
validation set; each costs seconds. This is the Design Target's saturation
check (rule 4), and it costs nothing extra in CPU. How to read the curves:

| Shape at the end of a run | Meaning | Next step |
|---|---|---|
| Validation MSE still falling while the LR is low | limited by steps | larger `--total-samples` |
| Training loss keeps falling, `val_gen2` MSE flat or rising, rows repeated 4+ times | limited by data (the net memorizes) | more or fresher generations (Phase 8) |
| Both flat early, rows seen only 1–2 times | limited by the architecture at this data size | a structural change (Phase 7) is justified |

### 8.2 Checkpoint selection

`--select-by {val,last}`, default `val` (today's behaviour). Data experiments
use `last`. Then `--out-best` is simply the final epoch, and the cosine
schedule ends where it was meant to.

Run one check before Phase 3: export both best and last of **D1** and SPRT them
against each other. If last is not worse, keep `last` for every later run. If
best is clearly better, find out why first (maybe the LR is too high at the
end).

### 8.3 Fixed sample budget and per-step cosine

`--total-samples N`. The run stops after `N / batch_size` steps whatever the
file size. With `--lr-schedule step-cosine`, the LR anneals per batch over
those steps, from `--lr` to `--lr-min` (today `eta_min=1e-5` is hard-coded at
`train.py:352` and steps once per epoch). Mid-epoch resume must restore the
step counter. Use the existing `save_checkpoint` fields; add a
`global_step`.

This removes the confound in Section 2. Runs with different file sizes see the
same number of samples on the same LR curve.

### 8.4 `mix_data.py`

- **Base-only subsets.** Allow `--gen-share 0`, so a Lichess-only control file
  is built with the same sampler and seed as the mixed files
  (`mix_data.py:157` rejects it today).
- **Several generated sources with caps.** For example
  `--gen-data data_gen2:all --gen-data data_gen1:5000000`, so older generations
  can be down-weighted (Phase 8).
- **Low-RAM writer.** Today the whole output is built in memory
  (`np.concatenate` at `mix_data.py:196` and `:222`). A 120M-row output is
  8.2 GB, plus the gen arrays, plus a 300M-entry permutation for the base
  sample (`:217`). Write in shuffled chunks instead: choose indices first, then
  stream in blocks of a few million rows. Do this before Phase 3 if the
  training machine has less than about 24 GB of RAM.
- **`--dedupe`** (optional): drop exact duplicate positions by hashing bytes
  0–64. Only needed if Phase 1 shows duplicates.
- Hold-out sets are **fixed for the whole round**. Make `val_gen2.bin` once
  (200k Gen-2 records) and never let a training file contain them. Add
  `--exclude <val file>`, or build every Phase 3/4 file from one shared
  permutation with the same seed.

### 8.5 `--loss-power` (only if A7 holds)

R1 Phase 6's `--loss-power p` (loss `mean(|sigmoid(out) - target|^p)`). It
mattered little in R1 because there were no WDL targets. Now there are. It is
**not** gated on validation MSE (a different loss scores worse on MSE by
construction). One SPRT, after Phase 4, at p = 2.5 against p = 2 with
everything else identical.

### 8.6 Tests

- With the default flags, a 200-batch run reproduces today's loss values
  exactly (same seed).
- `--total-samples` stops at the right step, and a resume from a mid-epoch
  `last.pt` reaches the same final weights as an uninterrupted run.
- `mix_data.py` with `--gen-share 0` gives a file whose records all have
  `res_code == 0`. The chunked writer's output, sorted, equals the in-memory
  writer's output, sorted, for the same seed.

---

## 9. Phase 3 – Gen-2 Training: Share at a Fixed Step Count

### 9.1 Data

- Generated pool: Gen-1 (about 10M, from `exp_4bkt`) + Gen-2 (50M, from D1)
  minus `val_gen2.bin`, about **60M** records.
- Lichess: `data/train.bin` (about 300M).
- Validation: `data/val.bin` (main) + `val_gen2.bin` (extra) + R1's
  `val_gen.bin` (extra).

### 9.2 Runs

All runs: warm start `--init exp_datagen_d1/nnue.pt`, `--mirror --king-buckets 4
--optimizer adamw --weight-decay 0 --bucket-decay 1e-4 --batch-size 8192
--lr 1e-4 --lr-schedule step-cosine --wdl-lambda 0.75 --seed 0
--select-by last --val-every 2000`, and the **same `--total-samples`**. Suggested budget:
480M (D1 used about 320M). Measure the speed first and shrink the budget if a
run would take over a day.

| Run | Training file | Purpose |
|---|---|---|
| **A** | all 60M generated + 60M Lichess (50%) | ship candidate: uses every generated record |
| **B** | 40M generated + 120M Lichess (25%) | share, at equal steps |
| **C** | 160M Lichess, no generated data (`--gen-share 0`) | control: how much is "another fine-tune"? |

B's file is not A's file at a lower share; it has to be a separate file. B and
C get the same total size (160M) so they repeat rows equally often. A is
smaller (120M), so its rows repeat more. That is inherent to using all the
generated data, and the step count is what is held fixed.

Output into `checkpoints/exp_r2_gen2/{A,B,C}/`.

### 9.3 Order and SPRTs

1. Train **A**. SPRT A vs GOOB-d1, STC (`BOOK=tools/book_test.epd`). If it
   passes, LTC.
2. Train **C**. SPRT A vs C. A pass means self-play data still adds more than
   the training does. A fail means D1's gain was mostly the fine-tune, and Phase
   6/8 datagen time should shrink in favour of Phase 7.
3. Train **B** only if A passed. SPRT A vs B. The winner's share carries
   forward.

Promote the best accepted net (backup first). It becomes the baseline for
Phase 4 and generates Gen-3.

### 9.4 What to record

Everything in R1 Section 14, plus: total samples, generated share, the
composition of the generated pool by generation, MSE on all three validation
sets per epoch, and the Phase 1 statistics of the pool used.

Also record the **learning-curve verdict** for A, B and C (step-, data- or
architecture-limited, using the 8.1 table). These three curves are the first
direct measurement of how much data this architecture can use. They decide
whether the next CPU-days go to datagen (Phase 6/8) or to Phase 7.

---

## 10. Phase 4 – WDL Lambda

At Phase 3's winning file and budget, change only λ:

| Run | λ | Purpose |
|---|---|---|
| L1 | 1.0 | pure eval: is the result signal helping at all? |
| L5 | 0.5 | more result weight |

SPRT each against the Phase 3 winner (λ 0.75). Compare validation sets on the
pure-eval MSE column only (8.1). The blended-target MSE is not comparable
across λ.

Priority depends on Phase 1. If K is near 400, train L1 first (it answers the
bigger question). If K is far from 400, train both.

A λ that changes over training (for example 1.0 → 0.6) is common practice
elsewhere but has weak evidence here. Try it only if L1 and L5 both lose
narrowly.

---

## 11. Phase 5 – Search Re-Tune and In-Check Eval

Search changes: the `AGENTS.md` search rules apply (bench, correctness checks,
SPRT against the same-net baseline).

### 11.1 SPSA after the new net

As R1 Phase 5: the margin group (`Futility|SEE|Razor|BetaMargin|probCutMargin`)
on the accepted Round 2 net, then SPRT tuned vs untuned with the same net. From
here on, re-tune every **second** accepted net instead of every net. SPSA is
expensive, and two generations of net drift is when the margins go stale.

### 11.2 Skip the static eval in check (R1 Open Question 5)

`AlphaBeta` computes `rawEval` in check and stores it uncorrected
(`search.c:371–375`). Every training source filters out in-check positions:
Lichess (quiet-only), Gen-1, Gen-2 and datagen v2. So the net never sees one,
and **more data will not close this gap**. Try the Stockfish convention: no
static eval in check (`staticEval = VALUE_NONE`), with `improving` and the TT
eval field handled to match. One SPRT. If it fails, the alternative is to stop
filtering in-check positions in datagen v3. That is a data change, so test it
only after Phase 6.

---

## 12. Phase 6 – Datagen v2 and Gen-3

### 12.1 Changes to `tools/datagen.c`

Each one is a flag, default off, and logged in the run header.

| Flag | Fixes | Behaviour |
|---|---|---|
| `--max-fifty N` (suggest 30) | F1 | skip a record when `fiftyMove > N`; count skipped records |
| `--max-open-eval N` (suggest 500) | F2 | after the random plies, run one search with the normal node budget. If \|score\| > N, discard the opening and pick another; count rejections |
| `--draw-move` from the game start | F3 | count plies from the first random ply (or read the FEN fullmove number) instead of `hisPly` |
| `--fresh-histories` | F4 | reset all search histories at the start of each game |
| `--index <file>` | F5 | sidecar file with one row per game: first record offset, record count, result, adjudication reason, plies. Lets `data_stats.py` compute per-game statistics without the logs |

Keep the 68-byte record format. The trainer, mixer and checker all read it,
and nothing in this plan needs more bytes.

### 12.2 Tests (extend `tools/test_datagen.py`)

- With every new flag off, output is byte-identical to v1 for the same seed.
- With `--max-fifty 30`, no record is from a position whose clock exceeds 30
  (check this with the index file, or by replaying a short game).
- With `--max-open-eval`, the rejection count is non-zero on a small run, and
  no game in the index ends by resignation within the first few plies.
- The index file's record counts sum to the file size divided by 68.

### 12.3 Gen-3

Generate from the Phase 3/4 winner with `--book tools/book_datagen.epd` and
the new flags on, into a **new** directory (`data_gen3/`). Size: 50–100M,
depending on the throughput measured in Phase 1. Run `data_stats.py` on it
before training.

### 12.4 Measuring v2

Measuring v2's Elo in isolation costs a second datagen run of the same size
with v1, plus two training runs and an SPRT. That is too expensive at full
size. The cheap version, if wanted: 10M records with v1 and 10M with v2 from the
same net and seed, two identical fine-tunes, one SPRT. Otherwise v2 is accepted
on its tests and statistics, and judged as part of the Gen-3 net's SPRT. Say in
the results log which of the two was done.

---

## 13. Phase 7 – 8 King Buckets with an Exact Warm Start

### 13.1 Why now, and why it fits the budget

R1 deferred more buckets until Phase 4 passed and the data had grown. Both are
now true. The factorizer already keeps the data cost low, and an exact warm
start (13.4) means the new net starts at the old net's strength.

This is the one architecture change in Round 2. It is a **structural** change
(Design Target, rule 2), and its data cost can be counted:

- `ft_shared` (768 x 1024) still learns from every position, as today.
- Each bucket's correction `ft_buckets[b]` learns from the positions where
  some perspective's king is in bucket `b`. With the layout in 13.3 and a
  minimum share of about 1% per bucket (checked in Phase 1), a 200M pool gives
  every bucket at least about 2M positions. That is enough for a correction
  term that weight decay pulls toward zero.
- The output layer and L1 width do not change, so capacity beyond the
  bucket corrections does not grow.

Prerequisite: the Phase 3 learning curves (9.4) do **not** say "limited by
data". If they do, more data comes first. Splitting buckets further when the
current net is starved of data spends the budget in the wrong place.

### 13.2 What it fixes

R1 Open Question 3: bucket 1 merges the uncastled king (e1 → d1 after
mirroring) with the queenside-castled king (c1) and f1. Bucket 3 covers 24 of
the 32 relative squares, every "active king" from rank 3 to rank 8.

### 13.3 Layout

Relative squares after the mirror flip (files a–d only):

```
            a  b  c  d
rank 6-8:   7  7  7  7      far-advanced king (endgames)
rank 4-5:   6  6  6  6      centralized king
rank 3:     5  5  5  5      king one step out
rank 2:     3  3  4  4      luft / fianchetto  |  king walked up the centre
rank 1:     0  0  1  2      castled corner     |  c1 (f1)  |  d1 (e1, uncastled)
```

Parent map to the current 4-bucket layout: `0→0, 1→1, 2→1, 3→2, 4→2, 5→3,
6→3, 7→3`. Check the Phase 1 histogram first and merge any bucket under about
1% of records.

### 13.4 Exact warm start

New trainer flag `--init-rebucket <4bkt.pt> --bucket-parent 0,1,1,2,2,3,3,3`:
copy `ft_shared`, set `ft_buckets_new[b] = ft_buckets_old[parent[b]]`, and copy
the bias and output layer. At step 0, every position's features map to the
same effective weights as before, so the 8-bucket net's output is **identical**
to its parent's.

Test: export the warm-started net **before any training step** and check that
`check_net.py` and the engine give exactly the parent net's evals on the
default FEN set. This also tests the C and Python bucket tables against each
other.

### 13.5 Trainer and engine

- `KING_BUCKET` table (`model.py`) and `--king-buckets` choices gain 8. The
  `KingBucket[64]` table in `nnue_loader.h` (symmetric across the d/e line) and
  `NNUE_KING_BUCKETS=8` in the makefile change to match. The format tag already
  carries `king_buckets`.
- Finny cache `nnue_cache[2][2][8]`: about 68 KB per thread.
- More bucket boundaries mean more refreshes (c1↔d1, rank 3, rank 4/5,
  rank 5/6). The refresh counter in `trace.c` (R1 7.5 step 8) reports them.
- FT table 12.6 MB (from 6.3 MB). This is the main NPS risk. Run the paired NPS
  harness against the 4-bucket binary.
- Training: AdamW updates about 7.1M FT parameters per step (from 3.9M),
  because `embedding_bag` gives dense gradients. Time 500 batches first. If a
  step costs over 1.5x, use R1 8.5's fallbacks (larger batch with LR
  adjustment, or a sparse update of `ft_buckets`).

### 13.6 Runs and acceptance

Same file and budget as the current accepted recipe:

| Run | Flags | Purpose |
|---|---|---|
| K8 | `--king-buckets 8 --init-rebucket <accepted 4bkt.pt>` | variant |
| K4 | `--king-buckets 4 --init <accepted 4bkt.pt>` | same-recipe control |

Primary SPRT: K8 vs K4, STC then **LTC**. NPS cost shows up most at STC, and
knowledge gains most at LTC. Ship decision: K8 vs the accepted engine.

If K8 does not beat K4, keep 4 buckets. Do not retry with a pool larger than
the 1B cap: needing more data to win is a fail under the Design Target. One
retry is allowed at the end of the round, with the larger pool still inside
the cap, if the learning curves show the first attempt was limited by steps.

Tests: everything in R1 8.8, with king walks across every new boundary for
both colours.

---

## 14. Phase 8 – The Generation Loop

Once Phases 3–6 are done, each cycle is:

```
net k  --datagen v2-->  Gen-(k+1) data  --stats-->  mix  --train from net k-->  net k+1  --SPRT vs net k-->  promote / reject
```

Rules:

1. **Data comes from the current accepted net**, built into the datagen binary
   with `EVALFILE=` (no copy into `src/weights/` needed).
2. **The pool is capped at 1B unique positions** (Design Target), Lichess's
   ~300M included. That leaves room for roughly 700M self-play positions,
   about 7–14 generations of 50–100M. Below the cap, keep every generation, and
   down-weight older ones with `mix_data.py` per-source caps (their labels come
   from weaker nets). At the cap, a new generation **replaces** the oldest one
   (Gen-1 first) instead of being added.
3. **After the cap, improvement comes from label quality, not volume.** Each
   new generation is labelled by a stronger net, so swapping old data for new
   keeps improving the pool at a constant size. This is the steady state the
   loop aims for. Spending CPU on better labels (for example a higher node
   budget, rule 6) beats generating past the cap.
4. **Taper Lichess.** Once self-play passes about 150M, test a self-play-only
   file against the current mix at equal steps (one SPRT). If self-play-only
   wins, Lichess leaves the pool, and its ~300M slots go to self-play under
   the same 1B cap. Let the SPRT decide when.
5. **Fixed step budget per cycle**, raised only when the pool grows enough for
   rows to repeat less than about 4 times. At the cap, 1–2 epochs over 1B
   would take about 1.5–3 days at 8k positions/s. Keep runs at about a day
   by sampling the pool, not by making it smaller.
6. **Stop rule.** Two consecutive generations fail their SPRT: stop
   generating and spend the CPU on Phase 7, the deferred work, or a larger node
   budget (5000 → 10000) for the next generation.
7. **SPSA** every second accepted net (11.1).
8. Each cycle gets a results block. The `data_stats.py` summary of the new
   generation and the learning-curve verdict go in its Notes.

### Budget check (once per round, optional but recommended)

Every accepted net in R1 and Round 2 is a warm start from its parent. Each run
stays inside the pool, but the chain as a whole has seen far more samples than
any single run. To confirm that the **current** architecture is still trainable
to full strength from zero on at most 1B positions:

1. Train one net from scratch on the round's final pool (no `--init`), with the
   accepted architecture flags, cosine LR from 1e-3, and a sample budget large
   enough that the learning curves flatten. That is about 2–4B samples, or
   roughly 3–6 days at 4-bucket speed. Run it when the machine would otherwise
   be idle.
2. SPRT it against the accepted warm-start net.
3. Reading the result:
   - Equal or better: the architecture meets the Design Target, and the
     from-scratch recipe goes into the README as the reproducible path.
   - Clearly worse: the strength depends on the warm-start history. Record it.
     Before the next round adds structure, find out whether more epochs, a
     higher starting LR or warmup closes the gap.

This check costs no Elo by itself. It protects the property the
Schoenemann-style net was chosen for.

---

## 15. Deferred Work

Every row is judged against the Design Target first. "Fits" means it can be
expected to pay off with at most 1B unique positions, on the home CPU, without
breaking the warm-start chain.

| Idea | Fits 200M–1B? | Cost | Status / reopen when |
|---|---|---|---|
| Higher node budget in datagen (5000 → 10000) | **Yes.** Better labels at a fixed pool size is exactly rule 1 | throughput halves per doubling | in scope. Reopen at the stop rule, or once the pool reaches the cap (Phase 8 rule 3) |
| Wider L1 (1024 → 1536) | **Possibly.** Data per weight does not change (R1 Section 1), but capacity grows 1.5x with no new structure. Gains at this data size are expected to be small next to the NPS cost | NPS: each accumulator update and the output layer grow 1.5x. Training 1.5x per step. Exact warm start is possible (new neurons get zero output weights) | deferred. Reopen only if the Phase 3/7 learning curves say "limited by the architecture" **and** the paired NPS harness shows headroom |
| More king buckets than 8 | **Only with the factorizer**, and only while every bucket keeps about 1% of the pool | FT table and refreshes grow again | after Phase 7 passes, if its curves leave room |
| Many king buckets without a factorizer | **No.** Each bucket sees 1/N of the data, which is the data cost the factorizer exists to avoid | n/a | never (R1 Section 11) |
| Hidden layers (L1 → 16 → 32 → 1, pairwise activation) | **Out of scope by design, not by data size.** R1 Section 11 notes that small L2/L3 heads have helped 768-input engines at a few hundred million positions, so data alone does not rule them out. They are left out because they break the exact warm-start chain, need a new int8 and sparse-L2 inference path in `nnue_loader.h`, and move the net away from the Schoenemann-style design this plan protects | large engine and trainer rewrite; a from-scratch training run | revisit only as a deliberate decision at the start of a later round, judged by a from-scratch run inside the budget |
| Threat / attack inputs | **No.** The input space grows several-fold and data per weight drops: the data-hungry direction | large engine and trainer change, slower updates | out of scope under this Design Target |
| From-scratch training on the full pool | yes | about 3–6 days of CPU per run | now the Phase 8 **budget check**, once per round |
| GPU training | n/a | n/a | if a GPU becomes available, from-scratch runs get cheap. The Design Target stays: a GPU makes training faster, it does not raise the data budget |

---

## 16. Compute Budget

Planning numbers from R1 speeds (about 11k positions/s for the 1-bucket net,
assumed about 8k/s for 4 buckets). Replace them with Phase 1 and Phase 2
measurements.

| Item | Estimate |
|---|---|
| Phase 1 audit | under 1 h |
| One Phase 3/4 run at 480M samples | 15–20 h |
| Phase 3 (A, C, B) | 45–60 h |
| Phase 4 (L1, L5) | 30–40 h |
| One SPRT at STC, 3000 games (concurrency 6, laptop rate) | up to about 11 h; many stop earlier |
| Gen-3, 50–100M records | 1–2 days (Phase 1 gives the real rate) |
| Phase 7 runs (K8, K4) | 35–60 h |
| Budget check (from scratch, 2–4B samples) | 3–6 days, once per round |
| Pool at the 1B cap | about 68 GB on disk at 68 bytes per record. Lichess `train.bin` is already about 20 GB |

Data generation and training both saturate the CPU. Run them one after the
other, never at the same time (R1 3).

---

## 17. Testing Checklist Additions

R1 Section 13 applies to every phase. Round 2 adds:

- [ ] Training runs that are compared to each other had the same
      `--total-samples`, seed, LR curve and init.
- [ ] Validation MSE logged on `val.bin`, `val_gen2.bin` and `val_gen.bin`
      (pure-eval column when λ differs).
- [ ] No training file contains any record of a fixed hold-out set.
- [ ] The pool the run drew from holds at most 1B unique positions (Lichess +
      every generation), and the results log gives its size.
- [ ] The learning-curve CSV (`--val-every`) is saved next to the
      checkpoint, and its verdict (steps / data / architecture) is in the
      results log.
- [ ] Architecture changes: the Design Target rule they satisfy is named, and
      the Section 15 "Fits" reasoning still holds after the run.
- [ ] `data_stats.py` run on every new generation before training; numbers in
      the results log.
- [ ] SPRTs use `BOOK=tools/book_test.epd`; datagen uses
      `tools/book_datagen.epd` (from Gen-3 on).
- [ ] Datagen v2 with all flags off is byte-identical to v1.
- [ ] Phase 7: the warm-started 8-bucket net evaluates identically to its
      parent before training.
- [ ] SHA-256 of `data/`, `data_gen*/`, `checkpoints/` and
      `src/weights/` unchanged except for files a phase was meant to create.

---

## 18. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Elo from more training attributed to the data | Lichess-only control C (Phase 3), equal sample budgets |
| A share or λ "result" that is really a step-count difference | `--total-samples` + per-step cosine (Phase 2.3) |
| Exported epoch chosen by fit to the wrong distribution | `--select-by last`, multi-distribution validation, the best-vs-last SPRT on D1 |
| Self-play feedback loop: the net learns its own blind spots | Lichess stays in the mix until an SPRT says otherwise; stop rule; λ < 1 keeps the outcome signal in the target |
| Test openings leak into the training data | book split (Phase 0.4) |
| Labels deflated by the fifty-move clock | `--max-fifty` (Phase 6) |
| Mixer runs out of RAM on 100M+ row files | chunked writer (Phase 2.4) |
| 8 buckets cost more NPS than they gain | paired NPS harness, LTC confirmation, K4 control |
| Overwriting data or nets | new directories only, `O_EXCL` in datagen, mixer refuses existing outputs, SHA-256 checks |
| Drift toward a data-hungry design (capacity added because "more data will fix it") | Design Target rules; the Fits column in Section 15; learning curves must show the architecture is the limit before capacity is added |
| Net strength depends on the warm-start history and cannot be reproduced inside the budget | Phase 8 budget check (from-scratch run) once per round |
| Pool outgrows the disk or the budget | 1B cap, with replacement of the oldest generation (Phase 8 rule 2) |

---

## 19. Open Questions

1. **Where did D1's gain come from?** Phase 3's control C answers it. The
   answer decides how much CPU goes to datagen versus architecture.
2. **What is GOOB's cp-to-outcome scale?** Phase 1's K. It decides how much λ
   matters and whether label scale is a problem between Lichess and self-play.
3. **Best-by-validation or last?** The D1 best-vs-last SPRT (8.2).
4. **When to drop Lichess?** Phase 8 rule 4.
5. **Static eval in check: skip it, or train on it?** Phase 5.2 first; the data
   route only if that fails.
6. **Is 5000 nodes still the right budget** as the net gets stronger? Revisit
   at the stop rule or at the pool cap, not before.
7. **At what pool size does this architecture saturate?** The learning curves
   from Phases 3, 4 and 7 answer it. If it saturates well below 1B, the
   remaining budget goes into label quality (Design Target, rule 1). If it is still improving
   at the cap, the budget check shows whether that holds from scratch too.

---

## 20. Housekeeping Found During the Review

Small items, not phases. Each is a separate small commit.

- Two compiled binaries are tracked in git: `tools/datagen` (6.6 MB, added in
  `b9b1111`) and `tools/test_incremental`. Untrack both
  (`git rm --cached tools/datagen tools/test_incremental`) and add them to
  `.gitignore`. They are built per machine and per net.
- `phase4_commands.md` passes `GAMES=10000` to `run_sprt.sh`, which ignores it.
  The script reads `ROUNDS` (default 1500 rounds = 3000 games). Any SPRT run
  that way was capped at 3000 games. Fix the guide, and check whether a
  "passed" Phase 4 result was really a 3000-game stop.
- R1's progress block (A8): restore the Phase 1 line if Phase 1 was accepted.
  Update the Phase 2 line from "Ready for execution" to the D1 result.
- The root-level guides (`PHASE2_DATAGEN_GUIDE.md`, `PHASE3_KING_MIRROR_GUIDE.md`,
  `phase4_commands.md`) could move to `documentations/` next to the plans.
