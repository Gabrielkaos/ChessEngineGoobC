
#include "defs.h"
#include "string.h"
#include "stdio.h"
#include <math.h>
#include "search.h"
#include "pvtable.h"
#include "some_maths.h"
#include "recog.h"
#include "inttypes.h"
#include "evaluate.h"
#include "bitboards.h"
#include "uci.h"
#include "movepicker.h"
#include "history.h"
#include "movegen.h"
#include "misc.h"
#include "makemove.h"
#include "io.h"
#include "attacks.h"
#include "thread.h"
#include "tinycthread.h"

//NOTE
/*
    Some Search code are based on Ethereal 12.75, big thanks credit to Andrew Grant
*/

//for threading
thrd_t workerThreads[MAXTHREADS];

int LMRTable[MAXDEPTH][MAXPOSMOVES];
// void initLMRTable(){
//     int i,j;
//     for(i=0;i<MAXDEPTH;++i){
//         for(j=0;j<MAXPOSMOVES;++j){
//             LMRTable[i][j]=0.75 + log(i) * log(j) / 2.25;
//         }
//     }
// }
void initLMRTable() {
    int i, j;
    for (i = 0; i < MAXDEPTH; ++i) {
        for (j = 0; j < MAXPOSMOVES; ++j) {
            LMRTable[i][j] = (int)(0.5 + log(i) * log(j) / 2.0);
        }
    }
    for (i = 0; i < MAXDEPTH; ++i) LMRTable[i][0] = 0;
    for (j = 0; j < MAXPOSMOVES; ++j) LMRTable[0][j] = 0;
}

//function for checking if we should stop early the search
INLINE void checkUp(S_SEARCHINFO *info){
    if(!info->UciInfinite && !info->ponder){
        if((!info->analyzeMode && info->EloNodeSet==TRUE && info->nodes>=info->EloNodelimit) ||
           (info->timeSet==TRUE && getTimeMs() > info->stoptime) ||
           (info->nodeSet==TRUE && info->nodes>=info->nodeLimit)){
                info->stopped=TRUE;
           }
    }
}

//initialize the search parameters and variables
INLINE void InitSearcher(S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table){
    int index=0;
    int index2=0;
    updateAge(table);

    for(index=0;index<2;++index){
        for(index2=0;index2<MAXDEPTH;++index2){
            pos->searchKillers[index][index2]=0;
        }
    }

    pos->ply=0;
    pos->seldepth=0;

    info->stopped=0;
    info->nodes=0ULL;

}


//protos
int Singularity(S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table, int threadNum,int ttValue,int depth,int beta,int ttMove,int *multiCut);
int StaticExchangeEvaluation(S_BOARD *pos,int move,int threshold);


//Quiescence search function to check if there are captures that can change the game
int Quiescence(int alpha,int beta,S_BOARD *pos,S_SEARCHINFO *info){

    int value,moveInLoop,moveNum;

    //check up for limits
    if((info->nodes & 2047)==0)checkUp(info);

    //update for uci
    info->nodes++;
    pos->seldepth=MAX(pos->seldepth,pos->ply);

    //if draw
    if(pos->ply){
        if(recog_draw(pos))return 1-(info->nodes & 2);
        if(pos->ply >= MAXDEPTH - 1)return EvalPosition(pos);
    }

    int ttMove  = NOMOVE;
    int ttValue = 0;
    int ttDepth = 0;
    int ttBound = HFNONE;
    int ttEval  = VALUE_NONE;
    int ttHit   = ProbeHashEntry(pos, pvTable, &ttMove, &ttValue, &ttDepth, &ttBound, &ttEval);

    if (ttHit) {
        ttValue = valueFromTT(ttValue, pos->ply);
        if (   ttBound == HFEXACT
            || (ttBound == HFALPHA && ttValue <= alpha)
            || (ttBound == HFBETA  && ttValue >= beta)) {
            return ttValue;
        }
    }

    int eval = pos->eval_stack[pos->ply] = (ttEval != VALUE_NONE) ? ttEval : EvalPosition(pos);


    //if the eval is good enough we return early
    if(eval>=beta)return beta;
    alpha=MAX(alpha,eval);



    S_MOVELIST list[1];
    GenerateAllNoisy(pos,list);
    InitAllScore(pos,list,NOMOVE,MAX(1,alpha-eval-QSSeeMargin));
    for(moveNum=0;moveNum<list->count;++moveNum){
        PickNextMove(moveNum,list);
        moveInLoop = list->moves[moveNum].move;


        if(!info->bruteForceMode){
            //SEE Pruning
            //if the score for this noisy move is lesser than zero we dont bother checking it
            if(list->moves[moveNum].score < 0){
                continue;
            }
            
            // negative history pruning
            int to       = TOSQ(moveInLoop);
            int from     = FROMSQ(moveInLoop);
            int pce      = pieceType[pos->pieces[from]];
            int captured = pieceType[pos->pieces[to]];
            if (moveInLoop & MVFLAGEP) captured = p_pawn;

            if (pos->chist[pce][to][captured] < -4000) continue;

            //DELTA PRUNING
            //to see if this capture has an effect
            //if the capture barely improves the position plus a margin
            //we dont bother checking the move
            if ((eval+SEEPieceValues[getCapturedPiece(moveInLoop)]+200<alpha) &&
                 PROMOTED(moveInLoop)==0 &&
                 getGamePhase(pos) < PHASE_ENDING){
                    continue;
            }
        }

        if(!makeMove(pos,moveInLoop))continue;
        value=-Quiescence(-beta,-alpha,pos,info);
        takeMove(pos);

        if(info->stopped==TRUE)return 0;

        if(value>alpha){
            if(value>=beta)return beta;
            alpha=value;
        }
    }
    return alpha;
}

