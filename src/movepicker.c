#include "movepicker.h"
#include "search.h"
#include "attacks.h"
#include "makemove.h"
#include "history.h"
#include "movegen.h"

static const int MVVAugment[] = { 700, 3150, 3150, 4725, 9100, 9100 };
static const int GoodQuietThreshold = -3000;

static inline int piece_mvv_lva_val(int pt) {
    static const int vals[6] = { 100, 450, 450, 675, 1300, 1500 };
    return (pt >= 0 && pt <= 5) ? vals[pt] : 0;
}

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

static void computeMovePickerThreats(S_MOVEPICKER *mp, const S_BOARD *pos) {
    int enemy = pos->side ^ 1;
    U64 colorBB = pos->byColorBB[enemy];
    U64 occ = pos->byTypeBB[ALL_PIECES];

    // Pawns
    U64 pawns = colorBB & pos->byTypeBB[PAWN];
    U64 pawnAttacks = pawnRightAttacks(pawns, ~0ULL, enemy) | pawnLeftAttacks(pawns, ~0ULL, enemy);

    // Knights
    U64 knights = colorBB & pos->byTypeBB[KNIGHT];
    U64 knightAttacks = 0ULL;
    while(knights) {
        knightAttacks |= knight_attacks[LSBINDEX(knights)];
        knights &= knights - 1;
    }

    // Kings
    U64 kings = colorBB & pos->byTypeBB[KING];
    U64 kingAttacks = kings ? king_attacks[LSBINDEX(kings)] : 0ULL;

    // Bishops (excluding queens)
    U64 bishops = colorBB & pos->byTypeBB[BISHOP];
    U64 bishopAttacks = 0ULL;
    while(bishops) {
        bishopAttacks |= get_bishop_attacks(LSBINDEX(bishops), occ);
        bishops &= bishops - 1;
    }

    // Rooks (excluding queens)
    U64 rooks = colorBB & pos->byTypeBB[ROOK];
    U64 rookAttacks = 0ULL;
    while(rooks) {
        rookAttacks |= get_rook_attacks(LSBINDEX(rooks), occ);
        rooks &= rooks - 1;
    }

    // Queens
    U64 queens = colorBB & pos->byTypeBB[QUEEN];
    U64 queenAttacks = 0ULL;
    while(queens) {
        int qSq = LSBINDEX(queens);
        queenAttacks |= get_bishop_attacks(qSq, occ) | get_rook_attacks(qSq, occ);
        queens &= queens - 1;
    }

    U64 minorThreats = pawnAttacks | knightAttacks | bishopAttacks;
    U64 rookThreats  = minorThreats | rookAttacks;

    mp->threatByLesser[p_pawn]   = 0ULL;
    mp->threatByLesser[p_knight] = pawnAttacks;
    mp->threatByLesser[p_bishop] = pawnAttacks;
    mp->threatByLesser[p_rook]   = minorThreats;
    mp->threatByLesser[p_queen]  = rookThreats;
    mp->threatByLesser[p_king]   = 0ULL;

    mp->threats = rookThreats | queenAttacks | kingAttacks;
    mp->threatsComputed = 1;
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
    mp->threats     = 0ULL;
    mp->threatsComputed = 0;

    int counter  = pos->ply > 0 ? pos->search->moveStack[pos->ply - 1] : NOMOVE;
    int cmPiece  = pos->ply > 0 ? pos->search->pieceStack[pos->ply - 1] : 0;
    int cmTo     = TOSQ(counter);

    int followup = pos->ply > 1 ? pos->search->moveStack[pos->ply - 2] : NOMOVE;
    int fmPiece  = pos->ply > 1 ? pos->search->pieceStack[pos->ply - 2] : 0;
    int fmTo     = TOSQ(followup);

    mp->killer1 = pos->search->searchKillers[0][pos->ply];
    mp->killer2 = pos->search->searchKillers[1][pos->ply];
    mp->counter = (counter != NOMOVE && counter != NULLMOVE)
                ? pos->shared->cmtable[!pos->side][cmPiece][cmTo] : NOMOVE;
    mp->followup = (followup != NOMOVE && followup != NULLMOVE)
                 ? pos->shared->followupTable[pos->side][fmPiece][fmTo] : NOMOVE;
}

void initSingularMovePicker(S_MOVEPICKER *mp, S_BOARD *pos, int ttMove){
    initMovePicker(mp, pos, ttMove);
    mp->stage = STAGE_GENERATE_NOISY;   // skip offering ttMove a second time
}

