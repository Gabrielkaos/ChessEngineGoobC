# GOOB Chess Engine – Source Directory (`/src`)

This directory contains the complete C source code for **GOOB 2.2-BETA**, a high-performance, UCI-compliant chess engine developed by Gabriel Montes.

---

## Table of Contents
1. [Architectural Overview](#architectural-overview)
2. [File-by-File Reference](#file-by-file-reference)
3. [Subdirectories & Assets](#subdirectories--assets)
4. [Compilation & Multi-ISA Build System](#compilation--multi-isa-build-system)
5. [UCI Protocol & Supported Options](#uci-protocol--supported-options)
6. [Design Details & Key Invariants](#design-details--key-invariants)

---

## Architectural Overview

* **Board Representation:** Little-Endian Rank-File (A1 = 0, H8 = 63) bitboards (`byTypeBB` for piece types 1..6, `byColorBB` for white/black occupancy) paired with an 8-bit mailbox array (`pieces[64]`).
* **State Management:** Linked [`StateInfo`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/board.h#L83-L99) nodes (`pos->st->previous`). Rolling back moves via [`takeMove()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makemove.c#L443-L485) restores the pointer without recomputing Zobrist keys, 50-move counters, castling permissions, or en-passant squares.
  The game history lives in `stateTable[MAXGAMESMOVES]` (550 states). When a `position ... moves` list outgrows it, [`compactStateHistory()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/board.c) keeps the newest 275 states (repetition detection never looks further back than the fifty-move counter) and `gamePlyOffset` keeps `hisPly + gamePlyOffset` equal to the real game ply for time management, WDL output and FENs.
* **Move Generation & Legality:** Pseudo-legal bulk bitboard generation with hardware PEXT (BMI2) attack lookups (and fallback to magic bitboards). Legality is tested dynamically on-the-fly via [`legal()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makemove.c#L158-L229) using pin and king-ray masks without making/unmaking moves.
* **Evaluation:** Schoenemann-0.5.0 style NNUE architecture:
  * Topology: `(768 -> 1024)x2 -> 1x8 buckets` with Squared Clipped ReLU (SCReLU) activation.
  * King squares are omitted from the 768-feature index, making king moves ordinary $\mathcal{O}(1)$ incremental updates and eliminating accumulator rebuilds during search.
  * Quantized SIMD inference routines (AVX-512, AVX2, and scalar fallbacks).
* **Correction History:** Multi-table evaluation error correction indexed by pawn-king hash, minor-piece hash, dual non-pawn material hashes, and 2-ply / 4-ply continuation moves.
* **Search:** Alpha-Beta with Principal Variation Search (PVS), Iterative Deepening, Aspiration Windows, Singular Extensions (including double extensions and multicut), ProbCut, Null Move Pruning with verification, Razoring, Beta Pruning (RFP), IIR, Late Move Pruning (LMP), Futility Pruning, SEE pruning, and **Surprise-SRD** (dynamic LMR responding to sibling fail highs and evaluation expectation).
* **Transposition Table:** 4-entry, 64-byte buckets (matching an x86 L1 cache line), lockless SMP key verification combining `posKey` and `smp_data`, and age/depth replacement.
* **Endgame Tablebases:** Syzygy 3–7 men support integrated via Fathom probe at root and interior nodes.
* **Parallel Search:** Lazy SMP with thread voting across threads wrapped by [`tinycthread`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/tinycthread.c).

---

## File-by-File Reference

### Board & Move Representation
* **[`defs.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/defs.h) / [`defs.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/defs.c):** Global constants, piece types (`PAWN`..`KING`), piece colors (`WHITE`, `BLACK`), square enums (`A1`..`H8`), piece arrays, align64 allocation wrappers (`goob_aligned_alloc`, `goob_aligned_free`), bit-counting macros, and search limits.
* **[`board.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/board.h) / [`board.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/board.c):** Defines [`S_BOARD`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/board.h#L102-L142), [`StateInfo`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/board.h#L83-L99), [`S_SEARCH_THREAD`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/board.h#L33-L54), and [`S_SHARED_TABLES`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/board.h#L69-L81). Implements FEN parsing ([`ParseFEN`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/board.c#L153)), board resetting ([`ResetBoard`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/board.c#L288)), board printing, board mirroring, material list updates, and stack initializations.
* **[`bitboards.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/bitboards.h) / [`bitboards.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/bitboards.c):** Bitboard lookup tables (`FileBBMask`, `RankBBMask`, `BetweenBB`, `LineBB`) and manipulation utilities (LSB extraction, clearing least significant bits, bitboard printing).
* **[`hashkeys.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/hashkeys.h) / [`hashkeys.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/hashkeys.c):** 64-bit Zobrist keys for positions (`posKey`), pawn-king structures (`pkHash`), non-pawn material keys per color (`npHash`), and minor pieces (`minorHash`).
* **[`makemove.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makemove.h) / [`makemove.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makemove.c):** Core board transition functions:
  * [`makeMove()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makemove.c#L243): Clones `StateInfo`, applies moves, tracks [`DirtyPiece`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/board.h#L18-L26) for NNUE, updates Zobrist hashes incrementally, and checks repetitions.
  * [`takeMove()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makemove.c#L443): Reverses piece positions and restores `pos->st = pos->st->previous`.
  * [`makeNullMove()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makemove.c#L487) / [`takeNullMove()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makemove.c#L524): Null-move transitions for Null Move Pruning.
  * [`legal()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makemove.h): Fast inlined on-the-fly pseudo-legal move validator featuring an ultra-fast non-pinned fast path (3 bitwise tests for ordinary non-king moves when not in check).
  * [`moveIsTactical()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makemove.h), [`moveEstimatedValue()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makemove.h), and [`MoveBestCaseValue()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makemove.h): Inlined tactical classification and move value estimators.
  * `update_slider_blockers()`: Recomputes the side to move's king blockers and the enemy pinners after every move, scanning snipers on the empty-board rays (`rook_pseudo_attacks` / `bishop_pseudo_attacks`) with an early exit when no enemy snipers exist.

### Attacks & Move Generation
* **[`attacks.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/attacks.h) / [`attacks.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/attacks.c):** Slider and leaper attack generation. Uses `_pext_u64` under BMI2 (`PEXT_ATTACKS`) with fallback to magic bitboards. Inlines `allAttackersToSquare()` and features ray pre-filtering in `attackersToKingSq()` using `bishop_pseudo_attacks` and `rook_pseudo_attacks` to bypass magic/PEXT table lookups when rays are empty.
* **[`movegen.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/movegen.h) / [`movegen.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/movegen.c):** Move generation routines. All three generators share one inline core, `generate()`, that is specialized at compile time for the side to move and the move type (all / noisy / quiet); moves are written through a local list cursor. The emission order is fixed (pawns, knights, bishops, rooks, queens, castling, king; within a piece captures before quiets) because the move picker breaks score ties by list position:
  * [`GenerateAllMoves()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/movegen.c#L204): Full pseudo-legal generator.
  * [`GenerateAllNoisy()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/movegen.c#L212): Captures and promotions only.
  * [`GenerateAllQuiet()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/movegen.c#L221): Non-tactical moves only; appends to the list after the noisy moves.
  * [`moveIsPseudoLegal()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/movegen.c#L230): Rapid pseudo-legality validator for TT, killer, counter and follow-up moves.

### Move Ordering & History Heuristics
* **[`movepicker.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/movepicker.h) / [`movepicker.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/movepicker.c):** Staged move selection:
  1. Hash move from Transposition Table.
  2. Winning & equal noisy moves:
     * Score = capture history + a victim bonus (`MVVAugment`: Pawn 700, Knight/Bishop 3150, Rook 4725, Queen 9100; quiet promotions count as a pawn victim). There is no attacker (LVA) term here.
     * `getCaptureHistory()` adds +64,000 to queen promotions; knight promotions get +15,000 and Bishop/Rook underpromotions -50,000 so they do not clog good noisies before quiets.
     * Gated by Static Exchange Evaluation (SEE $\ge$ threshold, 0 in the main search); failures are deferred to the bad noisy stage.
  3. Killer moves 1 & 2 (with duplicate elimination ensuring `killer2 != killer1`).
  4. Counter move (keyed by opponent's previous move).
  5. Followup move (keyed by own move 2 plies ago).
  6. Quiet moves: scored by butterfly + continuation + pawn history + root low-ply history + threat escape/entry bonuses + safe check bonuses; loop invariants (`pawnHistIndex(pos)`, `pawnTable`, `lowPlyTable`, and `basePiece`) are hoisted out of the candidate loop.
  7. Bad noisy moves (losing captures sorted by least material loss first: `(victimVal - attackerVal) * 16 + chist / 16` to maximize cutoff probability).
  8. Bad quiet moves (moves below `GoodQuietThreshold`).
* **[`history.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/history.h) / [`history.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/history.c):**
  * Butterfly history with threat context (`[side][threat_from][threat_to][piece][to]`).
  * Capture history (`[piece][threat_from][threat_to][to][captured]`).
  * Continuation history across 4 slots (1, 2, 4, 6 plies back).
  * Low-ply butterfly history (first 5 plies near the root).
  * Pawn history indexed strictly by pawn structure Zobrist key (king zobrist keys stripped from `pkHash` via `pawnHistIndex()`).
  * Gravity formula updates: `entry += bonus - entry * abs(bonus) / D`. History updates are gated at depth $> 0$ to prevent inverted bonuses/maluses on horizon check evasions.

### Evaluation & NNUE
* **[`evaluate.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/evaluate.h) / [`evaluate.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/evaluate.c):** Inlined evaluation interface [`EvalPosition()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/evaluate.h) featuring null-move tempo estimation (`-pos->search->eval_stack[pos->ply - 1] + 40`) and dispatch to NNUE.
* **[`nnue_loader.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/nnue_loader.h) / [`nnue_loader.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/nnue_loader.c):**
  * Schoenemann 0.5.0 NNUE architecture inference engine (768 input features, 1024 hidden, 8 material buckets).
  * AVX-512, AVX2, and scalar forward passes featuring 4-chain unrolled accumulators and exact/fast int16 dot-product kernels.
  * Synchronized dual-perspective lazy accumulator updates through [`nnue_update_accumulators_to_ply()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/nnue_loader.h) with fallbacks to bitboard-driven full refresh when beyond `NNUE_REFRESH_THRESHOLD` (32 plies) or when no ancestor accumulator is computed.
  * Specialized SIMD update kernels for quiet moves (`1add_1sub`, unrolled across 8 256-bit AVX2 registers) and captures (`1add_2sub`).
  * Precomputed 64-byte `s_piece_offset` table and bit-shift feature row addressing (`<< 10`).
  * Weight loading from embedded binary via [`incbin.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/incbin.h) or external file (`EvalFile` UCI option).
* **[`correction_types.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/correction_types.h), [`correction.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/correction.h) / [`correction.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/correction.c):** Four-table correction history blending pawn structure, minor pieces, non-pawn material per color, and 2-ply / 4-ply continuation corrections into static evaluations.

### Search & Transposition Table
* **[`search.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/search.h) / [`search.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/search.c):**
  * [`AlphaBeta()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/search.c#L231): PVS search implementation containing:
    * Mate distance pruning.
    * TT probing and TT research margins.
    * Syzygy tablebase WDL probing at interior nodes.
    * Hindsight depth adjustments (+1 extension / -1 reduction).
    * Razoring ($D \le 2$).
    * Beta Pruning / RFP ($D \le 8$).
    * Null Move Pruning with verification search ($D \ge 16$).
    * Internal Iterative Reduction (IIR) with all-node bias.
    * ProbCut ($D \ge 5$) with TT refutation check (`!(ttHit && ttDepth >= depth - 3 && ttValue < rBeta)`) and verified cutoff storage in TT.
    * Quiet pruning: Futility Pruning, Late Move Pruning (LMP), CounterMove/Followup pruning.
    * Tactical pruning: Capture Futility Pruning ($D \le 6$, `CaptureFutilityBase + CaptureFutilityPerDepth * depth`) and history-adjusted SEE margin.
    * Static Exchange Evaluation (SEE) pruning on quiet and noisy moves.
    * Singular Extensions ($D \ge 7$, TT move), double extensions, and multicut.
    * Check extensions and history extensions.
    * Late Move Reduction (LMR) table with adjustments for check, PV, improving, tactical TT moves (`ttCapture`), and history.
    * Post-LMR re-search depth adjustments: $+1$ ply when beating the previous best score by `LMRDeeperMargin`, $-1$ ply when barely scraping past by less than `LMRShallowerMargin`.
    * **Surprise-SRD:** Dynamic LMR adjustments based on sibling fail highs, eval deficit, and eval surplus.
  * [`Quiescence()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/search.c#L141): Stand-pat with 50-move deflation, delta pruning, and SEE-gated noisy move picking.
  * [`IterativeDeepening()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/search.c#L1135): Root iterative loop, Aspiration Windows, MultiPV support, soft time management (eval drop, best move flip instability via local `lastBestMoveDepth`, root effort), and single legal move cutoff.
  * Thread pool management: [`EnsureThreadPool()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/search.c#L1705) and [`FreeThreadPool()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/search.c#L1787). Thread-local node and tablebase hit counters written atomically and aggregated via `NodesSearchedThreadPool()` and `TbHitsThreadPool()` to prevent SMP cache-line contention.
* **[`pvtable.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/pvtable.h) / [`pvtable.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/pvtable.c):** Transposition Table with 4-entry buckets (64 bytes per bucket, exactly matching an x86 L1 cache line). Uses lockless SMP key packing:
  $$\text{key}_{32} = \text{posKey} \oplus (\text{posKey} \gg 32) \oplus \text{smp\_data} \oplus (\text{smp\_data} \gg 32)$$
  Features inlined high-throughput probing ([`ProbeTTEntry()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/pvtable.h#L52)) returning direct entry pointers without stack spills or 7-parameter pointer indirection, fast-path inlined mate/TB score scaling ([`valueFromTT()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/pvtable.h#L26) and [`valueToTT()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/pvtable.h#L36)), a cached `hashMask` in `S_PVTABLE` avoiding per-probe decrements, and L1 cache hardware prefetching ([`prefetchTT()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/pvtable.h#L46)) triggered immediately after move execution.

### Syzygy Tablebases (Fathom Integration)
* **[`syzygy.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/syzygy.h) / [`syzygy.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/syzygy.c):** GOOB bridge to the Fathom tablebase library. Manages root probing (`TBProbeRoot`) to filter winning/drawing subsets and interior node probing (`TBProbeWDLSearch`).
* **[`tbprobe.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/tbprobe.h), [`tbprobe.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/tbprobe.c), [`tbconfig.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/tbconfig.h), [`tbchess.inc`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/tbchess.inc), [`stdendian.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/stdendian.h):** Fathom Syzygy probing engine (supports 3-4-5-6-7 men tablebases).

### System, Threading, Telemetry, and UCI
* **[`uci.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/uci.h) / [`uci.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/uci.c):** Universal Chess Interface loop. Parses commands (`position`, `go`, `setoption`, `ucinewgame`, `stop`, `ponderhit`, `evaluate`, `perft`, `trace`), formats search output, and computes Win-Draw-Loss (WDL) probabilities via a 3rd-order polynomial fit.
* **[`trace.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/trace.h) / [`trace.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/trace.c):** Comprehensive telemetry system tracking over 50 search counters (AB/QS nodes, TT hits/cutoffs, RFP, NMP, IIR, ProbCut, LMP, LMR, Surprise-SRD, and move ordering cutoff stages). Formats outputs in plain text and JSON.
* **[`perft.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/perft.h) / [`perft.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/perft.c):** Standard and bulk move generation testing (`perft`, `uperft`, and `perfttest` test suites).
* **[`cpu.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/cpu.h) / [`cpu.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/cpu.c):** Runtime CPU instruction set detection (POPCNT, BMI2, AVX2, AVX-512) for dynamic dispatch.
* **[`thread.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/thread.h), [`tinycthread.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/tinycthread.h) / [`tinycthread.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/tinycthread.c):** Cross-platform C11 threads emulation on POSIX `pthread` and Windows Win32 thread APIs.
* **[`recog.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/recog.h):** Draw recognition used by the search ([`recog_draw()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/recog.h#L72)): fifty-move rule (when `useFiftyMoveRule` is on), Stockfish-style repetition (a 2-fold inside the search tree or any 3-fold), insufficient material (bare kings, a lone minor, two knights) and the wrong-coloured-bishop / rook-pawn fortress once the defending king reaches the promotion corner.
* **[`io.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/io.h) / [`io.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/io.c):** Formatting and parsing for moves, squares, and FEN strings.
* **[`misc.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/misc.h) / [`misc.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/misc.c):** Wall-clock timing (`getTimeMs()`).
* **[`some_maths.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/some_maths.h):** Utility math functions (`MIN`, `MAX`, `floorPowerOf2`).
* **[`validate.h`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/validate.h) / [`validate.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/validate.c):** Debug assertions for squares, moves, sides, and pieces.
* **[`main.c`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/main.c):** Entry point. Initializes subsystems via [`AllInit()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/init.c#L49), handles CLI arguments, and hands execution over to [`UCILoop()`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/uci.c#L598).

---

## Subdirectories & Assets

* **[`bin/linux/`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/bin/linux/):** Output directory for compiled Linux binaries (e.g., `GOOB-2.2-BETA-native`, `GOOB-2.2-BETA-universal`).
* **[`weights/`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/weights/):** Quantized NNUE weights. Not tracked in git (see `.gitignore`), but required to build:
  * `quantised.bin`: Production network, embedded into the binary at build time through `incbin` (`EVALFILE` in the makefile). `EvalFile` loads a different file at runtime.
  * `pknet.bin`: Left over from the earlier PKNet evaluation; no current code reads it.
* **`models/`:** An older release binary kept for comparison (untracked).
* **[`others/`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/others/):** Commit history logs and development test outputs.

---

## Compilation & Multi-ISA Build System

The build system is managed via [`src/makefile`](file:///home/gabriel/Desktop/ChessEngineGoobC/src/makefile), which provides dedicated ISA targets for different CPU microarchitectures:

| Target | Description | Recommended Usage |
| :--- | :--- | :--- |
| `make universal` | Multi-ISA binary with runtime CPU dispatch (SSE4.2 $\to$ AVX2 $\to$ AVX-512) | **Default distribution binary** |
| `make native` | Optimized specifically for the host CPU using `-march=native` | Local performance & benchmarking |
| `make x86-64-v4` | Requires AVX-512 (F, BW, VL, DQ) | High-end server / modern Intel & AMD Zen 4/5 |
| `make x86-64-v3` | Requires AVX2 + BMI2 (PEXT enabled) | Haswell / Zen 1 and newer |
| `make x86-64-v2` | Requires SSE4.2 + POPCNT | Nehalem and newer |
| `make x86-64` | Baseline 64-bit x86 (SSE2) | Legacy compatibility fallback |
| `make trace` | Builds `native` with `-DTRACE` for search telemetry | Use with `tools/trace_search.py` |

### Compilation Examples
```bash
# Build native binary for local testing:
make -C src native

# Build universal binary for distribution:
make -C src universal

# Build with search trace telemetry enabled:
make -C src trace

# Clean build artifacts:
make -C src clean
```

---

## UCI Protocol & Supported Options

GOOB communicates using standard UCI protocol commands (`uci`, `isready`, `ucinewgame`, `position`, `go`, `stop`, `quit`, `ponderhit`).

### Supported UCI Options
| Option Name | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `Threads` | spin | `1` (max 2048) | Number of search threads (Lazy SMP). |
| `Hash` | spin | `64` (max 1024) | Transposition Table size in megabytes. |
| `MultiPV` | spin | `1` (max 256) | Number of principal variation lines to output. |
| `Move Overhead` | spin | `50` (0–5000) | Buffer in milliseconds to absorb GUI/network lag. |
| `Clear Hash` | button | — | Clears the Transposition Table. |
| `Ponder` | check | `false` | Enables pondering during opponent's time. |
| `UCI_AnalyseMode` | check | `false` | Optimizes search behavior for analysis GUIs. |
| `UCI_LimitStrength` | check | `false` | Enables Elo rating capping. |
| `UCI_Elo` | spin | `2700` (1200–2700) | Target Elo when `UCI_LimitStrength` is active. |
| `BruteForceMode` | check | `false` | Disables prunings, reductions, and aspiration windows. |
| `useFiftyMoveRule` | check | `true` | Toggles the 50-move draw rule enforcement. |
| `EvalFile` | string | `<empty>` | Path to external NNUE `.bin` weights. |
| `SyzygyPath` | string | `<empty>` | Path to Syzygy 3-4-5-6-7 men `.rtbw` and `.rtbz` files. |
| `SyzygyProbeDepth` | spin | `1` (0–100) | Minimum search depth before querying Syzygy tablebases. |
| `Syzygy50MoveRule` | check | `true` | Enforces 50-move rule when probing Syzygy tables. |
| `SyzygyProbeLimit` | spin | `7` (0–7) | Maximum number of pieces for Syzygy probing. |
| `UCI_ShowWDL` | check | `false` | Output Win-Draw-Loss probabilities in search info lines. |
| `Surprise_SRD` | check | `true` | Enables dynamic LMR via sibling history and eval expectation. |

### Special Non-UCI Commands
* `print`: Displays an ASCII representation of the board, current FEN, posKey, and checkers.
* `eval` or `evaluate`: Prints the static NNUE evaluation and mirrored evaluation.
* `perft <depth>`: Runs move generation perft to the specified depth.
* `uperft <depth>`: Fast bulk perft test.
* `perfttest`: Runs the built-in perft test suite across multiple positions.
* `test` (typed before `uci`): Runs the transposition table bucket replacement unit tests.
* `trace <bench|print|json|reset>`: Controls the search telemetry tracking system.
* `compiler`: Prints compiler flags and detected host ISA capabilities.
* `bench [depth]`: As a command line argument (`GOOB-2.2-BETA-native bench 8`) or typed before `uci`: runs the 30-position fixed-depth benchmark (default depth 8) with the default UCI options. Its node count matches `trace bench` inside UCI mode.

---

## Design Details & Key Invariants

1. **King-Omitted Feature Indexing:**
   In standard HalfKP / HalfKA NNUE architectures, the king square is part of the feature tuple, meaning every king move invalidates the entire accumulator and requires an $\mathcal{O}(N)$ recomputation. In GOOB, features are indexed purely by `(color * 384 + piece_type * 64 + square)`. King moves are treated as simple 1-remove/1-add piece movements, keeping incremental updates strictly $\mathcal{O}(1)$.
2. **64-Byte Cache Alignment:**
   * Every Transposition Table bucket contains 4 entries of 16 bytes each ($4 \times 16 = 64$ bytes), matching the L1 cache line size of x86 processors.
   * `S_SEARCH_THREAD` and `NNUE_Accumulator` structures use `ALIGN64` to avoid false sharing across multiple CPU cores in Lazy SMP.
3. **Lockless Transposition Table:**
   Rather than using mutexes or spinlocks across threads, GOOB stores a 32-bit checksum combining both 32-bit halves of the board's `posKey` with the packed 64-bit entry data (`smp_data`). If another thread partially overwrites the slot during reading, the test key check fails safely.
4. **Surprise-SRD (Search Reduction Dynamics):**
   * *Sibling Surprise:* If an earlier reduced quiet move scores $> \alpha$, move ordering is volatile; reductions on subsequent siblings are decreased ($R -= 1$).
   * *Deficit Adjustment:* If static evaluation is severely behind $\alpha$ ($> 120\text{ cp}$ deficit), late quiet moves are reduced more aggressively ($R += 1$).
   * *Surplus Adjustment:* If static evaluation significantly exceeds $\alpha$, reductions on promising quiet moves are softened.
5. **LMR-Scaled Futility Pruning:**
   Instead of checking futility pruning conditions against the raw root `depth`, GOOB checks against the predicted `lmrDepth` (the depth after Late Move Reductions). This safely applies aggressive futility margins to moves deep in the move list which are already heavily reduced, generating significant node savings in wide branches.