//main search function alpha beta
int AlphaBeta(int alpha,int beta,int depth,S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table, int threadNum,int doNULL){

    int R,improving,quietMove,moveInLoop,newDepth,singular,extension,seeMargin[2];
    int fmhist          =0;
    int cmhist          =0;
    int multiCut        =FALSE;
    int value           =0;
    int Score           =-AB_BOUND;
    int pvNode          =(alpha != beta-1);
    int rootNode        =pos->ply==0;
    int quietsSeen      =0;
    int bestMove        =NOMOVE;
    int oldAlpha        =alpha;
    int moveNum         =0;
    int Legal           =0;
    int bestScore       =-AB_BOUND;
    int inCheck         =!!attackersToKingSq(pos,pos->side);
    int ttDepth         =0;
    int ttBound         =HFNONE;
    int ttValue         =0;
    int ttMove          =NOMOVE;
    int ttEval          =VALUE_NONE;
    int ttHit           =FALSE;
    int quietsPlayed    =0;
    int quietsTried[MAXPOSMOVES];
    int capturesPlayed  =0;
    int capturesTried[MAXPOSMOVES];
    int hist            =0;

    //go to qsearch if depth<=0
    //if in check dont go to q search
    if(depth<=0){
        if(!inCheck)return Quiescence(alpha,beta,pos,info);
        else depth = 1;
    }

    //see if we should abort the search
    if((info->nodes & 2047)==0)checkUp(info);

    //for uci updates
    pos->seldepth=rootNode ? 0 : MAX(pos->seldepth,pos->ply);
    info->nodes++;

    //if not rootNode check some things
    if(!rootNode){
        //see if board is drawn
        if(recog_draw(pos))return 1-(info->nodes & 2);
        //to deep
        if(pos->ply >= MAXDEPTH - 1)return EvalPosition(pos);
        //mate pruning
        int rAlpha = alpha > -AB_BOUND + pos->ply     ? alpha : -AB_BOUND + pos->ply;
        int rBeta  =  beta <  AB_BOUND - pos->ply - 1 ?  beta :  AB_BOUND - pos->ply - 1;
        if (rAlpha >= rBeta) return rAlpha;
    }

    //probing Transposition Table
    if((ttHit=ProbeHashEntry(pos, table, &ttMove, &ttValue, &ttDepth, &ttBound,&ttEval))){

        ttValue = valueFromTT(ttValue,pos->ply);

        if(ttDepth >= depth && (depth==0 || !pvNode)){
            if(    ttBound==HFEXACT
               || (ttBound==HFALPHA && ttValue <= alpha)
               || (ttBound==HFBETA  && ttValue >= beta)){
                return ttValue;
               }
        }

    }

    //store in eval_stack each staticEval
    // int staticEval=pos->eval_stack[pos->ply] = (ttEval != VALUE_NONE) ? ttEval:EvalPosition(pos);

    int rawEval    = (ttEval != VALUE_NONE) ? ttEval : EvalPosition(pos);
    int correction = getCorrectionHistory(pos) + getMaterialCorrection(pos);
    int staticEval = pos->eval_stack[pos->ply] = rawEval + correction;
    staticEval     = MAX(-AB_BOUND + 1, MIN(AB_BOUND - 1, staticEval));

    //see if we improved on the last position
    improving = !inCheck && pos->ply >= 2
         && (pos->eval_stack[pos->ply-2] == 0   // was in check, eval unreliable assume improving
             || staticEval > pos->eval_stack[pos->ply-2]);

    // seemargin for this depth
    seeMargin[0] = SEENoisyMargin * depth * depth;
    seeMargin[1] = SEEQuietMargin * depth;

    if(!info->bruteForceMode && !inCheck && !pvNode){

        //beta pruning
        //at shallow depth and when mate is unlikely
        //we prune aggresively
        //means that the position is good enough that no deeper search needed
        if (   depth <= 7
            && abs(beta) < ISMATE
            && staticEval - (70 * depth - 30 * improving) >= beta) {
            return staticEval - (70 * depth - 30 * improving);
        }

        // RAZORING
        // If we are very far below alpha at low depth, just drop into qsearch
        // Only useful at depth 1-3 where full search is cheap anyway
        if (!pvNode
            && !inCheck
            && depth <= 3
            && abs(alpha) < ISMATE
            && staticEval + 300 * depth < alpha) {
            int razorScore = Quiescence(alpha, beta, pos, info);
            if (razorScore <= alpha) return razorScore;
        }

        //null move
        //taking a null 'pseudo' move to see if the position has improved
        if(doNULL){

            //null move should not be done at root
            //if the position is favorable, we are likely to prune
            if(!rootNode &&
               staticEval >= beta &&
               depth >= defaultNullMoveDepth &&
               boardHasNonPawnMaterial(pos,pos->side) &&
               (!ttHit || !(ttBound == HFALPHA) || ttValue >= beta)){

                makeNullMove(pos);

                R = 4 + depth / 6 + MIN(3, (staticEval - beta) / 200);

                int valueNull=-AlphaBeta(-beta,-beta+1,depth-R,pos,info, table,threadNum,FALSE);
                takeNullMove(pos);
                if(info->stopped==TRUE)return 0;
                if (valueNull >= beta) {
                    // At very deep searches, verify with a full search (no null)
                    // to avoid being fooled by zugzwang positions
                    if (depth >= 14 && abs(valueNull) < ISMATE) {
                        int verifyScore = AlphaBeta(beta-1, beta, depth-R, pos, info, table, threadNum, FALSE);
                        if (verifyScore >= beta) return beta;
                    } else {
                        return beta;
                    }
                }
            }

            //razoring
            /*If the static evaluation plus some margin (pawn value) is still below beta,
            the engine performs quiescence search rather than a full search at shallow depths,
            potentially pruning bad lines early.*/
            Score=staticEval+SEEPieceValues[wP];
            int newScore;

            //checking if the score will likely exceed beta
            if(Score<beta){
                if(depth==1){
                    newScore=Quiescence(alpha,beta,pos,info);
                    return (newScore>Score) ? newScore:Score;
                }
            }

            //checking again for the value of a two pawn
            Score+=SEEPieceValues[wP];
            if(Score<beta && depth < 4){
                newScore=Quiescence(alpha,beta,pos,info);
                if(newScore<beta)return (newScore>Score) ? newScore:Score;
            }
        }
    }

    //PROBCUT
    //prune unlikely moves
    //if not bruteforce and not in princiap variation line
    //only in a threshold
    //dont prune moves if in a checkmate scenario to not miss a tactical sequence
    //only prune when the static eval is >= beta meaning we are winning so we can prune safely -
    //or static eval + move bestcase >= beta + margin
    if (!info->bruteForceMode &&
        !pvNode &&
        depth >=probCutDepth &&
        abs(beta) < ISMATE &&
        (staticEval>=beta || staticEval + MoveBestCaseValue(pos) >=beta + probCutMargin)){

            int rBeta = MIN(beta + probCutMargin, ISMATE - 1);
            int move_in_prob,move_num;
            //int probThresh = rBeta - staticEval;

            S_MOVELIST problist[1];
            GenerateAllNoisy(pos,problist);
            InitAllScore(pos,problist,NOMOVE,rBeta - staticEval);
            for(move_num=0;move_num<problist->count;++move_num){
                PickNextMove(move_num,problist);
                move_in_prob=problist->moves[move_num].move;


                //SEE Pruning
                if(problist->moves[move_num].score < 0){
                    continue;
                }

                if (!makeMove(pos,move_in_prob))continue;

                //perform a zero width search at ply 1 if the depth is higher than the threshold to quickly confirm
                //if it can exceed beta
                if(depth>=2*probCutDepth)value=-AlphaBeta(-rBeta,-rBeta+1,1,pos,info, table,threadNum,TRUE);
                //now at shallow depth perform a more deeper search to confirm
                if (depth<2*probCutDepth || value>=rBeta)value=-AlphaBeta(-rBeta,-rBeta+1,depth-4,pos,info, table,threadNum,TRUE);

                takeMove(pos);

                if(value>=rBeta)return value;

            }
    }

    //Internal Iterative Reduction(IIR)
    //dont over search a position with no Transposition table data
    //means this position might not be critical
    if(!info->bruteForceMode && depth>=4 && ttMove==NOMOVE)depth--;


    //generate the moves
    S_MOVELIST list[1];
    GenerateAllMoves(pos,list);
    InitAllScore(pos,list,ttMove,0);

    Score = -AB_BOUND;
    int skipQuiets = 0;

    //main move loop
    for(moveNum=0;moveNum<list->count;++moveNum){
        PickNextMove(moveNum,list);
        moveInLoop=list->moves[moveNum].move;

        if (rootNode && info->multiPVNumExcluded > 0) {
            int isExcluded = FALSE;
            for (int ex = 0; ex < info->multiPVNumExcluded; ex++) {
                if (moveInLoop == info->multiPVExcluded[ex]) {
                    isExcluded = TRUE;
                    break;
                }
            }
            if (isExcluded) continue;
        }

        //count the quiets seen and check if the move is tactical
        quietsSeen+=(quietMove=!moveIsTactical(pos,moveInLoop));

        //if tje skipQuiets flag is 1
        //and is quiet move
        //and move not a ttmove
        //then we skip that move
        if(skipQuiets
            && quietMove
            && moveInLoop != ttMove){
            continue;
        }

        //get history
        //get history of the move
        hist = !quietMove ? getCaptureHistory(pos,moveInLoop):getHistory(pos,moveInLoop,&fmhist,&cmhist);

        //Quiet late Move pruning
        if (!info->bruteForceMode && quietMove && bestScore > -ISMATE){

            //Futility pruning
            //checking if this position is likely to improve
            //if not then we skip it
            if (   depth <= FutilityPruningDepth
                && (staticEval + FutilityMargin) <= alpha
                && hist < FutilityPruningHistoryLimit[improving]){
                skipQuiets = 1;
                }

            //futile position if the static eval plus some margin is clearly worse then alpha
            //then we prune
            if (   depth <= FutilityPruningDepth
                && (staticEval + FutilityMargin + FutilityMarginNoHistory) <= alpha){
                skipQuiets = 1;
                }

            //if weve searched for quite a while Late moves that are quiet are pruned based on threshold
            if (depth<=LateMovePruningDepth &&
                quietsSeen>=LateMovePruningCounts[improving][depth]){
                    skipQuiets = 1;
                }

            //check the countermove and follow up moves
            //prune them if they have a low history performance
            R = LMRTable[MIN(depth, MAXDEPTH/2-1)][MIN(Legal, MAXDEPTH/2-1)];

            if ( list->moves[moveNum].score < SORT_COUNTER
                && cmhist < CounterMoveHistoryLimit[improving]
                && depth - R <= CounterMovePruningDepth[improving]){
                    continue;
                }

            if ( list->moves[moveNum].score < SORT_COUNTER
                && fmhist < FollowUpMoveHistoryLimit[improving]
                && depth - R <= FollowUpMovePruningDepth[improving]){
                    continue;
                }
        }

        //SEE
        //checks wether a capture move is valuable
        //if it actually gained material
        if (    !info->bruteForceMode
            &&  bestScore > -ISMATE
            && list->moves[moveNum].score < SORT_CAPTURE
            &&  depth <= SEEPruningDepth
            && !StaticExchangeEvaluation(pos, moveInLoop, seeMargin[quietMove])){
            continue;
        }

        if(!makeMove(pos,moveInLoop))continue;
        Legal++;

        //uci report the current move
        if(threadNum==0)if((getTimeMs()-info->starttime)>UciCurrMoveTime && rootNode)UciReportCurrentMove(depth,moveInLoop,Legal);


        //Singular Extensions and Multi Cut
        //decides whether the move is the only great move in the position
        //if the move being considered is the move probed in Transposition Table
        //then we search this line deeper
        if(!info->bruteForceMode){
            singular = !rootNode
                     &&  depth >= 8
                     &&  moveInLoop == ttMove
                     &&  ttDepth >= depth - 2
                     && (ttBound == HFBETA);

            //check if the move is singular or in check or quiet moves that performed based on history scores
            int singularResult = 0;
            if (singular) {
                singularResult = Singularity(pos, info, table, threadNum, ttValue, depth, beta, ttMove, &multiCut);
                // Double extension: move failed high in singular by a large margin = very singular
                extension = singularResult ? 1 : (ttValue >= beta + 20 ? 2 : 1);
            } else {
                extension = inCheck || (quietMove && pvNode && cmhist > HistexLimit && fmhist > HistexLimit);
            }

            newDepth = MIN(MAXDEPTH - 2, depth + (extension && !rootNode ? extension : 0));

            //MultiCut, super aggressive pruning
            //engine thinks that the move is too strong
            if(multiCut==TRUE){
                takeMove(pos);
                return MAX(ttValue-depth,-AB_BOUND);
            }
        }else{
            newDepth  = depth;
            extension = 0;
        }

        //Late Move Reduction, see how much we should cut depth
        //prunes if the move is quiet and that this is one of the many moves searched already
        //we prune if the move is unlikely promising
        if (quietMove && depth > 2 && Legal > 1 && !info->bruteForceMode){
            R = LMRTable[MIN(depth, MAXDEPTH/2-1)][MIN(Legal, MAXDEPTH/2-1)];

            R += !improving + !pvNode + extension;

            R += inCheck && pieceKing[pos->pieces[TOSQ(moveInLoop)]];

            R -= list->moves[moveNum].score >= SORT_COUNTER;

            R -= MAX(-2, MIN(2, hist / 5000));

            R = MIN(depth - 1, MAX(R, 1));
        }
        //for non quiet moves
        //we reduce search based on their performance history
        else if (!quietMove && depth > 2 && Legal > 1 && !info->bruteForceMode){
            R = MIN(depth - 1, MAX(1, MIN(3, 3 - (hist + 4000) / 2000)));
        }else{
            R = 1;
        }

        //LMR
        //see if the move can exceed current alpha
        //if not, no need to explore deeply
        if(R != 1) Score = -AlphaBeta(-alpha-1,-alpha,newDepth - R,pos,info, table,threadNum,TRUE);

        //PVS
        if((R != 1 && Score > alpha) || (R == 1 && !(pvNode && Legal == 1))){
            Score = -AlphaBeta(-alpha-1,-alpha,newDepth - 1,pos,info, table,threadNum,TRUE);
        }

        //Normal Search
        if(pvNode && (Legal == 1 || Score > alpha)){
            Score = -AlphaBeta(-beta,-alpha,newDepth - 1,pos,info, table,threadNum,TRUE);
        }


        takeMove(pos);
        if(quietMove)quietsTried[quietsPlayed++] = moveInLoop;
        else capturesTried[capturesPlayed++]     = moveInLoop;

        //see if we got a best move
        //update table
        //update history
        if(info->stopped==TRUE)return 0;
        if(Score>bestScore){
            bestScore=Score;
            bestMove=moveInLoop;
            if(Score>alpha){
                alpha=Score;
                if(alpha>=beta){
                    if(!moveIsTactical(pos,bestMove))updateHistories(pos,quietsTried,quietsPlayed,depth);
                    updateCaptureHistory(pos,bestMove,capturesTried,capturesPlayed,depth);
                    StoreHashEntry(pos, table, bestMove, beta, HFBETA, depth,staticEval);
                    return beta;
                }
            }
        }
    }

    //checkmate and stalemate
    if(Legal==0)return inCheck ? -AB_BOUND + pos->ply : 0;

    //update TT
    if(oldAlpha != alpha)StoreHashEntry(pos, table,bestMove,bestScore,HFEXACT,depth,staticEval);
    else                 StoreHashEntry(pos, table,bestMove,alpha    ,HFALPHA,depth,staticEval);

    // update correction history when we have a reliable score
    if (!inCheck && bestMove != NOMOVE && abs(bestScore) < ISMATE) {
        int diff = bestScore - rawEval;
        updateCorrectionHistory(pos, depth, diff);
        updateMaterialCorrection(pos, depth, diff);
    }

    return alpha;
}

