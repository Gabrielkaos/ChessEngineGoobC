#include "history.h"
#include "attacks.h"
#include "some_maths.h"

//Stockfish's StatsEntry operator<<: clamp bonus to [-D, D], then apply the
//gravity formula entry += bonus - entry*|bonus|/D
INLINE void histGravityUpdate(int16_t *entry,int bonus,int D){
    int b = MIN(MAX(bonus, -D), D);
    *entry = (int16_t)(*entry + b - *entry * abs(b) / D);
}

int getPawnHistory(S_BOARD *pos,int move){
    const int to    = TOSQ(move);
    const int piece = pieceType[pos->pieces[FROMSQ(move)]];
    const int idx   = pos->st->pkHash & (PAWN_HIST_SIZE - 1);

    return pos->shared->pawnHist[idx][piece][to];
}

void clearLowPlyHistory(S_BOARD *pos){
    for(int ply = 0; ply < LOWPLY_HIST_SLOTS; ++ply)
        for(int p = 0; p < 6; ++p)
            for(int to = 0; to < 64; ++to)
                pos->search->lowPlyHistory[ply][p][to] = 102;
}

int getCaptureHistory(S_BOARD *pos,int move, U64 threats){
    const int to   = TOSQ(move);
    const int from = FROMSQ(move);

    int piece = pieceType[pos->pieces[from]];
    int captured = pieceType[pos->pieces[to]];

    if (move & MVFLAGEP   ) captured = p_pawn;
    else if ((move & MVFLAGPROM) && pos->pieces[to] == EMPTY) captured = p_pawn;

    int threat_from = (threats & (1ULL << from)) ? 1 : 0;
    int threat_to   = (threats & (1ULL << to)) ? 1 : 0;

    return pos->shared->chist[piece][threat_from][threat_to][to][captured]
         + 64000 * (pieceType[PROMOTED(move)] == p_queen);
}

void updateCaptureHistory(S_BOARD *pos,int best,int *moves,int length,int depth){
    const int bonus = stat_bonus(depth);
    // Compute threats once per update
    U64 threats = allAttackedSquares(pos, pos->side ^ 1);

    int i,move,from,to,delta,piece,captured;

    for(i = 0;i<length;++i){
        move = moves[i];

        from = FROMSQ(move);
        to   = TOSQ(move);

        delta = move == best ? bonus:-bonus;

        piece    = pieceType[pos->pieces[from]];
        captured = pieceType[pos->pieces[to]];

        if(move & MVFLAGEP) captured = p_pawn;
        else if((move & MVFLAGPROM) && pos->pieces[to] == EMPTY) captured = p_pawn;

        ASSERT(piece >= p_pawn && piece <= p_king);
        ASSERT(captured >= p_pawn && captured < p_king);

        int threat_from = (threats & (1ULL << from)) ? 1 : 0;
        int threat_to   = (threats & (1ULL << to)) ? 1 : 0;

        histGravityUpdate(&pos->shared->chist[piece][threat_from][threat_to][to][captured], delta, HistoryDivisor);
    }
}

void updateKillers(S_BOARD *pos,int move){
    if(pos->search->searchKillers[0][pos->ply]==move)return;

    pos->search->searchKillers[1][pos->ply] = pos->search->searchKillers[0][pos->ply];
    pos->search->searchKillers[0][pos->ply] = move;
}

static const int ContinuationOffsets[CONT_HIST_SLOTS] = {1,2,4,6};

static int getContEntry(S_BOARD *pos,int slot,int piece,int to){
    const int back = ContinuationOffsets[slot];
    if(pos->ply < back)return 0;

    const int move = pos->search->moveStack[pos->ply - back];
    if(move==NOMOVE || move==NULLMOVE)return 0;

    return pos->shared->continuation[slot][pos->search->pieceStack[pos->ply - back]][TOSQ(move)][piece][to];
}

int getHistory(S_BOARD *pos,int move,int *fmhist,int *cmhist, U64 threats){

    int piece = pieceType[pos->pieces[FROMSQ(move)]];
    int to    = TOSQ(move);
    int from  = FROMSQ(move);

    int threat_from = (threats & (1ULL << from)) ? 1 : 0;
    int threat_to   = (threats & (1ULL << to)) ? 1 : 0;

    int cmMove  = pos->ply > 0 ? pos->search->moveStack[pos->ply - 1]:NOMOVE;
    int cmPiece = pos->ply > 0 ? pos->search->pieceStack[pos->ply - 1] : 0;
    int cmTo    = TOSQ(cmMove);

    int fmMove  = pos->ply > 1 ? pos->search->moveStack[pos->ply - 2]:NOMOVE;
    int fmPiece = pos->ply > 1 ? pos->search->pieceStack[pos->ply - 2] : 0;
    int fmTo    = TOSQ(fmMove);

    if(cmMove==NOMOVE || cmMove==NULLMOVE)*cmhist = 0;
    else *cmhist = pos->shared->continuation[0][cmPiece][cmTo][piece][to];

    if(fmMove==NOMOVE || fmMove==NULLMOVE)*fmhist = 0;
    else *fmhist = pos->shared->continuation[1][fmPiece][fmTo][piece][to];

    int total = *cmhist + *fmhist + pos->shared->histtable[pos->side][threat_from][threat_to][piece][to];

    for(int slot=2;slot<CONT_HIST_SLOTS;++slot)
        total += getContEntry(pos,slot,piece,to);

    return total;
}

