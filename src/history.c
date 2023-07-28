#include "history.h"
#include "some_maths.h"

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

void updateHistories(S_BOARD *pos,int *moves,int length, int depth){

    int bestMove = moves[length - 1];
    updateKillers(pos,bestMove);

    int cmMove  = pos->ply > 0 ? pos->moveStack[pos->ply - 1]:NOMOVE;
    int cmPiece = pos->pieceStack[pos->ply - 1];
    int cmTo    = TOSQ(cmMove);

    if (cmMove != NOMOVE && cmMove != NULLMOVE){
        pos->cmtable[!pos->side][cmPiece][cmTo] = bestMove;
    }

    if(!(length==1 && depth <= 3)){

        int index,bonus,entry,delta,move,piece,to,from;

        int fmMove  = pos->ply > 1 ? pos->moveStack[pos->ply - 2]:NOMOVE;
        int fmPiece = pos->pieceStack[pos->ply - 2];
        int fmTo    = TOSQ(fmMove);

        bonus = MIN(depth*depth,HistoryMax);

        for(index=0;index<length;++index){
            move = moves[index];

            delta = move==bestMove ? bonus:-bonus;

            piece = pieceType[pos->pieces[FROMSQ(move)]];
            from  = FROMSQ(move);
            to    = TOSQ(move);

            entry = pos->histtable[pos->side][from][to];
            entry += HistoryMultiplier * delta - entry * abs(delta) / HistoryDivisor;
            pos->histtable[pos->side][from][to] = entry;

            if(cmMove != NOMOVE && cmMove != NULLMOVE){
                entry = pos->continuation[0][cmPiece][cmTo][piece][to];
                entry += HistoryMultiplier * delta - entry * abs(delta) / HistoryDivisor;
                pos->continuation[0][cmPiece][cmTo][piece][to] = entry;
            }

            if(fmMove != NOMOVE && fmMove != NULLMOVE){
                entry = pos->continuation[1][fmPiece][fmTo][piece][to];
                entry += HistoryMultiplier * delta - entry * abs(delta) / HistoryDivisor;
                pos->continuation[1][fmPiece][fmTo][piece][to] = entry;
            }
        }

    }

}