//Singularity
//checks if the move is truly singular or the only best move in the position
//also checks if a stornger move if found(MULTICUT)
int Singularity(S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table, int threadNum,int ttValue,int depth,int beta,int ttMove,int *multiCut){

    int moveNum;
    int moveInLoop = NOMOVE;
    int skipQuiets = 0;
    int quiets     = 0;
    int tacticals  = 0;
    int value      = -AB_BOUND;
    int rBeta      = MAX(ttValue-depth,-AB_BOUND);
    int quietMove;

    //reverts the move
    takeMove(pos);

    //generate the moves
    S_MOVELIST singularList[1];
    GenerateAllMoves(pos,singularList);
    InitAllScore(pos,singularList,NOMOVE,0);

    for(moveNum=0;moveNum<singularList->count;++moveNum){
        PickNextMove(moveNum,singularList);
        moveInLoop = singularList->moves[moveNum].move;

        quietMove = !moveIsTactical(pos,moveInLoop);

        //skipQuiets if too many quiet moves has been seen
        if(skipQuiets
            && quietMove
            && moveInLoop != ttMove){
            continue;
        }

        //skip the pvMove
        if(moveInLoop==ttMove)continue;

        if(!makeMove(pos,moveInLoop))continue;
        value = -AlphaBeta(-rBeta-1,-rBeta,depth/2-1,pos,info, table,threadNum,TRUE);
        takeMove(pos);

        //if found a stronger move breaks, triggers MultiCut
        if(value>rBeta)break;

        quietMove ? tacticals++:quiets++;
        skipQuiets = quiets >= SingularQuietLimit;

        //break the loop if skip quiets and has seen too many tactical moves
        if(skipQuiets && tacticals >= SingularTacticalLimit)break;
    }

    //MultiCut
    //if found a stronger move than ttMove
    //we are confident that this is a strong move, we cut off every move and returns early
    if(value>rBeta && rBeta >=beta){
        if(moveInLoop != NOMOVE &&
           !moveIsTactical(pos,moveInLoop)){
                updateKillers(pos,moveInLoop);
        }
        *multiCut=TRUE;
    }

    //reapply
    if(!makeMove(pos,ttMove)){
        ASSERT(FALSE);
    }

    return value <= rBeta;
}

