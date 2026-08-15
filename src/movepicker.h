#ifndef MOVEPICKER_H
#define MOVEPICKER_H

#include "defs.h"
#include "board.h"

enum {
    STAGE_TABLE,
    STAGE_GENERATE_NOISY,
    STAGE_GOOD_NOISY,
    STAGE_KILLER_1,
    STAGE_KILLER_2,
    STAGE_COUNTER_MOVE,
    STAGE_GENERATE_QUIET,
    STAGE_QUIET,
    STAGE_BAD_NOISY,
    STAGE_DONE
};

enum { NORMAL_PICKER, NOISY_PICKER };

typedef struct {
    S_MOVELIST list[1];

    int split;      // list->moves[0..split) are noisy, [split..list->count) are quiet
    int noisySize;  // active un-popped count within [0, split)
    int quietSize;  // active un-popped count within [split, split+quietSize)

    int stage;
    int type;
    int threshold;

    int tableMove;
    int killer1;
    int killer2;
    int counter;

    int lastStage;  // which case in selectNextMove produced the most recently returned move
} S_MOVEPICKER;

extern void initMovePicker(S_MOVEPICKER *mp, S_BOARD *pos, int ttMove);
extern void initSingularMovePicker(S_MOVEPICKER *mp, S_BOARD *pos, int ttMove);
extern void initNoisyMovePicker(S_MOVEPICKER *mp, int threshold);
extern int  selectNextMove(S_MOVEPICKER *mp, S_BOARD *pos, int skipQuiets);

#endif // MOVEPICKER_H