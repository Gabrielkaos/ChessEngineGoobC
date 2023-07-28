#ifndef HISTORY_H
#define HISTORY_H

#include "defs.h"

static const int HistoryMax = 400;
static const int HistoryMultiplier = 32;
static const int HistoryDivisor = 512;

extern int getCaptureHistory(S_BOARD *pos,int move);
extern void updateKillers(S_BOARD *pos,int move);
extern void updateHistories(S_BOARD *pos,int *moves,int length, int depth);
extern int getHistory(S_BOARD *pos,int move,int *fmhist,int *cmhist);
extern void updateCaptureHistory(S_BOARD *pos,int best,int *moves,int length,int depth);

#endif // HISTORY_H
