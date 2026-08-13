// correction.c
#include "board.h"
#include "string.h"

int correctionValue(const S_BOARD *pos){
    int index = pos->pkHash % CORR_HIST_SIZE;
    return pos->pawnCorrHist[pos->side][index];
}

int correctedStaticEval(const S_BOARD *pos, int rawEval){
    int corr = correctionValue(pos);
    int adjusted = rawEval + corr / CORR_HIST_SCALE;

    // never let a correction push eval into mate-score range —
    // correction history should calibrate ordinary evals, not
    // manufacture false mate signals
    if(adjusted >  ISMATE-1) adjusted =  ISMATE-1;
    if(adjusted < -ISMATE+1) adjusted = -ISMATE+1;

    return adjusted;
}

void updateCorrectionHistory(S_BOARD *pos, int depth, int diff){
    int index = pos->pkHash % CORR_HIST_SIZE;
    int16_t *entry = &pos->pawnCorrHist[pos->side][index];

    int bonus = diff * depth / 8;
    if(bonus >  CORR_HIST_MAX/4) bonus =  CORR_HIST_MAX/4;
    if(bonus < -CORR_HIST_MAX/4) bonus = -CORR_HIST_MAX/4;

    //same gravity formula you already use in history.c
    int updated = *entry + bonus - (*entry) * abs(bonus) / CORR_HIST_MAX;
    if(updated >  CORR_HIST_MAX) updated =  CORR_HIST_MAX;
    if(updated < -CORR_HIST_MAX) updated = -CORR_HIST_MAX;

    *entry = (int16_t)updated;
}

void clearCorrectionHistory(S_BOARD *pos){
    memset(pos->pawnCorrHist, 0, sizeof(pos->pawnCorrHist));
}