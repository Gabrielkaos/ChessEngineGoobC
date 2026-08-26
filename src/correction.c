// correction.c
//
// Stockfish-style correction history: four tables blended into one
// evaluation adjustment, all indexed by incremental zobrist keys and
// updated with a gravity formula on fail highs.
//
//   pawn   : pawn-structure key            -> pawnCorrHist[side][idx]
//   minors : knights+bishops key           -> minorCorrHist[side][idx]
//   nonpawn: white material key            -> nonPawnCorrHist[WHITE][side][idx]
//            black material key            -> nonPawnCorrHist[BLACK][side][idx]
//
#include "board.h"
#include "string.h"

// relative table weights taken from Stockfish's correction_value():
// 15341*pawn + 10569*minor + 12906*(wnpcv + bnpcv), normalized so that
// pawn=128. Dividing by CORR_HIST_SCALE*128 keeps the existing per-entry
// gain of this engine's pawn table (entry/256 cp) unchanged.
#define CORR_PAWN_W      128
#define CORR_MINOR_W      88
#define CORR_NONPAWN_W   108

// per-table update weights from Stockfish's update_correction_history():
// pawn gets the raw bonus, minors 150/128, each non-pawn table 186/128
#define CORR_UP_PAWN_W    128
#define CORR_UP_MINOR_W   150
#define CORR_UP_NONPAWN_W 186

static void updateEntry(int16_t *entry, int depth, int diff, int weight){
    int bonus = diff * depth / 8;
    bonus = bonus * weight / 128;
    if(bonus >  CORR_HIST_MAX/4) bonus =  CORR_HIST_MAX/4;
    if(bonus < -CORR_HIST_MAX/4) bonus = -CORR_HIST_MAX/4;

    //same gravity formula you already use in history.c
    int updated = *entry + bonus - (*entry) * abs(bonus) / CORR_HIST_MAX;
    if(updated >  CORR_HIST_MAX) updated =  CORR_HIST_MAX;
    if(updated < -CORR_HIST_MAX) updated = -CORR_HIST_MAX;

    *entry = (int16_t)updated;
}

int correctedStaticEval(const S_BOARD *pos, int rawEval){
    int us     = pos->side;
    int idxPwn = pos->pkHash       & (CORR_HIST_SIZE - 1);
    int idxMin = pos->minorHash    & (CORR_HIST_SIZE - 1);
    int idxWnp = pos->npHash[WHITE] & (CORR_HIST_SIZE - 1);
    int idxBnp = pos->npHash[BLACK] & (CORR_HIST_SIZE - 1);

    int pawnCorr = pos->shared->pawnCorrHist[us][idxPwn];
    int minorCorr = pos->shared->minorCorrHist[us][idxMin];
    int wnpCorr = pos->shared->nonPawnCorrHist[WHITE][us][idxWnp];
    int bnpCorr = pos->shared->nonPawnCorrHist[BLACK][us][idxBnp];

    int totalCorr = CORR_PAWN_W    * pawnCorr
                  + CORR_MINOR_W   * minorCorr
                  + CORR_NONPAWN_W * (wnpCorr + bnpCorr);

    int adjusted = rawEval + totalCorr / (CORR_HIST_SCALE * 128);

    if(adjusted >  ISMATE-1) adjusted =  ISMATE-1;
    if(adjusted < -ISMATE+1) adjusted = -ISMATE+1;

    return adjusted;
}

void updateCorrectionHistory(S_BOARD *pos, int depth, int diff){
    int us     = pos->side;
    int idxPwn = pos->pkHash       & (CORR_HIST_SIZE - 1);
    int idxMin = pos->minorHash    & (CORR_HIST_SIZE - 1);
    int idxWnp = pos->npHash[WHITE] & (CORR_HIST_SIZE - 1);
    int idxBnp = pos->npHash[BLACK] & (CORR_HIST_SIZE - 1);

    updateEntry(&pos->shared->pawnCorrHist[us][idxPwn], depth, diff, CORR_UP_PAWN_W);
    updateEntry(&pos->shared->minorCorrHist[us][idxMin], depth, diff, CORR_UP_MINOR_W);
    updateEntry(&pos->shared->nonPawnCorrHist[WHITE][us][idxWnp], depth, diff, CORR_UP_NONPAWN_W);
    updateEntry(&pos->shared->nonPawnCorrHist[BLACK][us][idxBnp], depth, diff, CORR_UP_NONPAWN_W);
}

void clearCorrectionHistory(S_BOARD *pos){
    memset(pos->shared->pawnCorrHist, 0, sizeof(pos->shared->pawnCorrHist));
    memset(pos->shared->nonPawnCorrHist, 0, sizeof(pos->shared->nonPawnCorrHist));
    memset(pos->shared->minorCorrHist, 0, sizeof(pos->shared->minorCorrHist));
}
