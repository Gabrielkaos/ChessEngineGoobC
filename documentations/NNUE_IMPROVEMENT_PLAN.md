# NNUE Improvement Plan

Plan for improving GOOB's network while keeping its two strengths: it is
**fast to train** and it is **strong on little data** (the current net was
trained on roughly 300M Lichess positions).

Nothing in this document has been implemented or measured yet. Every phase
ends with its own SPRT against the previous accepted engine, as `AGENTS.md`
requires; only a passed SPRT counts as an improvement.

This is the second revision. It follows a static review of the first
revision against the engine source, the trainer scripts and the Stockfish
reference implementation in `Stockfish-sf_19/`. The review's findings are
folded into the phases below; the list that follows says what changed and
why.

---

## Changes from the previous revision

- **Data generation is now a phase, not a "what not to do".** The previous
  revision rejected additional data sources in one sentence. Engine-generated
  self-play data with game results is the single largest NNUE improvement in
  the history of comparable engines, and it is the only route to a WDL
  signal; the `--wdl-lambda` flag is currently dead because Lichess records
  carry no results. It is Phase 2.
- **Every architecture phase now has a control.** Section 7.1 of the old plan
  warned that extra fine-tuning epochs can gain Elo on their own, then Phases
  1 and 2 ignored the warning. Each architecture change is now compared
  against a same-recipe net with the architecture flag off. The cheapest such
  control, a plain fine-tune of the current net, is Phase 1 and may be a gain
  by itself.
- **Phase 3 trainer tweaks are reorganized around what the SPRT harness can
  resolve.** `tools/sprt/run_sprt.sh` stops at 3000 games with bounds
  [0, 5]. Effects of a few Elo end inconclusive there. Same-loss changes are
  gated on validation MSE and the recipe gets one combined SPRT.
- **The "data per weight" reasoning is corrected.** How often a feature
  transformer weight is updated depends on how often its input feature is
  active, not on the hidden width. This argument is valid against unfactorized
  king buckets and invalid against a wider L1. Section 11 is rewritten.
- **Mirroring is no longer described as "2x data".** Its real mechanism is one
  bit of king-location information plus a hard mirror-symmetry constraint. A
  flip-augmentation control separates the two.
- **AdamW decay moves into the king-bucket phase.** It is the shrinkage prior
  that makes the factorizer's "buckets learn only the difference" argument
  hold under Adam.
- **A silent failure mode is closed.** `main.c` ignores the return value of
  `nnue_init()`, and `nnue_eval()` returns 0 when no net is loaded. The format
  tag's "refuse a mismatched net" rule would turn that into an engine that
  plays with a zero evaluation. A failed embedded-net load becomes fatal.
- **Long time control confirmation is required for net changes.** Trading
  speed for knowledge looks different at 6+0.06 and 12+0.1.
- **Phase 0 verifies two assumptions the old plan took for granted.** The
  float checkpoint `tools/nnue_project/checkpoints/nnue.pt` is not present in
  this checkout, and the two data-preparation scripts apply different filters
  (`prepare_data.py` is **not** quiet-only).
- **A search re-tune follows any accepted net.** The SPSA tuner in
  `tools/spsa/` tuned the pruning margins against the current net's error
  profile.

---

## Table of Contents

