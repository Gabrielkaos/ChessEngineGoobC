
#include "history.h"
#include "some_maths.h"


#define HISTORY_MAX 16384

static void updateEntry(int *entry, int bonus) {
    // gravity formula: new = old + bonus - old * abs(bonus) / HISTORY_MAX
    *entry += bonus - (*entry) * abs(bonus) / HISTORY_MAX;
}



void updateCorrectionHistory(S_BOARD *pos, int depth, int diff) {
    int idx   = pos->pkHash & (CORRECTION_HISTORY_SIZE - 1);
    int side  = pos->side;
    int bonus = MIN(diff * depth, CORRECTION_HISTORY_LIMIT);

    
    pos->corrHist[side][idx] =
        (pos->corrHist[side][idx] * (CORRECTION_HISTORY_GRAIN - 1) + bonus)
        / CORRECTION_HISTORY_GRAIN;

    
    pos->corrHist[side][idx] = MAX(-CORRECTION_HISTORY_LIMIT,
                               MIN( CORRECTION_HISTORY_LIMIT,
                                    pos->corrHist[side][idx]));
}


int getCorrectionHistory(const S_BOARD *pos) {
    int idx = pos->pkHash & (CORRECTION_HISTORY_SIZE - 1);
    return pos->corrHist[pos->side][idx] / CORRECTION_HISTORY_GRAIN;
}

int getCaptureHistory(S_BOARD *pos,int move){
    const int to   = TOSQ(move);
    const int from = FROMSQ(move);

    int piece = pieceType[pos->pieces[from]];
    int captured = pieceType[pos->pieces[to]];

    if (move & MVFLAGEP   ) captured = p_pawn;
    if (move & MVFLAGPROM   ) captured = p_pawn;

    return pos->chist[piece][to][captured]
         + 64000 * (pieceType[PROMOTED(move)] == p_queen);
}

void updateCaptureHistory(S_BOARD *pos,int best,int *moves,int length,int depth){
    const int bonus = MIN(depth*depth,HistoryMax);

    int i,move,from,to,delta,piece,captured,entry;

    for(i = 0;i<length;++i){
        move = moves[i];

        from = FROMSQ(move);
        to   = TOSQ(move);

        delta = move == best ? bonus:-bonus;

        piece    = pieceType[pos->pieces[from]];
        captured = pieceType[pos->pieces[to]];

        if(move & MVFLAGEP) captured = p_pawn;
        if(move & MVFLAGPROM) captured = p_pawn;

        ASSERT(piece >= p_pawn && piece <= p_king);
        ASSERT(captured >= p_pawn && captured < p_king);

        entry = pos->chist[piece][to][captured];
        entry += HistoryMultiplier * delta - entry * abs(delta) / HistoryDivisor;
        pos->chist[piece][to][captured] = entry;

    }

}

void updateKillers(S_BOARD *pos,int move){
    if(pos->searchKillers[0][pos->ply]==move)return;

    pos->searchKillers[1][pos->ply] = pos->searchKillers[0][pos->ply];
    pos->searchKillers[0][pos->ply] = move;
}

int getHistory(S_BOARD *pos,int move,int *fmhist,int *cmhist){

    int piece = pieceType[pos->pieces[FROMSQ(move)]];
    int to    = TOSQ(move);
    int from  = FROMSQ(move);

    int cmMove  = pos->ply > 0 ? pos->moveStack[pos->ply - 1]:NOMOVE;
    int cmPiece = pos->pieceStack[pos->ply - 1];
    int cmTo    = TOSQ(cmMove);

    int fmMove  = pos->ply > 1 ? pos->moveStack[pos->ply - 2]:NOMOVE;
    int fmPiece = pos->pieceStack[pos->ply - 2];
    int fmTo    = TOSQ(fmMove);

    if(cmMove==NOMOVE || cmMove==NULLMOVE)*cmhist = 0;
    else *cmhist = pos->continuation[0][cmPiece][cmTo][piece][to];

    if(fmMove==NOMOVE || fmMove==NULLMOVE)*fmhist = 0;
    else *fmhist = pos->continuation[1][fmPiece][fmTo][piece][to];

    return *fmhist + *cmhist + pos->histtable[pos->side][from][to];

}

void updateHistories(S_BOARD *pos, int *quietsTried, int quietsPlayed, int depth) {
    int bonus = MIN(depth * depth + 2 * depth, HISTORY_MAX);

    // reward best move
    int bestMove = quietsTried[quietsPlayed - 1];
    int from = FROMSQ(bestMove);
    int to   = TOSQ(bestMove);
    updateEntry(&pos->histtable[pos->side][from][to], bonus);

    // penalize all moves that came before (failed quiets)
    for (int i = 0; i < quietsPlayed - 1; i++) {
        int move = quietsTried[i];
        from = FROMSQ(move);
        to   = TOSQ(move);
        updateEntry(&pos->histtable[pos->side][from][to], -bonus);
    }
}
