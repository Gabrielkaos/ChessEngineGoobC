#ifndef MOVEPICKER_H
#define MOVEPICKER_H

#include "defs.h"

static const int SORT_PV_MOVE = 5000000;
static const int SORT_CAPTURE = 3000000;
static const int SORT_KILLER0 = 2900000;
static const int SORT_KILLER1 = 2800000;
static const int SORT_COUNTER = 2700000;

extern void PickNextMove(int moveNum,S_MOVELIST *list);
extern void InitAllScore(S_BOARD *pos, S_MOVELIST *movelist, int ttMove, int threshold);

#endif // MOVEPICKER_H