void initNoisyMovePicker(S_MOVEPICKER *mp, int threshold, int ttMove){
    mp->list->count = 0;
    mp->stage       = STAGE_TABLE;
    mp->tableMove = ttMove;
    mp->killer1 = mp->killer2 = mp->counter = mp->followup = NOMOVE;
    mp->threshold   = threshold;
    mp->type        = NOISY_PICKER;
    mp->badNoisyCount = 0;
    mp->badNoisyIndex = 0;
    mp->noisySize   = 0;
    mp->quietSize   = 0;
    mp->split       = 0;
    mp->threats     = 0ULL;
    mp->threatsComputed = 0;
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
            if(mp->list->count > 0){
                computeMovePickerThreats(mp, pos);
                for(int i = 0; i < mp->list->count; ++i){
                    move = mp->list->moves[i].move;
                    int to = TOSQ(move);
                    int captured = pieceType[pos->pieces[to]];
                    if(move & MVFLAGEP)   captured = p_pawn;
                    else if((move & MVFLAGPROM) && pos->pieces[to] == EMPTY) captured = p_pawn;

                    int score = getCaptureHistory(pos, move, mp->threats) + MVVAugment[captured];
                    if(move & MVFLAGPROM){
                        int prom = pieceType[PROMOTED(move)];
                        if(prom == p_knight){
                            score += 15000;
                        } else if(prom == p_rook || prom == p_bishop){
                            score -= 50000;
                        }
                    }
                    mp->list->moves[i].score = score;
                }
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
                if(move == mp->followup) mp->followup = NOMOVE;

                if(mp->type == NOISY_PICKER){
                    TRACE_INC(pos, qs_see_attempted);
                }
                if(!StaticExchangeEvaluation(pos, move, mp->threshold)){
                    if(mp->type == NOISY_PICKER){
                        TRACE_INC(pos, qs_see_pruned);
                        continue;
                    }
                    int from = FROMSQ(move);
                    int to = TOSQ(move);
                    int pt = pieceType[pos->pieces[from]];
                    int captured = pieceType[pos->pieces[to]];
                    if(move & MVFLAGEP)   captured = p_pawn;
                    else if((move & MVFLAGPROM) && pos->pieces[to] == EMPTY) captured = p_pawn;

                    int chist = getCaptureHistory(pos, move, mp->threats);
                    int badScore;
                    if(move & MVFLAGPROM){
                        if(pieceType[PROMOTED(move)] == p_queen){
                            badScore = 10000 + (chist - 64000) / 16;
                        } else if(pieceType[PROMOTED(move)] == p_knight){
                            badScore = (piece_mvv_lva_val(captured) - piece_mvv_lva_val(pt)) * 16 + chist / 16;
                        } else {
                            badScore = -100000;
                        }
                    } else {
                        // Order least-losing captures first (e.g. RxN > BxP > RxP > QxR > QxP)
                        badScore = (piece_mvv_lva_val(captured) - piece_mvv_lva_val(pt)) * 16 + chist / 16;
                    }

                    if (mp->badNoisyCount < MAXPOSMOVES) {
                        mp->badNoisies[mp->badNoisyCount].move  = move;
                        mp->badNoisies[mp->badNoisyCount].score = badScore;
                        mp->badNoisyCount++;
                    }
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
            if(!skipQuiets
                && mp->killer2 != mp->tableMove
                && mp->killer2 != mp->killer1
                && !moveIsTactical(pos, mp->killer2)
                && moveIsPseudoLegal(pos, mp->killer2)){
                mp->lastStage = STAGE_KILLER_2;
                return mp->killer2;
            }
            /* fallthrough */

        case STAGE_COUNTER_MOVE:
            mp->stage = STAGE_FOLLOWUP_MOVE;
            if(!skipQuiets
                && mp->counter != NOMOVE
                && mp->counter != mp->tableMove
                && mp->counter != mp->killer1
                && mp->counter != mp->killer2
                && !moveIsTactical(pos, mp->counter)
                && moveIsPseudoLegal(pos, mp->counter)){
                mp->lastStage = STAGE_COUNTER_MOVE;
                return mp->counter;
            }
            /* fallthrough */

        case STAGE_FOLLOWUP_MOVE:
            mp->stage = STAGE_GENERATE_QUIET;
            if(!skipQuiets
                && mp->followup != NOMOVE
                && mp->followup != mp->tableMove
                && mp->followup != mp->killer1
                && mp->followup != mp->killer2
                && mp->followup != mp->counter
                && !moveIsTactical(pos, mp->followup)
                && moveIsPseudoLegal(pos, mp->followup)){
                mp->lastStage = STAGE_FOLLOWUP_MOVE;
                return mp->followup;
            }
            /* fallthrough */

        case STAGE_GENERATE_QUIET:
            if(!skipQuiets){
                int startCount = mp->list->count; // == mp->split
                GenerateAllQuiet(pos, mp->list);   // appends
                mp->quietSize = mp->list->count - startCount;

                if(mp->quietSize > 0){
                    if(!mp->threatsComputed){
                        computeMovePickerThreats(mp, pos);
                    }

                    int enemy = pos->side ^ 1;
                    U64 enemyKingBB = pieces_cp(pos, enemy, KING);
                    U64 checkSquares[6] = {0ULL};
                    if(enemyKingBB){
                        int enemyKingSq = LSBINDEX(enemyKingBB);
                        U64 occ = pos->byTypeBB[ALL_PIECES];
                        U64 myBishopsOrQueens = pieces_cp(pos, pos->side, BISHOP) | pieces_cp(pos, pos->side, QUEEN);
                        U64 myRooksOrQueens   = pieces_cp(pos, pos->side, ROOK)   | pieces_cp(pos, pos->side, QUEEN);
                        U64 bishopChecks = myBishopsOrQueens ? get_bishop_attacks(enemyKingSq, occ) : 0ULL;
                        U64 rookChecks   = myRooksOrQueens   ? get_rook_attacks(enemyKingSq, occ)   : 0ULL;
                        checkSquares[p_pawn]   = pawn_attacks[enemy][enemyKingSq];
                        checkSquares[p_knight] = knight_attacks[enemyKingSq];
                        checkSquares[p_bishop] = bishopChecks;
                        checkSquares[p_rook]   = rookChecks;
                        checkSquares[p_queen]  = bishopChecks | rookChecks;
                    }

                    int fm, cm;
                    int pawnIdx = pawnHistIndex(pos);
                    const int16_t (*pawnTable)[64] = pos->shared->pawnHist[pawnIdx];
                    const int16_t (*lowPlyTable)[64] = (pos->ply < LOWPLY_HIST_SLOTS) ? pos->search->lowPlyHistory[pos->ply] : NULL;
                    const int lowPlyDiv = 1 + pos->ply;
                    const int basePiece = (pos->side == WHITE) ? wP : bP;

                    for(int i = mp->split; i < mp->list->count; ++i){
                        move = mp->list->moves[i].move;
                        int from = FROMSQ(move);
                        int to = TOSQ(move);
                        int pType = pieceType[pos->pieces[from]];
                        
                        // quiet score: butterfly + continuation histories plus the
                        // shared pawn-structure history (Stockfish: 2 * pawn_entry)
                        mp->list->moves[i].score = getHistory(pos, move, &fm, &cm, mp->threats)
                                                 + 2 * pawnTable[pType][to];

                        // low-ply history boost near the root, fading out with ply
                        if(lowPlyTable)
                            mp->list->moves[i].score += 8 * lowPlyTable[pType][to] / lowPlyDiv;
                                  
                        // penalty for moving to a square threatened by a lesser piece
                        // or bonus for escaping an attack by a lesser piece.
                        int v = 20 * (((mp->threatByLesser[pType] & (1ULL << from)) ? 1 : 0) - 
                                      ((mp->threatByLesser[pType] & (1ULL << to))   ? 1 : 0));
                        mp->list->moves[i].score += SEEPieceValues[basePiece + pType] * v;

                        // Direct check bonus: if the move gives direct check and doesn't blunder material
                        if(checkSquares[pType] & (1ULL << to)){
                            if(!(mp->threats & (1ULL << to)) ||
                               (!(mp->threatByLesser[pType] & (1ULL << to)) && StaticExchangeEvaluation(pos, move, 0))){
                                mp->list->moves[i].score += 16384;
                            }
                        }
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
                   move == mp->killer2  || move == mp->counter ||
                   move == mp->followup)
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
                   move == mp->killer2  || move == mp->counter ||
                   move == mp->followup)
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
                   move == mp->killer2  || move == mp->counter ||
                   move == mp->followup)
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