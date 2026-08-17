// correction.c
#include "board.h"
#include "string.h"

int correctionValue(const S_BOARD *pos){
    int index = pos->pkHash & (CORR_HIST_SIZE - 1);
    return pos->shared->pawnCorrHist[pos->side][index];
}

int correctedStaticEval(const S_BOARD *pos, int rawEval){
    int pawnCorr    = correctionValue(pos);          // existing pawn table
    int nonPawnCorr = nonPawnCorrectionValue(pos);    // new table

    int totalCorr = pawnCorr + nonPawnCorr;   // start with equal weighting — simplest first pass
    int adjusted = rawEval + totalCorr / CORR_HIST_SCALE;

    if(adjusted >  ISMATE-1) adjusted =  ISMATE-1;
    if(adjusted < -ISMATE+1) adjusted = -ISMATE+1;

    return adjusted;
}

void updateCorrectionHistory(S_BOARD *pos, int depth, int diff){
    int index = pos->pkHash & (CORR_HIST_SIZE - 1);
    int16_t *entry = &pos->shared->pawnCorrHist[pos->side][index];

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
    memset(pos->shared->pawnCorrHist, 0, sizeof(pos->shared->pawnCorrHist));
    memset(pos->shared->nonPawnCorrHist, 0, sizeof(pos->shared->nonPawnCorrHist));
}

static inline int nonPawnMaterialIndex(const S_BOARD *pos, int side){
    // cheap, non-incremental signature: piece counts by type for one
    // side, folded into a single index. Doesn't need to be a perfect
    // hash — collisions just mean shared correction slots between
    // similar material configurations, which is a reasonable
    // approximation for correction history's purpose.
    int n = COUNTBIT(pos->bitboards[side==WHITE?wN:bN])
          + 3*COUNTBIT(pos->bitboards[side==WHITE?wB:bB])
          + 9*COUNTBIT(pos->bitboards[side==WHITE?wR:bR])
          + 27*COUNTBIT(pos->bitboards[side==WHITE?wQ:bQ]);
    return n & (CORR_HIST_SIZE - 1);
}

int nonPawnCorrectionValue(const S_BOARD *pos){
    int index = nonPawnMaterialIndex(pos, pos->side);
    return pos->shared->nonPawnCorrHist[pos->side][index];
}

void updateNonPawnCorrectionHistory(S_BOARD *pos, int depth, int diff){
    int index = nonPawnMaterialIndex(pos, pos->side);
    int16_t *entry = &pos->shared->nonPawnCorrHist[pos->side][index];

    int bonus = diff * depth / 8;
    if(bonus >  CORR_HIST_MAX/4) bonus =  CORR_HIST_MAX/4;
    if(bonus < -CORR_HIST_MAX/4) bonus = -CORR_HIST_MAX/4;

    int updated = *entry + bonus - (*entry) * abs(bonus) / CORR_HIST_MAX;
    if(updated >  CORR_HIST_MAX) updated =  CORR_HIST_MAX;
    if(updated < -CORR_HIST_MAX) updated = -CORR_HIST_MAX;

    *entry = (int16_t)updated;
}