1. [Current State](#1-current-state)
2. [Goals and Constraints](#2-goals-and-constraints)
3. [Strategy](#3-strategy)
4. [Phase 0 – Baseline, Safety and Verification](#4-phase-0--baseline-safety-and-verification)
5. [Phase 1 – Control Fine-Tune](#5-phase-1--control-fine-tune)
6. [Phase 2 – Native Data Generation and Self-Play Data](#6-phase-2--native-data-generation-and-self-play-data)
7. [Phase 3 – Horizontal King Mirroring](#7-phase-3--horizontal-king-mirroring)
8. [Phase 4 – Factorized King Buckets + Accumulator Cache](#8-phase-4--factorized-king-buckets--accumulator-cache)
9. [Phase 5 – Search Re-Tune After a Net Change](#9-phase-5--search-re-tune-after-a-net-change)
10. [Phase 6 – Trainer Recipe](#10-phase-6--trainer-recipe)
11. [Deferred Architecture Work](#11-deferred-architecture-work)
12. [Net File Format Versioning](#12-net-file-format-versioning)
13. [Testing Checklist (every phase)](#13-testing-checklist-every-phase)
14. [Results Log Template](#14-results-log-template)
15. [Risks and Mitigations](#15-risks-and-mitigations)
16. [Open Questions](#16-open-questions)

---

## 1. Current State

Verified from the code as of commit `8f10618`.

### Network

| Item | Value |
|---|---|
| Topology | `(768 -> 1024)x2 -> 1x8` |
| Inputs | `colour * 384 + piece_type * 64 + square`, perspective-relative (`sq ^ 56` and colours swapped for Black) |
| King info | none: kings are ordinary features, so a king move is a normal 1-add/1-sub update |
| Feature transformer | one 768 x 1024 matrix shared by both perspectives |
| Activation | SCReLU, `clamp(x, 0, 1)^2` |
| Output | 8 buckets by piece count, `clamp((pieces - 2) / 4, 0, 7)` |
| Quantization | QA = 255, QB = 64, eval = output x 400 |
| File | `src/weights/quantised.bin`, 1,607,744 bytes (int16 FT weights, FT bias, out weights, out bias, 48 bytes `"bullet"` padding) |

### Trainer (`tools/nnue_project/scripts/`)

| Item | Value |
|---|---|
| Framework | PyTorch, `nn.EmbeddingBag` for the feature transformer (CPU; no NVIDIA GPU on this machine) |
| Optimizer | Adam, lr 1e-3, cosine annealing per epoch to 1e-5 |
| Loss | MSE between `sigmoid(out)` and target |
| Target | `sigmoid(cp_stm / 400)`, blended with the game result when the record has one (`--wdl-lambda 0.75`); the Lichess data has no results, so training today is pure eval regression |
| Batch size | 8192 |
| Weight clipping | every parameter clamped to +/-1.98 after each step |
| Data | Lichess eval DB, deepest PV1 eval per position, 68-byte records; see the filter note below |

**Two data scripts, two filter sets.** `prepare_data_jsonl.py` keeps quiet
positions only (no check, best move not a capture or promotion, best move
does not give check), depth >= 20, |cp| <= 3000, no mates.
`prepare_data.py` (Hugging Face streaming) applies depth, mate and |cp|
filters only and has **no quiet filter**. Which script produced the training
set, and therefore what distribution the net saw, is confirmed in Phase 0.

### Engine (`src/nnue_loader.h`)

- Lazy accumulator stack `S_SEARCH_THREAD.nnue_accumulators[MAXDEPTH]`, each
  with `accumulation[2][1024]` and `computed[2]`.
- `nnue_update_accumulators_to_ply()` walks back to the nearest ply where
  **both** perspectives are computed and replays `dirtyPieces[]` forward,
  updating both perspectives together. A full refresh happens only when no
  ancestor is within `NNUE_REFRESH_THRESHOLD` (32) plies.
- `DirtyPiece` already has a `king_moved[COLOR_NB]` field (set in
  `makemove.c`), currently unused by the NNUE code.
- The loader checks the file size against `NNUE_TOTAL_SHORTS` and has a
  slow exact kernel fallback when output weights exceed +/-128.
- `nnue_init()` returns 0 on failure. `main.c` line 61 ignores that return
  value, and `nnue_eval()` returns 0 when `nnue_loaded` is 0. An engine whose
  embedded net fails to load therefore plays with a zero evaluation and
  prints only an `info string`.
- The `eval` UCI command prints the evaluation and the evaluation of the
  colour-mirrored position (`MirrorBoard`, a vertical flip). There is no
  horizontal-mirror check today.

### How the search uses the evaluation

The net is the only evaluation; there is no hand-crafted fallback.

- `AlphaBeta` computes the static eval when it is not in the TT, **including
  when the side to move is in check** (`search.c`, the `rawEval` line before
  `staticEval`). In check the raw value is stored uncorrected and still feeds
  the TT eval field and the hindsight terms.
- Quiescence stands pat on the net's eval in positions where captures are
  available.
- The static eval passes through correction history (`correction.c`) and
  fifty-move deflation (`adjustEvalOnFmr`) before pruning decisions.
- The pruning margins in `tune.h` were SPSA-tuned (`tools/spsa/`) against the
  current net's error profile.

### Known gaps between training data and what the search evaluates

| Gap | Effect |
|---|---|
| Training is quiet-only (if the JSONL script was used); the search evaluates in-check and capture-pending positions | Out-of-distribution evals at every in-check node and every stand-pat |
| Positions with \|cp\| > 3000 and all mate scores are dropped | The net never sees overwhelmingly won positions; with no fallback eval, conversion of won games depends on the net extrapolating monotonically there |
| Lichess positions come from human games and user-requested analysis | Opening and middlegame distribution differs from engine games started from `tools/book.epd` |
| Lichess labels come from many Stockfish versions, including the switch to normalized evals | Label scale is mixed across the dataset |
| No game results | No WDL signal; `--wdl-lambda` has no effect |

### Why the current net works well on little data

1. The labels are very high quality (deep Stockfish evals).
2. The feature set is small (768 inputs) and every input weight receives a
   gradient whenever its feature is active. Features are not split across
   king squares, so no feature's activations are diluted across parameters.

Property 2, stated precisely: **do not split a feature's activations across
parameters without a shared component.** This argues against unfactorized
king buckets. It does **not** argue against a wider hidden layer: weight
`ft[f][j]` is updated whenever feature `f` is active, for every `j`, so L1
width does not change how many examples each weight sees. The costs of a
wider L1 are inference speed and training time (Section 11).

---

## 2. Goals and Constraints

### Goals

- More Elo, measured by SPRT against the previous accepted engine, confirmed
  at a longer time control for net changes.
- First from the existing Lichess data; then from engine-generated data added
  to it (Phase 2).
- Training time per epoch about the same as today (at most ~1.5x).
- Every new net should be able to **warm-start** from the previous one, so
  improvements need fewer new training rows.
- Inference speed (NPS) within a few percent of today.

### Hard constraints

- **NNUE protection (`AGENTS.md`).** Never overwrite, regenerate or delete
  existing datasets, checkpoints or nets. Every experiment writes into its
  own new directory. `src/weights/quantised.bin` is never replaced as part
  of an experiment. Data generation writes only into new directories.
- One logical change per phase, each with its own SPRT.
- **Every architecture change has a control:** a net trained with the same
  init, data, epochs and seed and the architecture flag off. The SPRT that
  decides whether to keep the engine-side complexity is variant vs control.
- **Measurability:** a phase is only proposed with an experiment that can
  resolve its expected effect size with the harness in `tools/sprt/`
  (bounds [0, 5], 3000-game cap). Effects expected under 5 Elo are gated on
  validation loss where the loss is comparable, and pooled into one SPRT
  otherwise.
- `src/README.md`, `tools/README.md` and `tools/nnue_project/README.md`
  must be updated whenever `src/` or `tools/` changes.
- Correctness before Elo: incremental-vs-fresh accumulator checks and
  engine-vs-`check_net.py` bit-exact checks must pass before any match.

---

## 3. Strategy

Ordered by expected Elo, quality of the evidence behind the technique,
implementation and training cost, and regression risk.

| Phase | Change | Expected effect | Evidence | Engine change | Trainer change | Decided by |
|---|---|---|---|---|---|---|
| 0 | Baseline, verification, fatal net-load failure | none | n/a | one-line fix in `main.c` | `--init-from-bin` fallback if the float checkpoint is gone | checks |
| 1 | Control fine-tune of the current net | small to moderate if the baseline is undertrained | strong | none | none | SPRT vs base |
| 2 | Native data generation; self-play data with results mixed into training | largest available | strong across comparable engines; magnitude for GOOB unknown | none (new tool in `tools/`) | none; mixing already supported | SPRT vs Phase 1 winner, LTC confirm |
| 3 | Horizontal king mirroring | small positive | moderate | small | small | SPRT vs same-recipe control, LTC confirm |
| 4 | Factorized king buckets (4) + accumulator cache, with decay on bucket deltas | moderate positive | strong technique | medium | medium | SPRT vs same-recipe control, LTC confirm |
| 5 | SPSA re-tune of pruning margins against the accepted net | small to moderate | strong | none (constants) | none | SPRT tuned vs untuned |
| 6 | Trainer recipe: batch 16384 with LR adjustment, warmup, power loss | small | weak | none | small | val MSE gates, one combined SPRT |

### Dependencies and scheduling

- Phase 0 first. Phase 1 next: it is cheap, it may win by itself, and it is
  the control recipe every later phase reuses.
- Phase 2's tool can be written while Phase 1 trains. Data generation and
  training both saturate the CPU, so they run one after the other, not at
  the same time.
- Phases 3 and 4 are engine work and can be developed while data generates.
  Their training runs follow the data phase so that variant and control see
  the same training set.
- Phase 5 runs after any accepted net, including Phase 1's.
- Phase 6 flags can be added at any time. Their evaluation is folded into the
  training runs of Phases 2 to 4 where the gate is validation MSE.

### Wall-clock expectations

From the per-batch timing recorded in `tools/nnue_project/scripts/claude-suggest.txt`
(about 730 ms per 8192-position batch on this CPU). These are planning
numbers; Phase 0 measures the real figure.

| Run | Approx. CPU time |
|---|---|
| One epoch over 300M positions | 7 to 8 hours |
| Six-epoch fine-tune (Phases 1, 3 variant, 3 control) | about 45 hours each |
| Phase 4 at 1.2 to 1.5x per step | 55 to 70 hours |
| Data generation at 5000 nodes per move | to be measured; expect tens of millions of positions per day on this machine |

---

## 4. Phase 0 – Baseline, Safety and Verification

Do this once, before any code change.

### 4.1 Freeze the baseline

1. Record the SHA-256 of the current net, and of the float checkpoint if it
   exists:
   ```bash
   sha256sum src/weights/quantised.bin
   sha256sum tools/nnue_project/checkpoints/nnue.pt   # see 4.2 if missing
   ```
2. Build and keep a baseline binary outside the build output:
   ```bash
   make -C src native -j8
   cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-nnue-base
   ```
3. Run the sanity bench and store the output:
   ```bash
   python3 tools/sprt/bench.py tools/sprt/bin/GOOB-nnue-base 12
   ```

### 4.2 Confirm the float checkpoint exists

`tools/nnue_project/checkpoints/` is gitignored and is not present in this
checkout. Every warm start in this plan uses `--init <checkpoint>`. Locate the
checkpoint the current net was exported from and record its path and hash.

If it cannot be found, add `--init-from-bin <quantised.bin>` to `train.py`:
dequantize `ft_w / 255`, `ft_b / 255`, `out_w / 64`, `out_b / (255 * 64)`
into the model and start with a fresh optimizer. This loses the sub-quantum
precision of the float weights but is a sound warm start. Document the flag
in `tools/nnue_project/README.md`.

### 4.3 Confirm which script built the training set

Sample 100k records from the training file and count positions with the side
to move in check (python-chess). If the count is non-zero, the set came from
`prepare_data.py` and is not quiet-only; if it is zero, from
`prepare_data_jsonl.py`. Record the answer. It decides whether the
"quiet-only" row of the gap table in Section 1 applies and it is needed to
interpret Phase 2's mix experiments.

### 4.4 Directory layout for experiments

All new files go under new directories; nothing existing is touched.

```
tools/nnue_project/checkpoints/
  nnue.pt                    # existing, read-only from now on
  exp_control/               # Phase 1 outputs (nnue.pt, last.pt, quantised.bin)
  exp_datagen/               # Phase 2 outputs, one subdirectory per mix ratio
  exp_mirror/                # Phase 3 outputs: variant/, control/, flipaug/
  exp_kb4/                   # Phase 4 outputs: variant/, control/
  exp_recipe/                # Phase 6 outputs, one subdirectory per variant
tools/nnue_project/data_gen/ # Phase 2 generated records, one file per process
```

Always pass `--out-best`, `--out-last` and `--out` explicitly so no
default path (`../checkpoints/nnue.pt`, `../checkpoints/last.pt`,
`../checkpoints/quantised.bin`) is ever written by accident.

### 4.5 Build an engine with a different net without touching `src/weights/`

The makefile takes `EVALFILE` as a variable (`src/makefile:111`):

```bash
make -C src native -j8 EVALFILE=$PWD/tools/nnue_project/checkpoints/exp_mirror/variant/quantised.bin
cp src/bin/linux/GOOB-2.2-BETA-native tools/sprt/bin/GOOB-mirror
```

### 4.6 Confirm the reference checker matches the engine

Before changing anything, confirm `check_net.py` reproduces the engine's
evals for the current net. It becomes the reference for every later phase.

```bash
cd tools/nnue_project/scripts
python check_net.py --net ../../../src/weights/quantised.bin --checkpoint ../checkpoints/nnue.pt --pipeline
```

Compare against the engine's `eval` command on the same FENs.

### 4.7 Training-speed baseline

Time 500 batches of the current trainer on the real dataset with the
current settings and write down positions/second. Every phase is compared
against this number.

### 4.8 Make a failed embedded-net load fatal

Engine change, one function. In `main.c`, if `nnue_init(NULL)` returns 0,
print the failure and exit with a non-zero status. The `EvalFile` path in
`uci.c` stays non-fatal as it is today (a previously loaded net remains
active). Without this, Section 12's "refuse a mismatched net" rule produces
an engine that silently evaluates every position as 0.

Update `src/README.md` (startup behaviour). Perft and bench are unaffected.

### 4.9 Long time control baseline

Net changes are confirmed at a longer control after the STC SPRT. The
harness takes the time control and concurrency as arguments:

```bash
tools/sprt/run_sprt.sh <new> <base> <name>-ltc 12+0.1 4
```

Keep the same book, hash and thread settings as the STC run.

---

## 5. Phase 1 – Control Fine-Tune

### 5.1 Idea

Fine-tune the current net on the current data with the recipe every later
architecture run will use. This does two things:

- It tests whether the baseline is undertrained. If it is, this is the
  cheapest Elo in the plan and needs no engine change.
- It defines the **control recipe**: six epochs, lr 5e-4, cosine to 1e-5,
  seed 0, same data, same batch size. Phases 3 and 4 train their variants and
  controls with exactly this recipe so that the only difference is the
  architecture flag.

### 5.2 Run

```bash
cd tools/nnue_project/scripts
python train.py --train <train.bin> --val <val.bin> \
    --init ../checkpoints/nnue.pt --lr 5e-4 --epochs 6 --seed 0 \
    --out-best ../checkpoints/exp_control/nnue.pt \
    --out-last ../checkpoints/exp_control/last.pt
python export_weights.py --checkpoint ../checkpoints/exp_control/nnue.pt \
    --out ../checkpoints/exp_control/quantised.bin
python check_net.py --net ../checkpoints/exp_control/quantised.bin \
    --checkpoint ../checkpoints/exp_control/nnue.pt --pipeline
```

### 5.3 Acceptance

SPRT `GOOB-control` vs `GOOB-nnue-base`, STC, then LTC confirmation if the
STC passes. If accepted, the control net becomes the accepted engine and the
baseline for every later SPRT. If not accepted, the recipe is still the
control recipe; only the "baseline is undertrained" hypothesis is dropped.

Either way, note the validation MSE of the control net: it is the reference
for the same-loss gates in Phase 6.

---

## 6. Phase 2 – Native Data Generation and Self-Play Data

### 6.1 Why this is a phase

Comparable engines that train their own nets (bullet-trained engines in the
same architecture family) report the move from analysis-database labels to
their own self-play data as the largest single gain in their net history.
The reasons apply to GOOB:

- **Distribution match.** Self-play positions are the positions GOOB's search
  visits, from the openings GOOB is tested with.
- **A WDL signal.** Game results are the only direct measurement of "how do
  games actually end from here". The trainer already blends them
  (`--wdl-lambda`); today no record has one.
- **Consistent label scale.** One engine, one scale, instead of a mix of
  Stockfish versions.
- **Coverage of the gaps in Section 1.** Lopsided positions and in-check
  positions can be kept or dropped by choice, not by a dataset's filters.

The honest caveats: self-play labels at a few thousand nodes are shallower
than depth-20 Stockfish evals, so this trades label quality for distribution
and WDL. Whether the trade wins for GOOB, and at what mix ratio, is exactly
what the SPRTs in this phase measure. Nothing here replaces the Lichess set;
it is added to it.

### 6.2 Why a native tool

`tools/selfplay_nnue.py` already produces records with results, but it drives
one engine process per worker through UCI and python-chess and has produced
about 250k positions (`tools/nnue_project/data_selfplay/`). Hundreds of
millions are needed. The standard answer is a C generator that calls the
search directly.

### 6.3 Design of `tools/datagen.c`

A standalone C program linked against the engine objects, in the pattern of
`tools/test_incremental.c` (which includes the `src/` headers and calls
`AllInit`, `ParseFEN`, `makeMove`, `nnue_init`). Search setup follows the
`bench` path in `src/main.c`, which already runs searches outside the UCI
loop. One process per core, each with its own seed and output file; no
threading inside the tool.

Parameters: nodes per move (default 5000), number of games or positions,
output file, seed, opening source, adjudication thresholds.

Game loop:

1. **Opening.** Start from a line of `tools/book.epd` and play a few random
   legal plies (default 4), or play N random plies from the start position
   (default 8). Both are parameters. Random plies give the diversity that
   makes self-play data useful; the book keeps the openings close to what the
   SPRT harness uses.
2. **Play.** Each move is a node-limited search. The move played is the
   search's best move. The search score is converted from side-to-move to
   White's point of view and stored with the position.
3. **Record filter.** Skip positions where the side to move is in check and
   positions where the best move is a capture or promotion (the same quiet
   definition as `prepare_data_jsonl.py`, so the two sources agree). Do
   **not** drop lopsided positions: the record format saturates at +/-3000
   and these are the positions the gap table in Section 1 says the net never
   sees. Keep mate scores with the mate flag set; they are a small fraction
   and the result confirms them.
4. **Adjudication.** Resign when |score| exceeds a threshold (default 1500)
   for four consecutive plies; declare a draw when |score| stays under a
   small threshold (default 10) for eight consecutive plies after move 40,
   which mirrors the draw rule in `run_sprt.sh`; stop at a maximum ply
   (default 400). Game end by rule (mate, stalemate, repetition, fifty-move,
   insufficient material) uses the engine's own detection.
5. **Result.** Buffer the game's records, then write them with the result
   bits set (`fen_utils.py` flag bits 1-2: 1 = White win, 2 = draw,
   3 = Black win).

Output is the 68-byte record of `fen_utils.py`, so `nnue_dataset.py`,
`check_net.py --pipeline` and the shuffle step in `clear_cache.py` work
unchanged.

**NNUE protection.** The tool writes only to the output path it is given,
creates the file if it does not exist and refuses to open an existing file.
Never point it at `data_selfplay/`, `data/` or `checkpoints/`.

Document the tool in `tools/README.md` (build line, parameters, record
semantics) when it is added.

### 6.4 Throughput

Measure before planning data volume: run one process for ten minutes and
record positions per second. A rough expectation for an engine of GOOB's
speed at 5000 nodes per move is on the order of 100 to 200 recorded positions
per second per core. If that holds, a day on this machine yields tens of
millions of positions, and a first useful set (50M to 100M) takes a few days.

### 6.5 Training with mixed data

Records from both sources are concatenated and shuffled into one training
file under `exp_datagen/`. The collate function already applies the WDL blend
per record: Lichess rows have no result and train on the pure eval target;
generated rows blend with `--wdl-lambda` (default 0.75).

Mix experiments, each a fine-tune from the Phase 1 net with the control
recipe, each into its own subdirectory:

| Run | Training set | Purpose |
|---|---|---|
| D1 | Lichess + generated, generated share about 25% | first test of the direction |
| D2 | Lichess + generated, about 50% | mix ratio |
| D3 | D1's set with `--wdl-lambda 0.5` | weight of the result signal |

Hold the validation set fixed across runs so validation MSE stays comparable;
use the Lichess validation set, and additionally report MSE on a held-out
generated set so both distributions are watched.

### 6.6 Acceptance

SPRT each run against the accepted engine (the Phase 1 net if it was
accepted, else the baseline), STC then LTC. Record the mix ratio, lambda,
nodes per move, opening source and generated-set size in the results log.
Keep the best; carry its training set forward as the data every later phase
trains on.

### 6.7 Tests for Phase 2

- `check_net.py --pipeline` on records from the generated file: features,
  side to move, bucket and target agree with the engine's conventions, and
  the implied centipawns match the stored score.
- Result distribution: W/D/L counts, mean game length, share of adjudicated
  games. A draw share far outside what `run_sprt.sh` games show means the
  adjudication thresholds are wrong.
- No record with the side to move in check (python-chess over a sample).
- Determinism: the same seed and parameters produce the same file.

---

## 7. Phase 3 – Horizontal King Mirroring

### 7.1 Idea

For each perspective, if that side's own king is on files e–h, flip every
square of that perspective horizontally (`sq ^ 7`). The network then only
ever sees positions where "my king is on files a–d".

What this does, precisely:

- **One bit of king-location information.** The feature "pawn on b2" becomes
  "pawn on the b-file relative to my king's half". Kingside and queenside
  castled structures pool into the same features. This is a weak form of the
  king-relative encoding that king buckets (Phase 4) make explicit.
- **A hard symmetry constraint.** The net's evaluation of a position and its
  horizontal mirror are identical by construction. Chess is mirror-symmetric
  except for castling rights, which the features do not encode anyway, so
  nothing legal is lost. The constraint removes a degree of freedom the net
  would otherwise spend learning both halves separately; that is the real
  content of the "2x data" intuition, and it is a regularization effect, not
  a doubling of the dataset. The number of feature activations per position
  is unchanged.
- Parameter count and input count stay the same (768 inputs, 1024 hidden),
  so training speed is unchanged.

Expected size: small positive. In community experience with 768-input nets
this step is clearly smaller than king buckets. The two mechanisms are
separated by the flip-augmentation control in 7.4.

### 7.2 Exact feature definition

For perspective `p` (WHITE or BLACK):

```
ksq      = square of p's own king
rel(sq)  = sq            if p == WHITE
           sq ^ 56       if p == BLACK
mirror_p = file(rel(ksq)) >= 4          # files e..h  (file = sq & 7)
                                        # note: rel() does not change the file,
                                        # so this is just file(ksq) >= 4
feat(piece, sq) = rel_colour(piece, p) * 384
                + piece_type(piece) * 64
                + (rel(sq) ^ (mirror_p ? 7 : 0))
```

Each perspective has its own `mirror_p`, so in one position the White and
Black accumulators can be mirrored differently.

Consequence: in the start position both kings are on e-files, so both
perspectives are mirrored. O-O keeps the king on e–h (no change).
O-O-O moves the king to the c-file, so that perspective flips.

### 7.3 Trainer changes

**`nnue_dataset.py` – `nnue_collate`:**

1. Find the king squares per sample from the 64-byte board (code 6 = white
   king, 12 = black king):
   ```python
   b = records[:, :64]
   wk = np.argmax(b == 6, axis=1)      # (B,)
   bk = np.argmax(b == 12, axis=1)
   w_mirror = (wk & 7) >= 4            # (B,) bool
   b_mirror = (bk & 7) >= 4
   ```
2. After building `white_idx` / `black_idx` (as today), apply the flip per
   feature. Each feature knows its sample via `nz >> 6`. Because the colour
   and piece-type offsets are multiples of 64, XOR-ing 7 on the full index
   only changes the file:
   ```python
   sample = nz >> 6
   white_idx ^= np.where(w_mirror[sample], 7, 0)
   black_idx ^= np.where(b_mirror[sample], 7, 0)
   ```
3. Put this behind a `mirror` argument (`nnue_collate(batch, wdl_lambda, mirror=False)`)
   so the old behaviour stays available and old checkpoints can still be
   evaluated.
4. **Flip-augmentation control.** Add a `flip_augment` argument: with
   probability 0.5 per sample, XOR 7 into **both** perspectives' indices for
   that sample regardless of king position. The label is unchanged because the
   features encode neither castling rights nor en passant. This trains the
   current architecture toward mirror symmetry with no engine change, and is
   the control that isolates the king-half bit (7.4).

**`model.py`:** no shape change. Add an `arch` dict saved in every
checkpoint, for example `{"mirror": True, "king_buckets": 1}`, so later
tools know how to build features for it.

**`train.py`:**

- New flags `--mirror` and `--flip-augment` (mutually exclusive). `--mirror`
  passes `mirror=True` into the collate function and stores `arch` in the
  checkpoint.
- `--init` already loads weights with a fresh optimizer; the shapes match,
  so a mirrored run can start from the current `nnue.pt`.
- Refuse to `--resume` a checkpoint whose `arch` differs from the flags.

**`export_weights.py`:** write the format tag described in
[section 12](#12-net-file-format-versioning) into the 48 trailing bytes
(the file size stays exactly 1,607,744 bytes, so the tag is the only way the
engine can tell a mirrored net from a legacy one).

**`check_net.py`:** read the tag, apply the same mirroring in
`feature_indices()`, add a `hmirror_fen()` helper (horizontal flip, castling
rights dropped) next to the existing `mirror_fen()`, and add horizontally
mirrored FENs to the default position set including positions with kings on
the d- and e-files.

**`test_net.py`, `scale_test.py`:** use the checkpoint's `arch` to build
features.

### 7.4 Warm start and runs

The current net's weight for "piece X on square S" becomes the mirrored
net's weight for "piece X on square S, king on a–d". When **both** kings are
on a–d the new net starts out identical to the old one. When a king is on
e–h, that perspective's accumulator is the old net's view of the mirrored
position, combined by the old output layer with the other perspective's
unmirrored view. Since chess is nearly symmetric, this is still a strong
start, but it is not an exact equivalence, and the first epoch's validation
MSE will jump for that half of the data. Watch the trend; judge by SPRT.

All runs use the control recipe from Phase 1 (six epochs, lr 5e-4, seed 0)
on the training set carried forward from Phase 2, into `exp_mirror/`:

| Run | Flags | Purpose |
|---|---|---|
| M (variant) | `--mirror --init <phase1 or phase2 net> --lr 5e-4 --epochs 6 --seed 0` | the mirrored net |
| C (control) | same, without `--mirror` | same recipe, architecture flag off |
| F (optional) | same, with `--flip-augment` instead of `--mirror` | symmetry regularization alone |

```bash
cd tools/nnue_project/scripts
python train.py --train <train.bin> --val <val.bin> --mirror \
    --init <accepted.pt> --lr 5e-4 --epochs 6 --seed 0 \
    --out-best ../checkpoints/exp_mirror/variant/nnue.pt \
    --out-last ../checkpoints/exp_mirror/variant/last.pt
python export_weights.py --checkpoint ../checkpoints/exp_mirror/variant/nnue.pt \
    --out ../checkpoints/exp_mirror/variant/quantised.bin
python check_net.py --net ../checkpoints/exp_mirror/variant/quantised.bin \
    --checkpoint ../checkpoints/exp_mirror/variant/nnue.pt --pipeline
```

If CPU time is short, run M first and SPRT it against the accepted engine;
that answers "should we ship it". Run C answers "is the engine-side
complexity earning its keep", which is the question before merging the
engine change.

### 7.5 Engine changes (`src/nnue_loader.h`, `src/board.h`, `src/makemove.c`)

1. **Feature index.** `nnue_feature_index(us, piece, sq)` gets the
   perspective's mirror state:
   ```c
   static inline size_t nnue_feature_index(int us, int piece, int sq, int flip) {
       int s = ((us == WHITE) ? sq : (sq ^ 56)) ^ flip;   /* flip is 0 or 7 */
       return (size_t)(s_piece_offset[us][piece] + s);
   }
   ```
   `nnue_ft_row()` passes it through. `flip` for a perspective is
   `((file of own king) >= 4) ? 7 : 0`.

2. **Store the flip in the accumulator.** Add `uint8_t flip[COLOR_NB]` to
   `NNUE_Accumulator` (`board.h`) so every computed accumulator remembers
   how it was built. Incremental steps use the accumulator's own flip.

3. **Mark refreshes in `DirtyPiece`.** Add `uint8_t refresh[COLOR_NB]`.
   In `makeMove()` (where `king_moved[side]` is already set), set
   `refresh[side] = 1` when the king's from-file and to-file are on
   different halves (`(from & 7) >= 4 != (to & 7) >= 4`). This covers
   O-O-O, king captures across the line and ordinary king moves across the
   d/e line. Clear it in both places `DirtyPiece` is reset (the `makeMove`
   and `makeNullMove` reset blocks in `makemove.c`).

4. **Per-perspective lazy update.** Rewrite
   `nnue_update_accumulators_to_ply()` to handle each perspective
   independently (the `computed[]` flags are already per perspective):
   ```
   for each perspective p:
       if acc[target].computed[p]: continue
       walk back from target:
           if dirtyPieces[ply].refresh[p] is set at any ply in (ancestor, target]:
               refresh p at target from the current board   # only p
               continue to next perspective
           stop at the first ply with computed[p]  -> ancestor
       if no ancestor or distance > NNUE_REFRESH_THRESHOLD: refresh p at target
       else: replay dirtyPieces forward for p only
   ```
   The refresh is done from the board at `target` (the only board we have),
   which is why we look for a refresh flag *before* replaying.
   Keep a fast path that updates both perspectives together when neither
   needs a refresh (today's `nnue_update_accumulator_step_both`), so the
   common case is not slower.

5. **Refresh functions.** `nnue_refresh_perspective()` and
   `nnue_refresh_both()` compute each perspective's flip from the board's
   king squares and store it in the accumulator.

6. **No-search eval path** (`nnue_eval` with `pos->search == NULL`): uses
   the refresh functions, so it is covered by step 5.

7. **Loader.** Read the format tag (section 12); refuse to load a net whose
   tag does not match the compiled architecture, with a clear `info string`
   message. With 4.8 in place, a refused embedded net stops the engine
   instead of evaluating garbage.

8. **Refresh counter.** Count perspective refreshes caused by mirroring per
   1000 evals in `trace.c`, **reported separately for middlegame and endgame
   positions** (by piece count). Crossings are rare while kings are castled
   and frequent once kings walk in endgames; the endgame refreshes are cheap
   because few pieces remain, but the counter should show that, not assume
   it.

### 7.6 Tests for Phase 3

- `tools/test_incremental.c`: random games compare incremental vs fresh
  accumulators. Add forced sequences: king walks d1–e1–d1, O-O-O for both
  colours, king captures across the d/e line, and moves deep in the stack
  after a crossing (ancestor reuse after a refresh).
- Engine `eval` equals `check_net.py` bit-exact on the default FENs plus
  their horizontal mirrors.
- A position and its horizontal mirror (without castling rights) give the
  same engine eval. Add this as a third line of the `eval` command output,
  next to the existing colour-mirror line.
- Perft unchanged (makemove touched).
- `python3 tools/sprt/bench.py <binary> 12` runs clean; NPS compared to the
  baseline with the paired harness described in the testing notes.

### 7.7 Acceptance

Primary: SPRT `GOOB-mirror` (run M) vs `GOOB-mirror-control` (run C), STC,
then LTC confirmation. This decides whether the engine change is merged.

Ship decision: SPRT run M vs the accepted engine. If M beats the accepted
engine but not C, ship C (no engine change) and drop mirroring.

```bash
tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-mirror tools/sprt/bin/GOOB-mirror-control nnue-mirror 6+0.06
tools/sprt/run_sprt.sh tools/sprt/bin/GOOB-mirror tools/sprt/bin/GOOB-mirror-control nnue-mirror-ltc 12+0.1 4
```

If run F was trained, SPRT M vs F tells how much of the gain is the king-half
bit rather than symmetry. This is for understanding, not for the ship
decision.

---

## 8. Phase 4 – Factorized King Buckets + Accumulator Cache

### 8.1 Idea

King buckets give every piece-square weight a different value depending on
where the own king is. That is a big gain in knowledge, but normally each
bucket sees only a fraction of the data, so it needs much more data. This is
the one place where the data-per-weight argument of Section 1 applies.

The **factorizer** removes that cost during training:

```
effective_weight[bucket][feature] = bucket_weight[bucket][feature] + shared_weight[feature]
```

- `shared_weight` gets a gradient from **every** position, exactly like the
  current net, so it learns at today's speed.
- `bucket_weight` only learns the difference a king location makes, which
  is small and needs little data.
- At export the two are summed into one matrix. The engine never sees the
  factorizer; inference is an ordinary king-bucketed net.

This is the factorizer design used by Stockfish's trainer. In community
experience king buckets are the most reliable architectural gain available
to a 768-input net once the data split is handled, and the factorizer handles
it.

**Adam and the factorizer.** The "buckets learn only the difference"
argument assumes updates proportional to gradient size. Adam normalizes per
parameter, so a rarely active (bucket, feature) pair takes full-size noisy
steps early on. A decoupled weight decay on `ft_buckets` is the shrinkage
prior that pulls those deltas back toward the shared weight and makes the
argument hold. It is part of this phase, not a later tweak: train with AdamW
and a decay on `ft_buckets` (start at 0.01; try a larger value on the bucket
tensor than on the rest). It also means the control (8.6) trains with the
same optimizer so the comparison stays clean.

### 8.2 Warm start that starts exactly at Phase 3 strength

Initialize `shared_weight` = the Phase 3 net's 768 x 1024 matrix, all
`bucket_weight` = 0, and copy the FT bias and output layer. At step 0 the
new net produces **exactly** the Phase 3 net's outputs, so every extra row
of training is pure gain. This is the main reason this design fits the
"little data" goal.

### 8.3 Bucket layout

4 buckets on top of mirroring (Phase 3 must be accepted first). Squares are
from the own perspective (`rel(ksq)`, after the mirror flip, so only files
a–d occur):

```
            a  b  c  d
rank 8:     3  3  3  3
rank 7:     3  3  3  3
rank 6:     3  3  3  3
rank 5:     3  3  3  3
rank 4:     3  3  3  3
rank 3:     3  3  3  3
rank 2:     2  2  2  2
rank 1:     0  0  1  1
```

- Bucket 0: king tucked in the corner (castled-like; g1 and h1 map here).
- Bucket 1: king on c1/d1. After mirroring this holds the uncastled king
  (e1 maps to d1), the queenside-castled king (c1) and f1. These are
  different king-safety situations sharing one bucket; see Open Questions.
- Bucket 2: king stepped up to rank 2 (h2, g2 and the queenside equivalents).
- Bucket 3: king active (endgames, exposed king). This bucket covers 24 of
  the 32 relative squares and is the most heterogeneous.

Store it as a full 64-entry table with files e–h filled symmetrically (so it
is safe even if called without mirroring):

```c
static const uint8_t KingBucket[64] = {
    0, 0, 1, 1, 1, 1, 0, 0,
    2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
};
```

The same table, as a numpy array, lives in the trainer. Keep the layout in
one place per language and test that both agree (`check_net.py`).

Why 4 and not more: the training cost below grows with bucket count, and on
CPU the feature-transformer optimizer step is the expensive part. 8+
buckets only after Phase 4 passes and only if training time allows.

### 8.4 Trainer changes

**Feature index:** `bucket * 768 + feat768`, where `bucket =
KingBucket[rel(own ksq) ^ flip]` per perspective. Computed in
`nnue_collate` next to the mirror flags (vectorized, same pattern as 7.3).

**`model.py` – factorized feature transformer.** Don't add a second
`EmbeddingBag`; that would double the active features per sample. Instead
build the merged matrix once per step and use the functional embedding bag:

```python
self.ft_buckets = nn.Parameter(torch.zeros(NB, 768, L1))
self.ft_shared  = nn.Parameter(torch.empty(768, L1))
...
def ft_weight(self):
    return (self.ft_buckets + self.ft_shared.unsqueeze(0)).view(NB * 768, L1)

def forward(...):
    w = self.ft_weight()
    acc_w = F.embedding_bag(white_idx, w, offsets, mode="sum") + self.ft_bias
    acc_b = F.embedding_bag(black_idx, w, offsets, mode="sum") + self.ft_bias
    ...
```

The merge costs NB x 768 x 1024 additions per step (about 3M for 4
buckets), which is small next to the batch itself. Gradients flow to both
parameters automatically.

**Clipping must apply to the merged weight.** Clipping each parameter to
+/-1.98 is not enough: the sum could reach +/-3.96 and break the int16
accumulator bound. After each step:

```python
model.ft_shared.clamp_(-limit, limit)
model.ft_buckets.copy_(torch.clamp(model.ft_buckets,
                                   -limit - model.ft_shared,
                                    limit - model.ft_shared))
```

(`torch.clamp` accepts tensor bounds, which broadcast over the bucket
dimension.)

**Optimizer.** AdamW with parameter groups: `ft_buckets` gets its own decay
(flag `--bucket-decay`, default 0.01); the remaining parameters get
`--weight-decay` (default 0.01 in this phase). Both are plain flags so Phase
6 can vary them.

**`train.py`:** new flags `--king-buckets 4`, `--init-from-unbucketed
<phase3.pt>` (copies the Phase 3 FT matrix into `ft_shared`, zeros the
buckets, copies bias and output layer), `--optimizer adamw`,
`--weight-decay`, `--bucket-decay`. `arch` in the checkpoint becomes
`{"mirror": True, "king_buckets": 4, "factorized": True}`.

**`export_weights.py`:** quantize the merged matrix
`ft_buckets + ft_shared` (layout `[bucket][768][1024]`, bucket-major) with
QA, then bias and output layer as today, then the format tag with
`king_buckets = 4`. Run the existing accumulator-bound check on the merged
matrix. Print how many merged weights differ from the shared weight by less
than one quantization step (1/255): deltas below that vanish at export, and
a very high share means the buckets learned little.

**`check_net.py`:** bucketed feature indices, the bound check over each
bucket, and default FENs that put each king in each bucket.

### 8.5 Training cost estimate

| | Current | Phase 4 (4 buckets) |
|---|---|---|
| FT parameters | 0.79M | 3.15M buckets + 0.79M shared |
| Active features per sample | ~2 x 30 | same |
| Forward/backward FLOPs on the batch | 1x | ~1x |
| Per-step extra work | – | merge (3M adds) + AdamW on ~5x more FT parameters |
| Net file size | 1.6 MB | ~6.3 MB |

`nn.EmbeddingBag` / `F.embedding_bag` gives a dense gradient, so the
optimizer touches every FT parameter every step. Measure the real cost
against the Phase 0 speed baseline with 500 batches before starting a long
run. If it is too slow:

- Raise the batch size to 16384 with the learning rate adjusted as Phase 6
  describes (this halves the number of optimizer steps per epoch).
- Only if still too slow: try a sparse gradient for the bucket matrix with
  `torch.optim.SparseAdam` (requires a separate `nn.Embedding` path; more
  code, keep as a fallback).

### 8.6 Runs

Control recipe (six epochs, lr 5e-4, seed 0) on the training set carried
forward, into `exp_kb4/`:

| Run | Flags | Purpose |
|---|---|---|
| K (variant) | `--king-buckets 4 --init-from-unbucketed <phase3.pt> --optimizer adamw --weight-decay 0.01 --bucket-decay 0.01 --mirror ...` | the bucketed net |
| C (control) | `--mirror --init <phase3.pt> --optimizer adamw --weight-decay 0.01 ...` | same recipe and optimizer, no buckets |

### 8.7 Engine changes

1. **Weights.** `ft_w` becomes `[NNUE_KING_BUCKETS * 768 * 1024]`;
   `NNUE_TOTAL_SHORTS` and the embedded-size check follow. Row addressing:
   `ft_w + ((bucket * 768 + feat) << 10)`.

2. **Per-perspective "king state".** Generalize Phase 3's `flip` into a
   king state per perspective: `(flip, bucket)`. Store both in
   `NNUE_Accumulator`. `DirtyPiece.refresh[side]` is set in `makeMove()`
   when the king's (flip, bucket) differs between from- and to-square. Note
   that with the layout in 8.3 the common middlegame king moves g1–h2, g1–f1
   and g1–g2 all change bucket, so refreshes are frequent and the cache
   below is required, not optional.

3. **Accumulator cache ("Finny table").** A refresh from scratch costs
   about 30 row additions per perspective. Per thread, keep:
   ```c
   typedef struct {
       ALIGN64 int16_t acc[NNUE_HIDDEN_SIZE];
       U64 pieceBB[12];          /* piece bitboards the cached acc was built from */
   } NNUE_CacheEntry;

   /* in S_SEARCH_THREAD: */
   NNUE_CacheEntry nnue_cache[COLOR_NB][2 /* flip */][NNUE_KING_BUCKETS];
   ```
   Refresh of perspective `p` with king state `(flip, bucket)`:
   1. `e = &nnue_cache[p][flip][bucket]`.
   2. For each of the 12 piece types: `removed = e->pieceBB[i] & ~now[i]`,
      `added = now[i] & ~e->pieceBB[i]`.
   3. Apply all removed/added rows to `e->acc` with `nnue_acc_apply()`
      (callers already group up to 4 adds + 4 subs per pass).
   4. Store `now[]` in `e->pieceBB`, copy `e->acc` into the target
      accumulator.

   Typically only a few pieces differ, so a refresh drops from ~30 row
   additions to a handful. Memory: 2 x 2 x 4 x ~2.1 KB ≈ 34 KB per thread.
   Reference: Stockfish's `AccumulatorCaches`
   (`Stockfish-sf_19/src/nnue/nnue_accumulator.h`, around line 58) and
   `update_accumulator_refresh_cache` (declared near line 53 and defined
   near line 880 of `nnue_accumulator.cpp`). Stockfish keys its cache by
   king square; keying by `(flip, bucket)` is the same idea for this feature
   set, since those two values are everything that determines the feature
   mapping. Understand and adapt it; don't copy.

4. **Cache invalidation.** Reset every entry to (acc = FT bias, empty
   bitboards) when a thread is allocated, on `ucinewgame`, and **whenever a
   new net is loaded** (`EvalFile`), otherwise the cache holds sums of the
   old net's weights.

5. **Lazy update.** Same per-perspective logic as Phase 3, with "refresh"
   meaning "refresh through the cache".

6. **Threads.** The cache is per thread, so Lazy SMP needs no locking.

7. **Cache footprint.** The feature-transformer table grows to about
   6.3 MB. The two perspectives can sit in different buckets, so the hot
   working set grows from about 1.5 MB to about 3 MB and may no longer fit
   L2. This is the most likely source of an NPS loss and the reason for the
   paired NPS measurement and the LTC confirmation below.

### 8.8 Tests for Phase 4

Everything from 7.6, plus:

- King walks across every bucket boundary (a1→a2, c1→c2, a2→a3, c1→b1,
  d1→e1, …) for both colours, compared incremental vs fresh.
- Cache vs fresh: after long random games, every cache-based refresh must
  equal a from-scratch refresh bit for bit.
- Load net A, search, load net B with `EvalFile`, search: evals must match a
  fresh engine started with net B (catches stale caches).
- Multi-threaded `go depth` runs (2–4 threads) without crashes or eval
  mismatches.
- NPS paired comparison against the Phase 3 binary; target within a few
  percent. Record the result even if the SPRT passes.

### 8.9 Acceptance

Primary: SPRT run K vs run C, STC then LTC. The LTC run matters more here
than in Phase 3: a net that pays NPS for knowledge gains more at longer
controls, and an STC-only pass or fail can mislead in both directions.

Ship decision: SPRT run K vs the accepted engine.

---

## 9. Phase 5 – Search Re-Tune After a Net Change

The pruning margins, razoring and futility thresholds, null-move terms and
correction-history scale in `tune.h` were tuned by SPSA against the current
net. A different net has a different error profile, and the margins tuned
for the old one are no longer the best for the new one. The SPRT of a new
net therefore understates what the net can deliver.

After any accepted net (Phase 1 included):

1. Run the SPSA tuner on the margin group (`tools/spsa/README.md` has the
   commands; the `--include 'Futility|SEE|Razor|BetaMargin|probCutMargin'`
   example is the right starting set).
2. SPRT the tuned constants against the untuned ones with the **same net**,
   as a search change under the usual rules in `AGENTS.md`.
3. Record the SPSA run name and the constants that changed in the results
   log of the net phase.

This is a search phase and does not touch the trainer or the net file.

---

## 10. Phase 6 – Trainer Recipe

These change only the training procedure. Each one is a new flag whose
default keeps today's behaviour, so old commands still reproduce old
results.

| Flag | Change | Why | How it is judged |
|---|---|---|---|
| `--batch-size 16384` with `--lr` raised | larger batch | fewer optimizer steps per epoch, which matters once Phase 4 makes each step costlier. At fixed epochs the net receives half as many updates, so on its own this is a strength trade, not a speedup; raise the learning rate (start at 1.4x) or add epochs | validation MSE at equal wall-clock, same loss |
| `--warmup-batches N` | linear LR warmup over the first N batches | stabilizes the start of a from-scratch run. Every warm start in this plan runs at lr 5e-4 and does not need it | validation MSE on a from-scratch run only |
| `--optimizer adamw --weight-decay 0.01` | AdamW instead of Adam | introduced in Phase 4 for the bucket deltas; here the question is whether it helps the unbucketed parameters too | validation MSE, same loss |
| `--loss-power 2.5` | loss `mean(\|sigmoid(out) - target\|^p)`; `p = 2` is today's MSE | common in bullet-trained nets with WDL-blended targets; weights large errors more. Its value with pure-eval targets is not established | SPRT only |

### 10.1 Gates

- **Same-loss changes** (batch size, warmup, optimizer): train variant and
  control with identical init, data, epochs and seed, and compare validation
  MSE on the fixed validation set. Validation MSE is comparable here because
  the loss function is unchanged. A variant that does not lower validation
  MSE is dropped without a match.
- **The loss change** cannot be gated on validation MSE: a net trained on a
  power-2.5 loss is expected to score worse on MSE while possibly playing
  better. It is judged by SPRT only.
- **One combined SPRT.** The surviving same-loss changes plus the loss change
  form one "recipe" net, trained with the control recipe from the accepted
  net, and get one SPRT against the control-recipe net. Six separate SPRTs
  on effects of a few Elo each would end inconclusive under the 3000-game cap
  of `run_sprt.sh`.

### 10.2 Experiment design

Fine-tuning a net for extra epochs can gain Elo by itself, regardless of the
setting being tested. To measure the setting and not just the extra
training:

1. **Control:** fine-tune the accepted net for K epochs with today's settings.
2. **Variant:** fine-tune the same net for the same K epochs, same data,
   same seed, changing only the recipe.
3. SPRT variant vs control (not vs the original net).

Write each into its own directory under `checkpoints/exp_recipe/`.

---

## 11. Deferred Architecture Work

Not rejected; deferred, with the reason stated correctly and the condition
that would reopen each item.

| Idea | Real cost | Reopen when |
|---|---|---|
| Bigger hidden layer (1536, 2048) | Inference: the forward pass and every accumulator update scale with L1, so NPS drops. Training time scales the same way. It does **not** reduce examples per weight (Section 1). Overfitting risk is about total capacity versus data, which is real but smaller than the speed cost | the Phase 2 dataset is several times today's and a paired NPS test shows headroom |
| Extra hidden layers (`-> 16 -> 32 -> 1`) | The current kernel is one fused SCReLU dot product per bucket; adding layers is a substantial rewrite of `nnue_loader.h` and costs per-eval time. Small L2/L3 layers have given gains to 768-style engines at hundreds of millions of positions, so "needs billions" is not the reason to wait | Phases 3 and 4 are done and the data phase has grown the set |
| Threat / attack inputs | large engine and trainer change, slower updates, more data | after the above |
| Many king buckets without a factorizer | each bucket sees 1/N of the data; this is exactly the data cost the factorizer avoids | never without the factorizer; 8+ buckets with it after Phase 4 passes |
| Rejecting engine-generated data | the previous revision's reason ("dilutes deep Stockfish labels") ignores distribution match, the WDL signal and label-scale consistency. See Phase 2 | n/a; it is now a phase |

---

## 12. Net File Format Versioning

A mirrored net has exactly the same size as the current net, so the
engine cannot tell them apart by size. Use the 48 trailing padding bytes
(currently `"bullet"` x 8) as a tag, keeping the file size a multiple of 64:

| Offset in trailer | Size | Content |
|---|---|---|
| 0 | 8 | magic `"GOOBNNUE"` |
| 8 | 4 | `uint32` format version (2 for the first tagged format) |
| 12 | 4 | `uint32` flags: bit 0 = mirroring |
| 16 | 4 | `uint32` number of king buckets (1 = none) |
| 20 | 4 | `uint32` hidden size (1024) |
| 24 | 4 | `uint32` output buckets (8) |
| 28 | 20 | zero |

Rules:

- A trailer of `"bullet"` x 8 (or any non-`GOOBNNUE` trailer) means the
  legacy format: no mirroring, no king buckets.
- The engine compares the tag to its compiled architecture and refuses a
  mismatch with an `info string` explaining what it expected and what it
  found. The embedded net and `EvalFile` go through the same check.
- **A refused embedded net is fatal at startup** (Phase 0, 4.8). A refused
  `EvalFile` keeps the previously loaded net active, as today.
- `export_weights.py` writes the tag from the checkpoint's `arch`;
  `check_net.py` reads it to pick the feature mapping.

---

## 13. Testing Checklist (every phase)

Following `AGENTS.md` "Correctness Before Elo":

- [ ] Engine compiles cleanly (native and universal targets).
- [ ] Perft suite unchanged (`perfttest`).
- [ ] `tools/test_incremental.c`: 0 mismatches between incremental and
      fresh accumulators, including the phase-specific king sequences.
- [ ] Engine `eval` == `check_net.py` bit-exact on the default FEN set,
      including horizontal mirrors from Phase 3 on.
- [ ] Engine loads the new net embedded **and** via `EvalFile`; refuses a
      net of the wrong architecture; **exits with an error when the embedded
      net is refused**.
- [ ] Mate scores and PV look sane on a few tactical positions.
- [ ] `python3 tools/sprt/bench.py <binary> 12` runs without crashes.
- [ ] NPS paired comparison against the previous accepted binary, recorded
      even when the SPRT passes.
- [ ] For architecture phases: the same-recipe control net was trained and
      the primary SPRT is variant vs control.
- [ ] SPRT (conclusive, or 3000 games) against the previous accepted
      binary with identical conditions, **then an LTC confirmation for any
      net change**.
- [ ] For data phases: `check_net.py --pipeline` on generated records,
      result distribution reviewed, no in-check records.
- [ ] `src/README.md`, `tools/README.md`, `tools/nnue_project/README.md`
      updated (architecture, file format, new flags, new tools).
- [ ] No existing dataset, checkpoint or net was modified (re-check the
      SHA-256 values from Phase 0).

---

## 14. Results Log Template

Copy one block per experiment.

```
### <phase / experiment name>
Date:
Branch / commit:
Net:                 path + sha256
Trained from:        scratch | warm start from <checkpoint>
Control net:         path + sha256 (same recipe, architecture flag off) | n/a
Training data:       <file>, <positions>, generated share <%>, wdl-lambda
Epochs / LR / batch / seed:
Loss / optimizer / decay:
Training speed:      <positions/s>, total time
Final val MSE:       variant / control (comparable only under the same loss)

Engine NPS vs base:  <+/- %> (paired harness)
SPRT (STC):          <new> vs <base>
  Games:
  W / L / D:
  Score:
  Elo:               +/-
  LLR / bounds:
  TC / threads / hash / concurrency / book:
SPRT (LTC confirm):  same fields
Verdict:             accepted | rejected
Follow-up:           SPSA re-tune run name, constants changed | pending
Notes:
```

---

## 15. Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Accidentally overwriting the current net, checkpoint or data | explicit output paths into `exp_*/` and `data_gen/`; SHA-256 check from Phase 0; never build into `src/weights/` during experiments (`EVALFILE=` instead); the data generator refuses to open an existing file |
| Engine silently loads a net of the wrong architecture | format tag (Section 12) with a hard refusal on mismatch |
| Engine silently plays with a zero evaluation after a refused embedded net | fatal startup on embedded-net failure (4.8) |
| Elo from extra fine-tuning attributed to an architecture change | same-recipe control for every architecture phase; Phase 1 measures the fine-tune effect on its own |
| SPRT cannot resolve the effect | expected-size check before each match; same-loss recipe changes gated on validation MSE; one combined recipe SPRT |
| A speed-for-knowledge trade passes or fails only at STC | LTC confirmation for every net change |
| Incremental update bug after a king crosses the mirror line or a bucket border | forced king sequences in `test_incremental.c`; refresh decision taken *before* replaying dirty pieces |
| Stale accumulator cache after `EvalFile` or `ucinewgame` | explicit invalidation + a dedicated test (8.8) |
| int16 accumulator overflow from factorized weights | clip the merged weight (8.4), bound check on the merged matrix at export |
| Bucket deltas drift under Adam on rare features | weight decay on `ft_buckets` inside Phase 4 |
| Phase 4 training too slow on CPU | measure 500 batches first; batch 16384 with LR adjustment; SparseAdam fallback; stay at 4 buckets |
| Self-play labels too shallow, or mix ratio wrong | Phase 2 tests ratios and lambda separately; Lichess set is kept, never replaced |
| Label scale mismatch between Lichess and generated rows | the net's output is already on the Lichess scale (x 400), so GOOB's scores are approximately on it; the mix SPRTs measure the residual |
| Generated data distribution skewed by adjudication | result distribution and game-length review in 6.7 |
| New net invalidates the SPSA-tuned margins | Phase 5 re-tune after any accepted net |
| Warm-start net looks worse early in training | expected for Phase 3's first epoch; decide only on the final net's SPRT |
| Float checkpoint missing | `--init-from-bin` dequantized warm start (4.2) |

---

## 16. Open Questions

Items to settle during the phases, not before them.

1. **Does `nnue.pt` still exist?** Phase 0, 4.2. Decides whether the
   dequantized warm start is needed.
2. **Which script built the training set, and is it quiet-only?** Phase 0,
   4.3. Changes how the gap table in Section 1 reads and how Phase 2's mix
   results are interpreted.
3. **Bucket 1 grouping.** c1, d1 (and e1, f1 via mirroring) share a bucket in
   8.3. If Phase 4 passes, a layout that separates the queenside-castled king
   from the uncastled king is the next thing to try, before raising the
   bucket count.
4. **Lopsided positions.** Phase 2 keeps them in generated data. Whether to
   also regenerate the Lichess set with a higher `--max-abs-cp` is open; the
   generated data may cover the gap on its own.
5. **Evaluating in check.** The search computes the static eval in check and
   the net never trained on such positions. Not evaluating in check (as
   Stockfish does) is a search change outside this plan, but it closes a gap
   the plan inherits and should be tried under the search rules in
   `AGENTS.md` once a net phase has landed.
6. **Generated-data parameters.** Nodes per move, opening source, random
   plies and adjudication thresholds in 6.3 are starting values. The first
   throughput and result-distribution measurements decide them.
7. **How much of the mirroring gain is the king-half bit?** Run F in 7.4
   answers it if it is trained. It changes nothing about the ship decision,
   but it decides whether flip augmentation is worth keeping as a default for
   any future unmirrored net.
