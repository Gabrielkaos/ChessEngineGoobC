# Syzygy tablebase support for GOOB

This adds Syzygy endgame tablebase probing to the engine, controlled entirely
through UCI options (`SyzygyPath`, `SyzygyProbeDepth`, `Syzygy50MoveRule`,
`SyzygyProbeLimit`) — the same names Stockfish uses, so any GUI that already
knows how to configure tablebases for Stockfish works unchanged here.

## What's in this drop

**New files (add these to your project/build):**
- `syzygy.c` / `syzygy.h` — the GOOB-specific wrapper: converts between
  `S_BOARD` and the probing calls, does root-move filtering, and does
  interior-node WDL cutoffs.
- `fathom/tbprobe.c`, `fathom/tbprobe.h`, `fathom/tbchess.c`,
  `fathom/tbconfig.h`, `fathom/stdendian.h` — [Fathom]
  (https://github.com/jdart1/Fathom), the probing library itself. Decoding
  the `.rtbw`/`.rtbz` file format correctly is a large, extremely
  fiddly piece of work (index compression, pair coding, DTZ-map decoding,
  etc.) — this is the same core every serious engine uses (Stockfish
  ships its own close relative of it), so it was pulled in rather than
  reimplemented. MIT licensed, copyright headers preserved in each file.
  **Only `fathom/tbprobe.c` gets compiled** — it `#include`s `tbchess.c`
  itself, so don't add `tbchess.c` to your build as a second translation
  unit or you'll get duplicate-symbol link errors.

**Modified files (replace your copies with these, or diff and merge by hand):**
- `board.h` — three new fields on `S_BOARD` (`tbHit`, `tbRootMoveCount`,
  `tbRootMoves[]`) that carry the root-filtering result for one search.
- `defs.h` — one new field on `S_SEARCHINFO` (`tbhits`, reported in UCI
  `info` lines like every other UCI engine does).
- `search.c` — probes at the root once per `go` (`TBProbeRoot`, in
  `SearchPosition`), restricts the root move loop to the tablebase-approved
  subset (`AlphaBeta`), and does an interior-node WDL cutoff
  (`TBProbeWDLSearch`) once a node is small enough.
- `uci.c` — the four new `option name Syzygy...` lines in `uciPrint()`,
  their `setoption` handlers, and `tbhits` added to the `info` line.
- `main.c` — frees tablebase memory on shutdown.

Nothing else changed. `attacks.c`, `evaluate.c`, `movegen.c`, etc. are
untouched.

## Why so little translation code was needed

Fathom's plain-bitboard API happens to already match GOOB's own conventions
exactly:
- squares are numbered `a1=0 .. h8=63`, same as `defs.h`'s `A1..H8` enum,
  so `pos->enPas`/`FROMSQ`/`TOSQ` pass straight through with no remapping;
- Fathom's castling bits (`TB_CASTLING_K/Q/k/q` = `0x1/0x2/0x4/0x8`) are
  numerically identical to `defs.h`'s `WKCA/WQCA/BKCA/BQCA`, so
  `pos->castleRights` also passes straight through.

The only real translation is promotion-piece encoding when matching
Fathom's compact `TbMove` format back onto GOOB's own `MOVE()` encoding,
handled by `tbPromoFromPiece()` in `syzygy.c`.

## How the two probing paths work

**Root (`TBProbeRoot`, called once per `go`, before the depth loop starts):**
Ranks every legal root move by DTZ (falls back to WDL ranking if DTZ files
are missing for that material), then records the subset of moves that
*preserve the position's proven result* into `pos->tbRootMoves[]`.
`AlphaBeta`'s root move loop then only searches that subset. This means the
ordinary search — full depth, your real eval, all the pruning — still picks
whichever of the tablebase-approved moves looks practically best (fastest
mate, safest fortress, whatever), rather than blindly obeying DTZ's
sometimes-eccentric single suggestion. It was verified against real
Syzygy files during development: e.g. in a K+R vs K position where the rook
can walk into being captured, `TBProbeRoot` correctly excludes exactly
those three squares and keeps the other 14 legal moves.

**Interior nodes (`TBProbeWDLSearch`, called from inside `AlphaBeta`):**
Once a node's material is within `SyzygyProbeLimit` pieces and
`depth >= SyzygyProbeDepth`, a WDL probe gives an exact ground-truth score
and the node returns immediately without generating/searching moves.
One honest caveat: Fathom's plain WDL probe only answers when the
half-move (fifty-move) clock is exactly 0 at that node — it refuses
otherwise, since a "cursed win"/"blessed loss" classification can't be
trusted without knowing the true clock, and the safe convenience wrapper
just declines rather than risk it. In practice this still fires right
when a capture or pawn push carries the search into an endgame — exactly
the moment it's most valuable — but won't keep firing at every node deep
into a long reversible sequence within that endgame. Root filtering
(above) has no such restriction and is the one guaranteeing the engine
never *chooses* to throw away a proven win/draw.

## Building

Add to your compile step:
```
gcc ... your_existing_sources.c syzygy.c fathom/tbprobe.c -lpthread -lm ...
```
(`-lpthread` is presumably already there for `tinycthread.c`/threading;
`-lm` for `math.h` is presumably already there too.) No extra `-I` include
path is needed — `fathom/tbprobe.h` was adjusted to `#include "tbconfig.h"`
with quotes instead of `<tbconfig.h>` so it resolves as a plain sibling
file. Needs a C11-ish compiler (`stdatomic.h`, `stdbool.h`) — you're
already relying on C11 for `defs.h`'s `stdalign.h`, so this should be a
non-issue.

This was test-built and linked clean with `gcc -O2 -std=gnu11 -pthread`,
and exercised end-to-end against real Syzygy 3/4-piece tables (load,
root filtering, `go depth N` search, all engine UCI options) with no
crashes and correct results.

## Using it

```
setoption name SyzygyPath value /path/to/your/syzygy/files
setoption name SyzygyProbeDepth value 1        # default 1, matches Stockfish
setoption name Syzygy50MoveRule value true     # default true
setoption name SyzygyProbeLimit value 7        # default 7 (max piece count to probe)
```
`SyzygyPath` accepts a single directory, or an OS-style separated list of
directories (`;` on Windows, `:` elsewhere) exactly like Stockfish. Send
`setoption name SyzygyPath value <empty>` to disable again.

You'll need actual tablebase files, which aren't included here (the 3-4-5
piece sets alone run into several GB) — they're distributed by the usual
chess-programming community sources (e.g. `syzygy-tables.info`, or
`tablebase.lichess.ovh` for the human-readable mirror lichess itself
uses). Point `SyzygyPath` at wherever you download them to.

## A possible follow-up (not done here)

Interior-node scores currently sit just below `ISMATE` in GOOB's scoring
scale (`TB_WIN_VALUE = ISMATE - 1000` in `syzygy.c`), deliberately *below*
the range that `valueToTT`/`valueFromTT` rescale by ply — meaning a
tablebase-proven win is reported as a large centipawn score rather than a
"mate in N", and a TT entry storing that score keeps its exact magnitude if
reused at a different ply via transposition (never wrong in sign/outcome,
just occasionally a slightly stale distance-style number). Stockfish
instead layers TB scores into the same ply-rescaled range mate scores use.
Doing the same here is a reasonable future refinement but touches the
mate-score plumbing in a few more places, so it was left as the simpler,
still-correct version for this pass.
