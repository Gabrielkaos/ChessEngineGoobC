#include "movepicker.h"
#include "search.h"
#include "makemove.h"
#include "history.h"
#include "movegen.h"

static const int MVVAugment[] = {0, 2400, 2400, 4800, 9600, 19200};

static int mp_getBestIndex(S_MOVE *moves, int lo, int count){
    int best = lo;
    for(int i = lo + 1; i < lo + count; ++i)
        if(moves[i].score > moves[best].score) best = i;
    return best;
}

static int mp_popMoveAt(S_MOVE *moves, int lo, int *size, int index){
    int popped = moves[index].move;
    moves[index] = moves[lo + --(*size)];
    return popped;
}

void initMovePicker(S_MOVEPICKER *mp, S_BOARD *pos, int ttMove){

    mp->list->count = 0;
    mp->stage       = STAGE_TABLE;
    mp->tableMove   = ttMove;
    mp->threshold   = 0;
    mp->type        = NORMAL_PICKER;

    int counter  = pos->ply > 0 ? pos->search->moveStack[pos->ply - 1] : NOMOVE;
    int cmPiece  = pos->ply > 0 ? pos->search->pieceStack[pos->ply - 1] : 0;
    int cmTo     = TOSQ(counter);

    mp->killer1 = pos->search->searchKillers[0][pos->ply];
    mp->killer2 = pos->search->searchKillers[1][pos->ply];
    mp->counter = (counter != NOMOVE && counter != NULLMOVE)
                ? pos->shared->cmtable[!pos->side][cmPiece][cmTo] : NOMOVE;
}

void initSingularMovePicker(S_MOVEPICKER *mp, S_BOARD *pos, int ttMove){
    initMovePicker(mp, pos, ttMove);
    mp->stage = STAGE_GENERATE_NOISY;   // skip offering ttMove a second time
}

void initNoisyMovePicker(S_MOVEPICKER *mp, int threshold){
    mp->list->count = 0;
    mp->stage       = STAGE_GENERATE_NOISY;
    mp->tableMove = mp->killer1 = mp->killer2 = mp->counter = NOMOVE;
    mp->threshold   = threshold;
    mp->type        = NOISY_PICKER;
}

