#ifndef MOVEPICKER_H
#define MOVEPICKER_H

#include "defs.h"
#include "board.h"



extern void initMovePicker(S_MOVEPICKER *mp, S_BOARD *pos, int ttMove);
extern void initSingularMovePicker(S_MOVEPICKER *mp, S_BOARD *pos, int ttMove);
extern void initNoisyMovePicker(S_MOVEPICKER *mp, int threshold);
extern int  selectNextMove(S_MOVEPICKER *mp, S_BOARD *pos, int skipQuiets);

#endif // MOVEPICKER_H