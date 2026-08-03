#ifndef SYZYGY_H
#define SYZYGY_H

#include "board.h"

//CMK syzygy.h -- thin wrapper around the Fathom Syzygy probing library
//(fathom/tbprobe.c, https://github.com/jdart1/Fathom, MIT licensed) that
//translates between GOOB's S_BOARD representation and Fathom's plain
//bitboard-based probing API. See syzygy.c for the integration notes.

//UCI-controlled state, mirroring Stockfish's option names so any GUI
//that already knows how to drive Syzygy support just works unchanged.
extern int SyzygyEnabled;      //TRUE once a tablebase set has been loaded
extern int SyzygyProbeDepth;   //min remaining depth before probing mid-search
extern int Syzygy50MoveRule;   //treat cursed win/blessed loss as draws
extern int SyzygyProbeLimit;   //max total pieces on board to probe (<= largest loaded set)
extern int TBLargestMen;       //largest piece count covered by the loaded tables, 0 if none loaded

//(Re)load tablebases from `path` (a single directory, or an OS-specific
//list of directories separated by ';' on Windows / ':' elsewhere, exactly
//like Stockfish's SyzygyPath). Pass "" or "<empty>" to disable. Safe to
//call repeatedly (e.g. every "setoption name SyzygyPath value ...").
void TBInit(const char *path);

//Release any loaded tablebase resources (called from TBInit and at
//engine shutdown).
void TBFree(void);

//Root-only probe: ranks every legal move in `pos` by Syzygy DTZ (falling
//back to WDL if DTZ files are missing) and records the subset of moves
//that preserve the position's game-theoretic result into
//pos->tbRootMoves[]/pos->tbRootMoveCount, setting pos->tbHit=1. The
//ordinary search is then restricted to that subset (see search.c) so it
//still picks the practically-best move (fastest mate, safest fortress,
//etc.) rather than blindly obeying DTZ's sometimes-eccentric suggestion.
//Returns 1 if filtering was applied, 0 otherwise (leaves pos->tbHit=0,
//meaning search should consider all moves as usual).
int TBProbeRoot(S_BOARD *pos);

//Returns 1 if `move` is one of the tablebase-approved root moves recorded
//by a prior TBProbeRoot() call on this position (only meaningful when
//pos->tbHit is set).
int TBRootMoveAllowed(const S_BOARD *pos, int move);

//Interior-node probe used inside AlphaBeta: attempts a Syzygy WDL probe
//of `pos` (a no-op unless the position is within probing range -- see
//syzygy.c for the exact conditions) and, on success, writes a
//search-ready score (already ply-adjusted so faster wins/slower losses
//still get preferred by the search) to *scoreOut and returns 1.
//Returns 0 if the position could not be probed (too many pieces,
//castling rights still available, non-zero fifty-move counter, or no
//tablebase loaded).
int TBProbeWDLSearch(S_BOARD *pos, int ply, int *scoreOut);

#endif // SYZYGY_H
