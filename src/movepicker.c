#include "movepicker.h"
#include "search.h"
#include "attacks.h"
#include "makemove.h"
#include "history.h"
#include "movegen.h"

static const int MVVAugment[] = {1200, 2400, 2400, 4800, 9600, 19200};
static const int GoodQuietThreshold = -3000;

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
    mp->badNoisyCount = 0;
    mp->badNoisyIndex = 0;
    mp->noisySize   = 0;
    mp->quietSize   = 0;
    mp->split       = 0;

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

void initNoisyMovePicker(S_MOVEPICKER *mp, int threshold, int ttMove){
    mp->list->count = 0;
    mp->stage       = STAGE_TABLE;
    mp->tableMove = ttMove;
    mp->killer1 = mp->killer2 = mp->counter = NOMOVE;
    mp->threshold   = threshold;
    mp->type        = NOISY_PICKER;
    mp->badNoisyCount = 0;
    mp->badNoisyIndex = 0;
    mp->noisySize   = 0;
    mp->quietSize   = 0;
    mp->split       = 0;
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
            mp->threats = allAttackedSquares(pos, pos->side ^ 1);
            GenerateAllNoisy(pos, mp->list);
            for(int i = 0; i < mp->list->count; ++i){
                move = mp->list->moves[i].move;
                int to = TOSQ(move);
                int captured = pieceType[pos->pieces[to]];
                if(move & MVFLAGEP)   captured = p_pawn;
                else if((move & MVFLAGPROM) && pos->pieces[to] == EMPTY) captured = p_pawn;
                mp->list->moves[i].score = getCaptureHistory(pos, move, mp->threats) + MVVAugment[captured];
            }
            mp->split = mp->noisySize = mp->list->count;
            mp->stage = STAGE_GOOD_NOISY;
        }
        /* fallthrough */

        case STAGE_GOOD_NOISY:
            while(mp->noisySize){
                best = mp_getBestIndex(mp->list->moves, 0, mp->noisySize);
                move = mp_popMoveAt(mp->list->moves, 0, &mp->noisySize, best);

                if(move == mp->tableMove) continue;
                if(move == mp->killer1) mp->killer1 = NOMOVE;
                if(move == mp->killer2) mp->killer2 = NOMOVE;
                if(move == mp->counter) mp->counter = NOMOVE;

                if(!StaticExchangeEvaluation(pos, move, mp->threshold)){
                    mp->badNoisies[mp->badNoisyCount].move  = move;
                    mp->badNoisies[mp->badNoisyCount].score = getCaptureHistory(pos, move, mp->threats);
                    mp->badNoisyCount++;
                    continue;
                }

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
            if(!skipQuiets && mp->killer1 != mp->tableMove && !moveIsTactical(pos, mp->killer1) && moveIsPseudoLegal(pos, mp->killer1)){
                mp->lastStage = STAGE_KILLER_1;
                return mp->killer1;
            }
            /* fallthrough */

        case STAGE_KILLER_2:
            mp->stage = STAGE_COUNTER_MOVE;
            if(!skipQuiets && mp->killer2 != mp->tableMove && !moveIsTactical(pos, mp->killer2) && moveIsPseudoLegal(pos, mp->killer2)){
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
                && !moveIsTactical(pos, mp->counter)
                && moveIsPseudoLegal(pos, mp->counter)){
                mp->lastStage = STAGE_COUNTER_MOVE;
                return mp->counter;
            }
            /* fallthrough */

        case STAGE_GENERATE_QUIET:
            if(!skipQuiets){
                int fm, cm;
                
                U64 threatByLesser[6];
                int enemy = pos->side ^ 1;
                U64 enemyPawns   = pieces_cp(pos, enemy, PAWN);
                U64 enemyKnights = pieces_cp(pos, enemy, KNIGHT);
                U64 enemyBishops = pieces_cp(pos, enemy, BISHOP);
                U64 enemyRooks   = pieces_cp(pos, enemy, ROOK);
                U64 occ = pos->byTypeBB[ALL_PIECES];

                U64 pawnAttacks = pawnRightAttacks(enemyPawns, ~0ULL, enemy) | pawnLeftAttacks(enemyPawns, ~0ULL, enemy);
                
                U64 knightAttacks = 0ULL;
                U64 temp = enemyKnights;
                while(temp) { knightAttacks |= knight_attacks[LSBINDEX(temp)]; temp &= temp - 1; }
                
                U64 bishopAttacks = 0ULL;
                temp = enemyBishops;
                while(temp) { bishopAttacks |= get_bishop_attacks(LSBINDEX(temp), occ); temp &= temp - 1; }
                
                U64 rookAttacks = 0ULL;
                temp = enemyRooks;
                while(temp) { rookAttacks |= get_rook_attacks(LSBINDEX(temp), occ); temp &= temp - 1; }

                threatByLesser[p_pawn] = 0;
                threatByLesser[p_knight] = pawnAttacks;
                threatByLesser[p_bishop] = pawnAttacks;
                threatByLesser[p_rook]   = pawnAttacks | knightAttacks | bishopAttacks;
                threatByLesser[p_queen]  = threatByLesser[p_rook] | rookAttacks;
                threatByLesser[p_king]   = 0;

                U64 checkSquares[6];
                U64 enemyKingBB = pieces_cp(pos, enemy, KING);
                if(enemyKingBB){
                    int enemyKingSq = LSBINDEX(enemyKingBB);
                    U64 bishopChecks = get_bishop_attacks(enemyKingSq, occ);
                    U64 rookChecks   = get_rook_attacks(enemyKingSq, occ);
                    checkSquares[p_pawn]   = pawn_attacks[enemy][enemyKingSq];
                    checkSquares[p_knight] = knight_attacks[enemyKingSq];
                    checkSquares[p_bishop] = bishopChecks;
                    checkSquares[p_rook]   = rookChecks;
                    checkSquares[p_queen]  = bishopChecks | rookChecks;
                    checkSquares[p_king]   = 0ULL;
                } else {
                    for(int p = 0; p < 6; ++p) checkSquares[p] = 0ULL;
                }

                int startCount = mp->list->count; // == mp->split
                GenerateAllQuiet(pos, mp->list);   // appends
                mp->quietSize = mp->list->count - startCount;
                for(int i = mp->split; i < mp->list->count; ++i){
                    move = mp->list->moves[i].move;
                    int from = FROMSQ(move);
                    int to = TOSQ(move);
                    int pType = pieceType[pos->pieces[from]];
                    
                    //quiet score: butterfly + continuation histories plus the
                    //shared pawn-structure history (Stockfish: 2 * pawn_entry)
                    mp->list->moves[i].score = getHistory(pos, move, &fm, &cm, mp->threats)
                                            //  + getMainHistory(pos, move, mp->threats)
                                             + 2 * getPawnHistory(pos, move);

                    //low-ply history boost near the root, fading out with ply
                    //(Stockfish: += 8 * lowPlyHistory[ply][move] / (1 + ply))
                    if(pos->ply < LOWPLY_HIST_SLOTS)
                        mp->list->moves[i].score +=
                            8 * pos->search->lowPlyHistory[pos->ply][pType][to]
                              / (1 + pos->ply);
                              
                    // penalty for moving to a square threatened by a lesser piece
                    // or bonus for escaping an attack by a lesser piece.
                    int v = 20 * ((threatByLesser[pType] & (1ULL << from) ? 1 : 0) - 
                                  (threatByLesser[pType] & (1ULL << to) ? 1 : 0));
                    int piece_value_lookup = (pos->side == WHITE) ? (wP + pType) : (bP + pType);
                    mp->list->moves[i].score += SEEPieceValues[piece_value_lookup] * v;

                    // Direct check bonus: if the move gives direct check and doesn't blunder material
                    if((checkSquares[pType] & (1ULL << to)) && StaticExchangeEvaluation(pos, move, 0)){
                        mp->list->moves[i].score += 16384;
                    }
                }
            }
            mp->stage = STAGE_GOOD_QUIET;
            /* fallthrough */

        case STAGE_GOOD_QUIET:
            while(!skipQuiets && mp->quietSize){
                best = mp_getBestIndex(mp->list->moves, mp->split, mp->quietSize);
                if(mp->list->moves[best].score <= GoodQuietThreshold)
                    break;

                move = mp_popMoveAt(mp->list->moves, mp->split, &mp->quietSize, best);

                if(move == mp->tableMove || move == mp->killer1 ||
                   move == mp->killer2  || move == mp->counter)
                    continue;

                mp->lastStage = STAGE_GOOD_QUIET;
                return move;
            }

            mp->stage = STAGE_BAD_NOISY;
            /* fallthrough */

        case STAGE_BAD_NOISY:
            if(mp->type == NOISY_PICKER){
                mp->stage = STAGE_DONE;
                return NOMOVE;
            }

            // Sort bad noisies by material value (descending):
            // least-losing captures first to maximize cutoffs.
            // Guard: only sort on first entry (index == 0).
            if(mp->badNoisyIndex == 0){
                for(int i = 1; i < mp->badNoisyCount; i++){
                    S_MOVE key = mp->badNoisies[i];
                    int j = i - 1;
                    while(j >= 0 && mp->badNoisies[j].score < key.score){
                        mp->badNoisies[j + 1] = mp->badNoisies[j];
                        j--;
                    }
                    mp->badNoisies[j + 1] = key;
                }
            }

            while(mp->badNoisyIndex < mp->badNoisyCount){
                move = mp->badNoisies[mp->badNoisyIndex++].move;

                if(move == mp->tableMove || move == mp->killer1 ||
                   move == mp->killer2  || move == mp->counter)
                    continue;

                mp->lastStage = STAGE_BAD_NOISY;
                return move;
            }

            if(skipQuiets){
                mp->stage = STAGE_DONE;
                return NOMOVE;
            }

            mp->stage = STAGE_BAD_QUIET;
            /* fallthrough */

        case STAGE_BAD_QUIET:
            while(!skipQuiets && mp->quietSize){
                best = mp_getBestIndex(mp->list->moves, mp->split, mp->quietSize);
                move = mp_popMoveAt(mp->list->moves, mp->split, &mp->quietSize, best);

                if(move == mp->tableMove || move == mp->killer1 ||
                   move == mp->killer2  || move == mp->counter)
                    continue;

                mp->lastStage = STAGE_BAD_QUIET;
                return move;
            }

            mp->stage = STAGE_DONE;
            /* fallthrough */

        case STAGE_DONE:
        default:
            return NOMOVE;
    }
}