#include "history.h"
#include "attacks.h"
#include "some_maths.h"
#include "init.h"
#include "bitboards.h"

//pkHash also hashes both kings; the pawn history wants the pawn structure
//alone (Stockfish keys it on pawn_key), otherwise every king step scatters
//what was learned into a fresh bucket. Strip the two king keys back out.
INLINE int pawnHistIndex(const S_BOARD *pos){
    U64 key = pos->st->pkHash
            ^ pieceKeys[wK][LSBINDEX(pieces_cp(pos, WHITE, KING))]
            ^ pieceKeys[bK][LSBINDEX(pieces_cp(pos, BLACK, KING))];
    return (int)(key & (PAWN_HIST_SIZE - 1));
}

//Stockfish's StatsEntry operator<<: clamp bonus to [-D, D], then apply the
//gravity formula entry += bonus - entry*|bonus|/D
INLINE void histGravityUpdate(int16_t *entry,int bonus,int D){
    int b = MIN(MAX(bonus, -D), D);
    *entry = (int16_t)(*entry + b - *entry * abs(b) / D);
}

int getPawnHistory(S_BOARD *pos,int move){
    const int to    = TOSQ(move);
    const int piece = pieceType[pos->pieces[FROMSQ(move)]];
    const int idx   = pawnHistIndex(pos);

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
    //no bonus from depth 0 nodes, see updateHistories
    if(depth <= 0) return;

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

    //depth 0 only happens at in-check nodes past the horizon, where
    //stat_bonus() turns negative (-118) and would reward the moves that
    //failed and punish the cutoff move. Those nodes update no histories
    //(Stockfish searches them in qsearch, which updates none either).
    if(depth > 0 && !(length==1 && depth <= 3)){

        U64 threats = allAttackedSquares(pos, pos->side ^ 1);
        int index,bonus,delta,move,piece,to,from,slot,back,pmove,ppiece,pto;

        bonus = stat_bonus(depth);

        for(index=0;index<length;++index){
            move = moves[index];

            delta = move==bestMove ? bonus:-bonus;

            piece = pieceType[pos->pieces[FROMSQ(move)]];
            from  = FROMSQ(move);
            to    = TOSQ(move);

            int threat_from = (threats & (1ULL << from)) ? 1 : 0;
            int threat_to   = (threats & (1ULL << to)) ? 1 : 0;

            histGravityUpdate(&pos->shared->histtable[pos->side][threat_from][threat_to][piece][to], delta, HistoryDivisor);

            if(pos->ply < LOWPLY_HIST_SLOTS)
                histGravityUpdate(&pos->search->lowPlyHistory[pos->ply][piece][to],
                                  delta * 712 / 1024, LOWPLY_HIST_MAX);

            {
                const int pIdx = pawnHistIndex(pos);
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
    }
}