//SEE function
//verifies the move if it gains a value by capturing
int StaticExchangeEvaluation(S_BOARD *pos,int move,int threshold){
    int from, to, colour, balance, nextVictim, promoted , isEnpassant;
    U64 bishops, rooks, occupied, attackers, myAttackers;

    from        = FROMSQ(move);
    to          = TOSQ(move);
    promoted    = PROMOTED(move);
    isEnpassant = move & MVFLAGEP;

    nextVictim = promoted == 0 ? pos->pieces[from]  : promoted;

    balance = moveEstimatedValue(pos,move)-threshold;
    if(balance < 0)return 0;
    balance -= SEEPieceValues[nextVictim];
    if(balance >= 0)return 1;

    bishops = pos->bitboards[wB] | pos->bitboards[bB];
    rooks   = pos->bitboards[wR] | pos->bitboards[bR];

    occupied = pos->occupancy[BOTH];
    occupied = (occupied ^ (1ull << from)) | (1ull << to);
    if(isEnpassant) occupied ^= (1ull << pos->enPas);

    attackers = allAttackersToSquare(pos,occupied,to) & occupied;

    colour = !pos->side;

    while(TRUE){

        myAttackers = attackers & pos->occupancy[colour];
        if (myAttackers == 0ull) break;

        for (nextVictim = wP; nextVictim <= bK; nextVictim++){
            if(nextVictim==wK || nextVictim==bK)continue;
            if (myAttackers & pos->bitboards[nextVictim])break;
        }

        occupied ^= (1ull << LSBINDEX(myAttackers & pos->bitboards[nextVictim]));

        if (nextVictim == wP || nextVictim == bP || nextVictim == wB || nextVictim == bB || nextVictim == wQ || nextVictim == bQ){
            attackers |= get_bishop_attacks(to,occupied) & bishops;
        }

        if (nextVictim == wR || nextVictim == bR || nextVictim == wQ || nextVictim == bQ){
            attackers |=   get_rook_attacks(to, occupied) & rooks;
        }

        attackers &= occupied;

        colour = !colour;

        balance = -balance - 1 - SEEPieceValues[nextVictim];

        if (balance >= 0) {
            if ((nextVictim==wK || nextVictim==bK) && (attackers & pos->occupancy[colour])){
                colour = !colour;
            }
            break;
        }

    }

    return pos->side != colour;

}



