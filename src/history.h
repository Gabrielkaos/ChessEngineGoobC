
#ifndef HISTORY_H
#define HISTORY_H

#include "board.h"

static const int HistoryMax = 400;
static const int HistoryMultiplier = 32;
static const int HistoryDivisor = 512;

extern int getCaptureHistory(S_BOARD *pos,int move);
extern void updateKillers(S_BOARD *pos,int move);
extern void updateHistories(S_BOARD *pos,int *moves,int length, int depth);
extern int getHistory(S_BOARD *pos,int move,int *fmhist,int *cmhist);
extern void updateCaptureHistory(S_BOARD *pos,int best,int *moves,int length,int depth);

void updateCorrectionHistory(S_BOARD *pos, int depth, int diff);
int  getCorrectionHistory(const S_BOARD *pos);

void   updateMaterialCorrection(S_BOARD *pos, int depth, int diff);
int    getMaterialCorrection(const S_BOARD *pos);

#endif // HISTORY_H
