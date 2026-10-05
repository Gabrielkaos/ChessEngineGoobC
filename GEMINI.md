# ChessEngineGoobC Development Rules

## Engine Reference Sources

If you need to study how other chess engines implement something, DO NOT
perform web searches for chess-engine source code or implementations.

Use the local reference engines in the specified directory:

- Ethereal-12.75
- Ethereal-14.00
- Stockfish-sf_19
- berserk-14
- Schoenemann-0.5.0
- c4ke-main

You may inspect their source code and use them as implementation references.
Do not copy code blindly. Understand the algorithm and adapt it to the
architecture of ChessEngineGoobC.

---

## Source / Tools Documentation

Whenever changes are made to `src/` or `tools/`, the `README.md` in the
corresponding directory MUST be kept synchronized with the actual
implementation.

Before finishing a task:

1. Check whether `src/` or `tools/` was modified.
2. Update the corresponding README if necessary.
3. Verify that the README accurately describes the current implementation.

---

## NNUE Protection

NEVER overwrite, regenerate, modify, delete, or replace anything related to:

- NNUE datasets
- NNUE training data
- NNUE weights
- NNUE network files
- NNUE generated artifacts

Do not run commands or programs that could overwrite these files.

Before running any training, conversion, generation, or testing program,
verify that it cannot modify NNUE datasets or weights.

If there is uncertainty about whether a command could modify NNUE data,
STOP and inspect it before running it.

---

## Engine Strength Testing

Every new engine implementation or search-related modification MUST be
tested against the previous baseline version.

The baseline is the engine state immediately before the changes are made.

Use:

    tools/sprt/run_sprt.sh (cutechess-cli)

Refer to:

    tools/sprt/README.md

Sanity check before long matches:

    python3 tools/sprt/bench.py <binary> 12

Minimum match size:

    500 games (or until SPRT reaches a conclusive decision)

Do not claim that an implementation is stronger without completing the
required engine-vs-baseline test.

Record at minimum:

- Number of games
- Wins
- Losses
- Draws
- Win rate / score
- Elo difference if available
- Time control
- Threads
- Hash size
- Any relevant engine options

Keep testing conditions identical between the baseline and modified engine.

---

## Search Improvements

When working on search:

1. First inspect the existing search implementation completely.
2. Identify the current search algorithm and existing heuristics.
3. Establish a baseline before modifying code.
4. Identify the specific bottleneck or weakness being addressed.
5. Compare relevant techniques with the local reference engines.
6. Implement one logical improvement at a time where practical.
7. Compile and run functional tests.
8. Test tactical correctness and run a sanity bench (tools/sprt/bench.py) before strength testing.
9. Run the required SPRT / 500-game cutechess-cli match against the baseline using tools/sprt/run_sprt.sh.
10. Analyze the result before deciding whether to keep the change.

Do not make large collections of unrelated search changes and then attribute
the resulting Elo change to a single technique.

Prefer changes that can be measured independently.

---

## Correctness Before Elo

Engine strength is not the only criterion.

Before strength testing, verify:

- Legal move generation remains correct.
- Search does not crash.
- No obvious search instability was introduced.
- Principal variation remains valid.
- Mate scores and mate distances remain correct.
- TT behavior remains correct.
- Quiescence search remains correct.
- Time management remains functional.
- UCI behavior remains functional.

A change that gains Elo but introduces incorrect chess behavior must not be
accepted.

---

## General Development Rule

Do not modify files unrelated to the current objective.

Before making architectural changes, inspect the existing implementation
and explain why the change is necessary.

Prefer small, measurable, reversible changes.

Never delete existing functionality merely to simplify implementation unless
the task explicitly requires it.