int SearchPositionThread(void *data){
    THREAD_DATA *thread_data = (THREAD_DATA*)data;
    S_BOARD *pos = malloc(sizeof(S_BOARD));
    memcpy(pos, thread_data->originalPos,sizeof(S_BOARD));
    SearchPosition(pos, thread_data->info, thread_data->ttable);
    free(pos);
    free(thread_data);
    return 0;
}

//passing threads
//searches for each threads
void IterativeDeepening(THREAD_SEARCH_WORKER *workerthread) {

    int currentDepth, numberOfPvMoves, bestScore;
    S_BOARD     *pos    = workerthread->originalPos;
    S_SEARCHINFO *info  = workerthread->info;
    S_PVTABLE   *table  = workerthread->ttable;
    int threadNum       = workerthread->threadNumber;
    int lastBestMove    = NOMOVE;
    int bestMoveChanges = 0;

    workerthread->bestMove   = NOMOVE;
    workerthread->ponderMove = NOMOVE;

    int multiPV = MAX(1, info->multiPV);

    for (currentDepth = 1; currentDepth <= MAXDEPTH; ++currentDepth) {

        // --- MultiPV loop: search for each PV line ---
        int pvIdx;
        int numExcluded = 0;   // how many root moves excluded so far this depth
        int excludedMoves[256];// moves already found as best at this depth
        memset(excludedMoves, 0, sizeof(excludedMoves));

        for (pvIdx = 0; pvIdx < multiPV; pvIdx++) {

            // Restore excluded moves into pos so AlphaBeta skips them at root
            pos->multiPVMoves[pvIdx] = NOMOVE;  // sentinel
            // We track excluded moves in info for AlphaBeta to check at root
            info->multiPVExcluded    = excludedMoves;
            info->multiPVNumExcluded = numExcluded;

            int alpha = -AB_BOUND;
            int beta  =  AB_BOUND;

            // Aspiration windows only for first PV line and deeper depths
            if (pvIdx == 0 && currentDepth >= 5 && multiPV == 1) {
                alpha = MAX(-AB_BOUND, pos->multiPVScores[0] - ScoreWindow);
                beta  = MIN( AB_BOUND, pos->multiPVScores[0] + ScoreWindow);
            }

            while (TRUE) {
                bestScore = AlphaBeta(alpha, beta, currentDepth, pos, info, table, threadNum, TRUE);

                if (info->stopped) break;

                if (bestScore <= alpha) {
                    beta  = (alpha + beta) / 2;
                    alpha = MAX(-AB_BOUND, bestScore - ScoreWindow);
                } else if (bestScore >= beta) {
                    beta  = MIN(AB_BOUND, bestScore + ScoreWindow);
                } else {
                    break;
                }

                // Safety: fall back to full window if aspiration fails badly
                if (alpha <= -AB_BOUND + 1 || beta >= AB_BOUND - 1) break;
            }

            if (info->stopped) break;

            // Collect the PV line for this pvIdx
            numberOfPvMoves = getPvLine(currentDepth, pos, table);

            // Save this line's score and moves
            pos->multiPVScores[pvIdx]      = bestScore;
            pos->multiPVLineLengths[pvIdx] = numberOfPvMoves;
            for (int m = 0; m < numberOfPvMoves; m++) {
                pos->multiPVLines[pvIdx][m] = pos->pvArray[m];
            }

            // The first move of this PV is the pvIdx-th best root move
            if (numberOfPvMoves > 0) {
                excludedMoves[numExcluded++] = pos->pvArray[0];
            } else {
                break; // no move found, stop multiPV loop
            }

            // Report this PV line (thread 0 only)
            if (threadNum == 0) {
                UciReportMultiPV(info, table, pos, alpha, beta, bestScore,
                                 currentDepth, pvIdx,
                                 pos->multiPVLines[pvIdx],
                                 pos->multiPVLineLengths[pvIdx]);
                fflush(stdout);
            }
        }

        if (info->stopped) break;

        // Best move is always the first move of the first PV line
        if (threadNum == 0 && pos->multiPVLineLengths[0] > 0) {
            int newBest = pos->multiPVLines[0][0];
            if (newBest != lastBestMove && lastBestMove != NOMOVE) {
                bestMoveChanges++;
            }
            lastBestMove = newBest;
            workerthread->bestMove = newBest;
            workerthread->ponderMove = pos->multiPVLineLengths[0] > 1
                                    ? pos->multiPVLines[0][1] : NOMOVE;
        }

        // Clear excluded moves for next depth
        info->multiPVExcluded    = NULL;
        info->multiPVNumExcluded = 0;

        // Depth/mate limits (based on best line)
        if (!info->ponder && !info->UciInfinite) {
            if(info->timeSet && threadNum == 0){
                int now      = getTimeMs();
                int changes  = MIN(bestMoveChanges, 7);
                int scale    = SoftLimitBestMoveScale[changes];
                int scaledSoft = info->starttime + (info->softLimit - info->starttime) * scale / 10;
                scaledSoft   = MIN(scaledSoft, info->stoptime); // never exceed hard limit
                if (now >= scaledSoft) break; // clean break, no need to set stopped
            }

            if (info->depthSet && currentDepth >= info->depth) break;
            if (abs(pos->multiPVScores[0]) > ISMATE && workerthread->bestMove != NOMOVE) {
                int mateIn = (AB_BOUND - abs(pos->multiPVScores[0]) + 1) / 2;
                if (info->mateLimit != -1) {
                    if (mateIn <= info->mateLimit) break;
                } else {
                    if (currentDepth >= (mateIn * 2) + 10) break;
                }
            }
        }
    }
}