//apply one gravity delta to every quiet-history table a move lives in:
//butterfly (threat-aware), low-ply, pawn-structure and the continuation slots
static void applyQuietHistoryDelta(S_BOARD *pos,int move,int delta,U64 threats){
    const int piece = pieceType[pos->pieces[FROMSQ(move)]];
    const int from  = FROMSQ(move);
    const int to    = TOSQ(move);
    int slot,back,pmove,ppiece,pto;

    const int threat_from = (threats & (1ULL << from)) ? 1 : 0;
    const int threat_to   = (threats & (1ULL << to)) ? 1 : 0;

    histGravityUpdate(&pos->shared->histtable[pos->side][threat_from][threat_to][piece][to], delta, HistoryDivisor);

    if(pos->ply < LOWPLY_HIST_SLOTS)
        histGravityUpdate(&pos->search->lowPlyHistory[pos->ply][piece][to],
                          delta * 712 / 1024, LOWPLY_HIST_MAX);

    {
        const int pIdx = pos->st->pkHash & (PAWN_HIST_SIZE - 1);
        const int pBonus = delta * (delta > 0 ? 1104 : 459) / 1024;
        histGravityUpdate(&pos->shared->pawnHist[pIdx][piece][to],
                          pBonus, PAWN_HIST_MAX);
    }

    for(slot=0;slot<CONT_HIST_SLOTS;++slot){
        back  = ContinuationOffsets[slot];
        if(pos->ply < back)continue;

        pmove = pos->search->moveStack[pos->ply - back];
        if(pmove==NOMOVE || pmove==NULLMOVE)continue;

        ppiece = pos->search->pieceStack[pos->ply - back];
        pto    = TOSQ(pmove);

        histGravityUpdate(&pos->shared->continuation[slot][ppiece][pto][piece][to], delta, HistoryDivisor);
    }
}

//a capture or promotion was best at this node: every quiet we tried before
//it was a waste, so push all of them down (Stockfish's quiet malus)
void penalizeQuiets(S_BOARD *pos,int *moves,int length,int depth){
    if(length==0)return;
    if(length==1 && depth <= 3)return;

    U64 threats = allAttackedSquares(pos, pos->side ^ 1);
    const int bonus = stat_bonus(depth);
    int index;

    for(index=0;index<length;++index)
        applyQuietHistoryDelta(pos,moves[index],-bonus,threats);
}

//this node failed low: the opponent's quiet move that led here refuted us,
//so reward it in the continuation tables (seen from the plies before it)
//and in the pawn-structure history (Stockfish's prior countermove bonus)
void bonusPriorQuiet(S_BOARD *pos,int depth){
    if(pos->ply < 1)return;

    const int m1 = pos->search->moveStack[pos->ply - 1];
    if(m1==NOMOVE || m1==NULLMOVE)return;
    if(pos->st->capturedPiece != EMPTY || PROMOTED(m1) != 0)return;

    const int pc1   = pos->search->pieceStack[pos->ply - 1];
    const int sq1   = TOSQ(m1);
    const int bonus = stat_bonus(depth);
    int slot,back,pmove,ppiece,pto;

    for(slot=0;slot<CONT_HIST_SLOTS;++slot){
        back = 1 + ContinuationOffsets[slot];
        if(pos->ply < back)continue;

        pmove = pos->search->moveStack[pos->ply - back];
        if(pmove==NOMOVE || pmove==NULLMOVE)continue;

        ppiece = pos->search->pieceStack[pos->ply - back];
        pto    = TOSQ(pmove);

        histGravityUpdate(&pos->shared->continuation[slot][ppiece][pto][pc1][sq1], bonus, HistoryDivisor);
    }

    //a quiet non-pawn move leaves the pawn structure untouched, but pkHash
    //also hashes the kings, so key by the parent position's hash
    if(pc1 != p_pawn && pos->st->previous != NULL){
        const int pIdx = pos->st->previous->pkHash & (PAWN_HIST_SIZE - 1);
        histGravityUpdate(&pos->shared->pawnHist[pIdx][pc1][sq1],
                          bonus * 1104 / 1024, PAWN_HIST_MAX);
    }
}

void updateHistories(S_BOARD *pos,int *moves,int length, int depth){

    int bestMove = moves[length - 1];
    updateKillers(pos,bestMove);

    int cmMove  = pos->ply > 0 ? pos->search->moveStack[pos->ply - 1]:NOMOVE;
    int cmPiece = pos->ply > 0 ? pos->search->pieceStack[pos->ply - 1] : 0;
    int cmTo    = TOSQ(cmMove);

    if (cmMove != NOMOVE && cmMove != NULLMOVE){
        pos->shared->cmtable[!pos->side][cmPiece][cmTo] = bestMove;
    }

    int fmMove  = pos->ply > 1 ? pos->search->moveStack[pos->ply - 2] : NOMOVE;
    int fmPiece = pos->ply > 1 ? pos->search->pieceStack[pos->ply - 2] : 0;
    int fmTo    = TOSQ(fmMove);

    if (fmMove != NOMOVE && fmMove != NULLMOVE){
        pos->shared->followupTable[pos->side][fmPiece][fmTo] = bestMove;
    }

    if(!(length==1 && depth <= 3)){

        U64 threats = allAttackedSquares(pos, pos->side ^ 1);
        const int bonus = stat_bonus(depth);
        int index;

        for(index=0;index<length;++index)
            applyQuietHistoryDelta(pos,moves[index],
                                   moves[index]==bestMove ? bonus : -bonus,threats);
    }
}
