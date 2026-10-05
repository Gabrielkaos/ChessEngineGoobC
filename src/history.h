#ifndef HISTORY_H
#define HISTORY_H

#include "board.h"
#include "some_maths.h"

static const int HistoryMax = 16384;
static const int HistoryDivisor = 16384;

INLINE int stat_bonus(int depth) {
    return MIN(1708, 4 * depth * depth + 191 * depth - 118);
}

extern int getCaptureHistory(S_BOARD *pos,int move, U64 threats);
extern void updateKillers(S_BOARD *pos,int move);
extern void updateHistories(S_BOARD *pos,int *moves,int length, int depth);
extern void penalizeQuiets(S_BOARD *pos,int *moves,int length,int depth);
extern void bonusPriorQuiet(S_BOARD *pos,int depth);
extern int getHistory(S_BOARD *pos,int move,int *fmhist,int *cmhist, U64 threats);
extern void updateCaptureHistory(S_BOARD *pos,int best,int *moves,int length,int depth);

//pawn history: [pawn structure key][piece][to], shared across threads
extern int getPawnHistory(S_BOARD *pos,int move);

//low-ply history helpers (per-thread, cleared to 102 every search like Stockfish)
extern void clearLowPlyHistory(S_BOARD *pos);

#endif // HISTORY_H
