# GOOB Chess Engine Research Laboratory

The **Goob Chess Engine Research Laboratory** is an interactive experimentation, telemetry, and tournament platform built around **ChessEngineGoobC (Goob)**.

---

## Capabilities

1. **Position Analysis & Interactive Explorer**
   - Interactive SVG chessboard with legal move highlighting, check indicators, board flipping, and move history.
   - Live UCI communication supporting fixed depth, fixed nodes, fixed movetime, or infinite search.
   - Multi-PV line display with centipawn/mate score formatting and dynamic Win/Draw/Loss probability bars.
   - Iterative deepening evaluation trajectory tracking (score vs. depth progression).
   - PGN game importer allowing ply-by-ply exploration and analysis of entire games.
   - Saved analysis archive backed by local SQLite storage.

2. **Search-Tree & Heuristics Visualizer**
   - Real-time diagnostic telemetry from Goob's trace engine (`make trace` build).
   - Beta cutoff distribution breakdown (1st move cutoff, TT move, Killer, Counter, Followup, Quiet, Tactical).
   - Search pruning & reduction health checks (RFP, NMP, Razoring, ProbCut, LMP, Futility, SEE, LMR, Surprise-SRD) with automatic status assessment against Goob's diagnostic thresholds.
   - Node allocation profiling (Alpha-Beta vs. Quiescence vs. Static Evaluations vs. NNUE Refreshes).

3. **Tournaments & Engine Benchmarks**
   - Automated Cutechess-cli head-to-head matches and SPRT (Sequential Probability Ratio Test) runs.
   - Live match monitor showing wins, losses, draws, score percentage, Elo difference with error bounds, and SPRT likelihood ratio bounds.
   - Live console output streaming via WebSockets.
   - PGN game export and download.

4. **SPSA Parameter Tuning**
   - Deep integration with `tools/spsa/spsa.py` and historical runs in `tools/spsa/runs/`.
   - Inspection of tunable search constants, default values in `src/tune.h`, tuned values, deltas, and valid ranges.
   - Parameter trajectory convergence charting across iterations.
   - One-click copy of cutechess-cli parameter strings for tournament testing.
   - Safe background launching and stopping with state-saving signals.

5. **Experiments Database & Side-by-Side Comparison**
   - Persistent SQLite database (`research_lab/data/lab.db`) recording match settings, SPRT results, SPSA tuning histories, and trace profiles.
   - Side-by-side comparison modal for diffing engine configurations and match outcomes.

---

## Quick Start

Launch the laboratory from the repository root:

```bash
# Using the default Python environment (auto-detects tools/.venv)
python3 run_lab.py

# Or specify host/port without opening a browser
python3 run_lab.py --port 8080 --no-browser
```

The web dashboard is available at `http://127.0.0.1:8080` (API documentation at `http://127.0.0.1:8080/docs`).

---

## Running Automated Tests

```bash
PYTHONPATH=. tools/.venv/bin/pytest research_lab/tests/
```

All 19 test cases verify:
- UCI engine process lifecycle and options discovery.
- Search info and Multi-PV parsing with SAN and WDL calculation.
- Search heuristic trace metric extraction and diagnostic status checks.
- SQLite CRUD for engines, saved analyses, and experiments.
- Cutechess-cli tournament log parsing and SPRT decision detection.
- SPSA state and parameter trajectory parsing.
- REST API validation, FEN legality, and PGN game playback.