//call when creating workers
int startWorkerThreads(void *data){

    THREAD_SEARCH_WORKER *thread_data = (THREAD_SEARCH_WORKER*)data;
    //printf("Starting Thread:%d\n",thread_data->threadNumber);
    IterativeDeepening(thread_data);
    
    //printf("Ending Thread:%d\n",thread_data->threadNumber);
    if (thread_data->threadNumber==0){
        if(thread_data->info->setOptionPonder){
            if(thread_data->ponderMove != NOMOVE){
                printf("bestmove %s ",PrMove(thread_data->bestMove));
                printf("ponder %s\n",PrMove(thread_data->ponderMove));
            }else{
                printf("bestmove %s\n",PrMove(thread_data->bestMove));
            }
        }else{
            printf("bestmove %s\n",PrMove(thread_data->bestMove));
        }
        fflush(stdout);
    }
    free(thread_data->originalPos);
    free(thread_data);

}


//creates a data for the thread
//then starts the searching
void setupWorkers(int threadNum, thrd_t *workerthread, S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table){

    THREAD_SEARCH_WORKER *pThread = malloc(sizeof(THREAD_SEARCH_WORKER));

    pThread->originalPos = malloc(sizeof(S_BOARD));
    memcpy(pThread->originalPos,pos,sizeof(S_BOARD));

    pThread->info = info;
    pThread->ttable = table;

    pThread->threadNumber=threadNum;

    thrd_create(workerthread,&startWorkerThreads,(void*)pThread);

}



void SearchPosition(S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table){

    ASSERT(checkBoard(pos));

    int bestMove       = NOMOVE;
    int ponderMove     = NOMOVE;


    //init search things
    InitSearcher(pos,info, table);

    //setup the workers
    //create worker threads
    //then start
    for(int i=0;i<info->threadNum;++i){
        setupWorkers(i,&workerThreads[i],pos,info,table);
    }
    //setupWorkers(0,&workerThreads[0],pos,info,table);

    //if we finish
    
    for(int i=0;i<info->threadNum;++i){
        thrd_join(workerThreads[i],NULL);
    }

    
   
}