int selectNextMove(S_MOVEPICKER *mp, S_BOARD *pos, int skipQuiets){

    int best, move;

    switch(mp->stage){

        case STAGE_TABLE:
            mp->stage = STAGE_GENERATE_NOISY;
            if(moveIsPseudoLegal(pos, mp->tableMove)){
                mp->lastStage = STAGE_TABLE;
                return mp->tableMove;
            }
            /* fallthrough */

        case STAGE_GENERATE_NOISY: {
            GenerateAllNoisy(pos, mp->list);
            for(int i = 0; i < mp->list->count; ++i){
                move = mp->list->moves[i].move;
                int to = TOSQ(move);
                int captured = pieceType[pos->pieces[to]];
                if(move & MVFLAGEP)   captured = p_pawn;
                if(move & MVFLAGPROM) captured = p_pawn;
                mp->list->moves[i].score = getCaptureHistory(pos, move) + MVVAugment[captured];
            }
            mp->split = mp->noisySize = mp->list->count;
            mp->stage = STAGE_GOOD_NOISY;
        }
        /* fallthrough */

        case STAGE_GOOD_NOISY:
            while(mp->noisySize){
                best = mp_getBestIndex(mp->list->moves, 0, mp->noisySize);

                if(mp->list->moves[best].score < 0) break; // rest are worse - defer to STAGE_BAD_NOISY

                if(!StaticExchangeEvaluation(pos, mp->list->moves[best].move, mp->threshold)){
                    mp->list->moves[best].score = -1;
                    continue;
                }

                move = mp_popMoveAt(mp->list->moves, 0, &mp->noisySize, best);

                if(move == mp->tableMove) continue;
                if(move == mp->killer1) mp->killer1 = NOMOVE;
                if(move == mp->killer2) mp->killer2 = NOMOVE;
                if(move == mp->counter) mp->counter = NOMOVE;

                mp->lastStage = STAGE_GOOD_NOISY;
                return move;
            }

            if(skipQuiets || mp->type == NOISY_PICKER){
                mp->stage = STAGE_BAD_NOISY;
                return selectNextMove(mp, pos, skipQuiets);
            }

            mp->stage = STAGE_KILLER_1;
            /* fallthrough */

        case STAGE_KILLER_1:
            mp->stage = STAGE_KILLER_2;
            if(!skipQuiets && mp->killer1 != mp->tableMove && moveIsPseudoLegal(pos, mp->killer1)){
                mp->lastStage = STAGE_KILLER_1;
                return mp->killer1;
            }
            /* fallthrough */

        case STAGE_KILLER_2:
            mp->stage = STAGE_COUNTER_MOVE;
            if(!skipQuiets && mp->killer2 != mp->tableMove && moveIsPseudoLegal(pos, mp->killer2)){
                mp->lastStage = STAGE_KILLER_2;
                return mp->killer2;
            }
            /* fallthrough */

        case STAGE_COUNTER_MOVE:
            mp->stage = STAGE_GENERATE_QUIET;
            if(!skipQuiets
                && mp->counter != mp->tableMove
                && mp->counter != mp->killer1
                && mp->counter != mp->killer2
                && moveIsPseudoLegal(pos, mp->counter)){
                mp->lastStage = STAGE_COUNTER_MOVE;
                return mp->counter;
            }
            /* fallthrough */

        case STAGE_GENERATE_QUIET:
            if(!skipQuiets){
                int fm, cm;
                int startCount = mp->list->count; // == mp->split
                GenerateAllQuiet(pos, mp->list);   // appends
                mp->quietSize = mp->list->count - startCount;
                for(int i = mp->split; i < mp->list->count; ++i){
                    move = mp->list->moves[i].move;
                    //quiet score: butterfly + continuation histories plus the
                    //shared pawn-structure history (Stockfish: 2 * pawn_entry)
                    mp->list->moves[i].score = getHistory(pos, move, &fm, &cm)
                                             + 2 * getPawnHistory(pos, move);

                    //low-ply history boost near the root, fading out with ply
                    //(Stockfish: += 8 * lowPlyHistory[ply][move] / (1 + ply))
                    if(pos->ply < LOWPLY_HIST_SLOTS)
                        mp->list->moves[i].score +=
                            8 * pos->search->lowPlyHistory[pos->ply][FROMSQ(move)][TOSQ(move)]
                              / (1 + pos->ply);
                }
            }
            mp->stage = STAGE_QUIET;
            /* fallthrough */

        case STAGE_QUIET:
            while(!skipQuiets && mp->quietSize){
                best = mp_getBestIndex(mp->list->moves, mp->split, mp->quietSize);
                move = mp_popMoveAt(mp->list->moves, mp->split, &mp->quietSize, best);

                if(move == mp->tableMove || move == mp->killer1 ||
                   move == mp->killer2  || move == mp->counter)
                    continue;

                mp->lastStage = STAGE_QUIET;
                return move;
            }

            mp->stage = STAGE_BAD_NOISY;
            /* fallthrough */

        case STAGE_BAD_NOISY:
            if(mp->noisySize && mp->type != NOISY_PICKER){
                move = mp_popMoveAt(mp->list->moves, 0, &mp->noisySize, 0);

                if(move == mp->tableMove || move == mp->killer1 ||
                   move == mp->killer2  || move == mp->counter)
                    return selectNextMove(mp, pos, skipQuiets);

                mp->lastStage = STAGE_BAD_NOISY;
                return move;
            }

            mp->stage = STAGE_DONE;
            /* fallthrough */

        case STAGE_DONE:
        default:
            return NOMOVE;
    }
}