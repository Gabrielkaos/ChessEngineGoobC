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
#include "tt_eval.h"
#include "syzygy.h"
#include "correction.h"
#include "nnue_loader.h"

//NOTE
/*
    Some Search code are based on Ethereal 12.75, big thanks credit to Andrew Grant
*/


int LMRTable[64][64];
void initLMRTable(){
    int i,j;
    for(i=1;i<64;++i){
        for(j=1;j<64;++j){
            LMRTable[i][j]=0.75 + log(i) * log(j) / 2.25;
        }
    }
}

//function for checking if we should stop early the search
INLINE void checkUp(S_SEARCHINFO *info){
    if(!info->UciInfinite && !info->ponder){
        int hitLimit = (!info->analyzeMode && info->EloNodeSet==TRUE && info->nodes>=info->EloNodelimit) ||
                       info->stopOnPonderhit                                       ||
                       (info->timeSet==TRUE && getTimeMs()>info->stoptime)         ||
                       (info->nodeSet==TRUE && info->nodes>=info->nodeLimit);
        if(!hitLimit)return;
        //normally we wait until one depth has finished so a move always exists,
        //but never let that guard keep us running past the hard stop time
        int grace = DepthOneGraceMs;
        if (info->timeSet) {
            int max_budget = info->stoptime - info->starttime;
            if (grace > max_budget / 2) {
                grace = max_budget / 2;
            }
        }
        if(!info->depthOneComplete &&
           !(info->timeSet==TRUE && getTimeMs()>info->stoptime+grace))return;
        info->stopped=TRUE;
    }

}

//initialize the search parameters and variables
INLINE void InitSearcher(S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table){
    int index=0;
    int index2=0;
    updateAge(table);

    for(index=0;index<2;++index){
        for(index2=0;index2<MAXDEPTH;++index2){
            pos->search->searchKillers[index][index2]=0;
        }
    }

    pos->ply=0;
    pos->seldepth=0;

    info->stopped=0;
    info->stopOnPonderhit=0;
    info->nodes=0ULL;
    info->tbhits=0ULL;
    pos->search->rootEffortCount = 0;

    info->depthOneComplete=FALSE; 

    pos->search->rootPvMove = NOMOVE;

    pos->nmpMinPly = 0;

    //low-ply history is refreshed every search (Stockfish fills it with 102)
    clearLowPlyHistory(pos);
}


//protos
int Singularity(S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table, int threadNum,int ttValue,int depth,int beta,int ttMove,int *multiCut, int cutNode, StateInfo *ttSt);
int StaticExchangeEvaluation(S_BOARD *pos,int move,int threshold);


int isExcludedRootMove(const S_BOARD *pos,int move){
    int i;
    for(i=0;i<pos->excludedRootMoveCount;++i){
        if(pos->excludedRootMoves[i]==move)return TRUE;
    }
    return FALSE;
}

void checkPvLegality(S_BOARD *pos, int *pvArray, int len) {
    StateInfo st[MAXDEPTH];
    for (int i = 0; i < len; i++) {
        int move = pvArray[i];
        if (!legal(pos, move)) {
            printf("ILLEGAL PV MOVE FOUND! move=%x\n", move);
            fflush(stdout);
            abort();
        }
        makeMove(pos, move, &st[i]);
    }
    for (int i = 0; i < len; i++) {
        takeMove(pos);
    }
}


static int isShuffling(S_BOARD *pos, int move){
    if(moveIsTactical(pos,move) || pos->st->fiftyMove < 10) return FALSE;
    if(pos->st->pliesFromNull < 6 || pos->ply < 20) return FALSE;
    if(pos->ply < 4) return FALSE;   // need moveStack[ply-2] and [ply-4] to exist

    int m2 = pos->search->moveStack[pos->ply-2];
    int m4 = pos->search->moveStack[pos->ply-4];
    if(m2==NOMOVE || m2==NULLMOVE || m4==NOMOVE || m4==NULLMOVE) return FALSE;

    return FROMSQ(move)==TOSQ(m2) && FROMSQ(m2)==TOSQ(m4);
}


//Quiescence search function to check if there are captures that can change the game

int Quiescence(int alpha,int beta,S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table){

    int value,moveInLoop,moveNum;
    int best;

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

    int ttMove=NOMOVE, ttValue=0, ttDepth=0, ttBound=HFNONE, ttEval=VALUE_NONE, ttHit;
    if((ttHit=ProbeHashEntry(pos, table, &ttMove, &ttValue, &ttDepth, &ttBound, &ttEval))){
        ttValue = valueFromTT(ttValue,pos->ply);
        if(pos->st->fiftyMove < 96){
            if(ttBound==HFEXACT || (ttBound==HFALPHA && ttValue<=alpha) || (ttBound==HFBETA && ttValue>=beta)){
                return ttValue;
            }
        }
    }

    //standing pat: save the static eval, then use it as our floor
    int rawEval = (ttEval != VALUE_NONE) ? ttEval : EvalPosition(pos);
    int eval = pos->search->eval_stack[pos->ply] = correctedStaticEval(pos,rawEval);
    best = eval;
    alpha = MAX(alpha, eval);
    if(alpha >= beta) return eval;

    //DELTA PRUNING
    //if even the best possible capture (or the DeltaMarginQ floor, whichever
    //is larger) can't close the gap to alpha, there's no point generating
    //or trying any capture here at all
    if(MAX(DeltaMarginQ, MoveBestCaseValue(pos)) < alpha - eval)
        return eval;

    if (ttMove != NOMOVE && !moveIsTactical(pos, ttMove)) ttMove = NOMOVE;

    S_MOVEPICKER *mp = &pos->search->movePickers[pos->ply];
    initNoisyMovePicker(mp, MAX(1,alpha-eval-QSSeeMargin), ttMove);

    while((moveInLoop = selectNextMove(mp,pos,FALSE)) != NOMOVE){

        if(!legal(pos, moveInLoop)) continue;
        StateInfo st;
        makeMove(pos, moveInLoop, &st);
        prefetchTT(table, pos->st->posKey);
        value=-Quiescence(-beta,-alpha,pos,info,table);
        takeMove(pos);

        if(info->stopped==TRUE)return 0;

        if(value>best){
            best = value;
            if(value>alpha){
                alpha=value;
            }
        }

        if(alpha>=beta)return best;
    }

    return best;
}

//main search function alpha beta
int AlphaBeta(int alpha,int beta,int depth,S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table, int threadNum,int doNULL, int cutNode, S_PVLINE *pv){

    int R,improving,quietMove,moveInLoop,newDepth,singular,extension,seeMargin[2];
    int fmhist          =0;
    int cmhist          =0;
    int multiCut        =FALSE;
    int value           =0;
    int Score           =-AB_BOUND;
    int pvNode          =(alpha != beta-1);
    int rootNode        =pos->ply==0;
    int allNode         =!(pvNode || cutNode);
    int quietsSeen      =0;
    int bestMove        =NOMOVE;
    int oldAlpha        =alpha;
    int Legal           =0;
    int bestScore       =-AB_BOUND;
    

    
    S_PVLINE lpv;
    lpv.count = 0;
    if (pv != NULL) pv->count = 0;
    int inCheck         =(pos->st->checkersBB != 0);
    int ttDepth         =0;
    int ttBound         =HFNONE;
    int ttValue         =0;
    int ttMove          =NOMOVE;
    int ttEval          =VALUE_NONE;
    int ttHit           =FALSE;
    int quietsPlayed    =0;
    int *quietsTried    = pos->search->quietsTried[pos->ply];
    int capturesPlayed  =0;
    int *capturesTried  = pos->search->capturesTried[pos->ply];
    int hist            =0;

    //go to qsearch if depth<=0 AND we are not in check.
    if(depth<=0 && !inCheck){
        return Quiescence(alpha,beta,pos,info, table);
    }

    //clamp depth to a non-negative value right after the qsearch
    depth = MAX(0, depth);

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

    pos->search->searchKillers[0][pos->ply+1] = NOMOVE;
    pos->search->searchKillers[1][pos->ply+1] = NOMOVE;

    //probing Transposition Table
    if((ttHit=ProbeHashEntry(pos, table, &ttMove, &ttValue, &ttDepth, &ttBound,&ttEval))){

        ttValue = valueFromTT(ttValue,pos->ply);

        if(ttDepth >= depth && (depth==0 || !pvNode) && pos->st->fiftyMove < 96){
            if(    ttBound==HFEXACT
               || (ttBound==HFALPHA && ttValue <= alpha)
               || (ttBound==HFBETA  && ttValue >= beta)){
                return ttValue;
               }
        }

    }

    //Syzygy interior-node probe
    if(!rootNode && SyzygyEnabled && depth >= SyzygyProbeDepth){
        int tbScore, tbBound;
        if(TBProbeWDLSearch(pos, pos->ply, &tbScore, &tbBound)){
            info->tbhits++;
            
            if (tbBound == HFEXACT
                || (tbBound == HFBETA && tbScore >= beta)
                || (tbBound == HFALPHA && tbScore <= alpha)) {
                StoreHashEntry(pos, table, NOMOVE, tbScore, tbBound, MAXDEPTH-1, tbScore);
                return tbScore;
            }

            if (tbBound == HFBETA) {
                if (tbScore > alpha) alpha = tbScore;
                if (tbScore > bestScore) bestScore = tbScore;
            } else if (tbBound == HFALPHA) {
                if (tbScore < beta) beta = tbScore;
            }
        }
    }

    //SMALL PROBCUT
    //cheap TT-only cutoff: if we already have a lower-bound entry from a
    //reasonably deep search that clears beta by a solid margin, trust it
    //without generating a single move
    
    // int smallProbCutBeta = beta + SmallProbCutMargin;
    // if(!rootNode &&
    //    ttHit &&
    //    (ttBound == HFBETA) &&
    //    ttDepth >= depth - 4 &&
    //    ttValue >= smallProbCutBeta &&
    //    abs(beta) < ISMATE &&
    //    abs(ttValue) < ISMATE){
    //     return smallProbCutBeta;
    // }



    //store in eval_stack each staticEval
    //history stays independent of how much correction was already
    //applied when this position was last stored
    int rawEval = (ttEval != VALUE_NONE) ? ttEval : EvalPosition(pos);

    //store in eval_stack each staticEval (corrected, used for all pruning)
    int staticEval = pos->search->eval_stack[pos->ply] =
        inCheck ? rawEval : correctedStaticEval(pos, rawEval);

    //see if we improved on the last position
    improving = (!inCheck && pos->ply >= 2) ? (staticEval > pos->search->eval_stack[pos->ply-2] || (pos->ply >= 4 && staticEval > pos->search->eval_stack[pos->ply-4])) : 1;


    //hindsight depth adjustment based on how much the parent reduced
    //to reach this node, and whether the position has kept getting
    //worse for the opponent since
    int priorReduction = pos->ply >= 1 ? pos->search->reduction_stack[pos->ply] : 0;
    int opponentWorsening = pos->ply >= 1 && staticEval > -pos->search->eval_stack[pos->ply-1];

    if(!info->bruteForceMode){
        if(priorReduction >= 3 && !opponentWorsening) depth++;
        if(priorReduction >= 2 && depth >= 2 && staticEval + pos->search->eval_stack[pos->ply-1] > HindsightMargin) depth--;
    }

    //RAZORING
    //if staticEval is far below alpha, a full search is very unlikely to
    //recover — verify with qsearch instead of expanding this node
    if(!info->bruteForceMode && !pvNode && !inCheck &&
    depth <= RazoringDepth &&
    staticEval < alpha - RazorMarginBase - RazorMarginCoeff * depth * depth){
        int r = Quiescence(alpha,beta,pos,info,table);
        if(r <= alpha) return r;
    }


    // seemargin for this depth
    seeMargin[0] = SEENoisyMargin * depth * depth;
    seeMargin[1] = SEEQuietMargin * depth;

    if(!info->bruteForceMode && !inCheck && !pvNode){

        //beta pruning
        //at shallow depth and when mate is unlikely
        //we prune aggresively
        //means that the position is good enough that no deeper search needed
        if(depth <= BetaPruningDepth && staticEval - BetaMargin*depth > beta){
            return staticEval;
        }

        //null move
        //taking a null 'pseudo' move to see if the position has improved
        if(doNULL){

            //null move should not be done at root
            //if the position is favorable, we are likely to prune
            if(!rootNode &&
                staticEval >= beta &&
                depth >= defaultNullMoveDepth &&
                pos->ply >= pos->nmpMinPly &&
                boardHasNonPawnMaterial(pos,pos->side) &&
                (pos->ply < 1 || pos->search->moveStack[pos->ply-1] != NULLMOVE) &&
                (pos->ply < 2 || pos->search->moveStack[pos->ply-2] != NULLMOVE) &&
                (!ttHit || !(ttBound == HFALPHA) || ttValue >= beta)){

                StateInfo nullSt;
                makeNullMove(pos, &nullSt);

                R = 4 + depth / 6 + MIN(3, (staticEval - beta) / 200);

                int valueNull=-AlphaBeta(-beta,-beta+1,depth-R,pos,info, table,threadNum,FALSE, FALSE, &lpv);
                takeNullMove(pos);
                if(info->stopped==TRUE)return 0;

                if(valueNull >= beta){
                    //don't trust an unproven mate/near-mate score from null move
                    if(abs(valueNull) >= ISMATE) return beta;

                    //at low depth or already inside a verification subtree,
                    //trust the null move result directly — not worth the
                    //extra search cost
                    if(pos->nmpMinPly > 0 || depth < NMPVerifyDepth) return beta;

                    //verification search: disable NMP until ply passes this
                    //threshold, to avoid a recursive false-positive, then
                    //confirm the cutoff holds without the null-move shortcut
                    pos->nmpMinPly = pos->ply + 3 * (depth - R) / 4;

                    int v = AlphaBeta(beta-1,beta,depth-R,pos,info,table,threadNum,FALSE, FALSE, &lpv);

                    pos->nmpMinPly = 0;

                    if(info->stopped==TRUE)return 0;
                    if(v >= beta) return beta;
                }
            }
        }
    }

    //IIR — reduce depth when no TT move is available.
    //All-nodes get a stronger reduction (-2) since they are least likely
    //to have a useful move from the TT.
    if(!info->bruteForceMode && depth>=IIRDepth && ttMove==NOMOVE)
        depth -= (1 + allNode);

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
            int move_in_prob;
            //int probThresh = rBeta - staticEval;

            S_MOVEPICKER *probmp = &pos->search->movePickers[pos->ply];
            initNoisyMovePicker(probmp, rBeta - staticEval, NOMOVE);
            while((move_in_prob = selectNextMove(probmp,pos,FALSE)) != NOMOVE){

                if (!legal(pos, move_in_prob)) continue;
                StateInfo probSt;
                makeMove(pos, move_in_prob, &probSt);
                prefetchTT(table, pos->st->posKey);

                //perform a zero width search at ply 1 if the depth is higher than the threshold to quickly confirm
                //if it can exceed beta
                if(depth>=2*probCutDepth)value=-AlphaBeta(-rBeta,-rBeta+1,1,pos,info, table,threadNum,TRUE, TRUE, &lpv);

                //now at shallow depth perform a more deeper search to confirm
                if (depth<2*probCutDepth || value>=rBeta)value=-AlphaBeta(-rBeta,-rBeta+1,depth-4,pos,info, table,threadNum,TRUE, TRUE, &lpv);

                takeMove(pos);
                if(info->stopped==TRUE)return 0;
                if(value>=rBeta)return value;

            }
    }

    //generate the moves
    S_MOVEPICKER *mp = &pos->search->movePickers[pos->ply];
    initMovePicker(mp, pos, ttMove);

    Score = -AB_BOUND;
    int skipQuiets = 0;

    //main move loop
    while((moveInLoop = selectNextMove(mp,pos,skipQuiets)) != NOMOVE){

        int isExempt      = (mp->lastStage==STAGE_TABLE || mp->lastStage==STAGE_GOOD_NOISY);
        int isRefutation  = (mp->lastStage==STAGE_KILLER_1 || mp->lastStage==STAGE_KILLER_2 || mp->lastStage==STAGE_COUNTER_MOVE);
        int isSpecial     = isExempt || isRefutation;

        //Syzygy root filtering
        if(rootNode && pos->tbHit && !TBRootMoveAllowed(pos,moveInLoop)){
            continue;
        }

        //MultiPV
        if(rootNode && pos->excludedRootMoveCount>0 && isExcludedRootMove(pos,moveInLoop)){
            continue;
        }

        //count the quiets seen and check if the move is tactical
        quietsSeen+=(quietMove=!moveIsTactical(pos,moveInLoop));

        //get history
        //get history of the move
        hist = !quietMove ? getCaptureHistory(pos,moveInLoop, mp->threats):getHistory(pos,moveInLoop,&fmhist,&cmhist, mp->threats);

        //pawn history: orthogonal signal based on pawn structure
        int pawnHist = quietMove ? getPawnHistory(pos, moveInLoop) : 0;

        //Quiet late Move pruning
        if (!info->bruteForceMode && quietMove && bestScore > -ISMATE){

            //Futility pruning
            //checking if this position is likely to improve
            //if not then we skip it
            if (   depth <= FutilityPruningDepth
                && (staticEval + FutilityMargin * depth + FutilityMarginNoHistory) <= alpha){
                    skipQuiets = 1;
                }

            if (   !skipQuiets
                && !isSpecial
                && depth <= FutilityPruningDepth
                && (staticEval + FutilityMargin * depth) <= alpha
                && hist < FutilityPruningHistoryLimit[improving]){
                    continue;
                }

            //if weve searched for quite a while Late moves that are quiet are pruned based on threshold
            if (depth<=LateMovePruningDepth &&
                quietsSeen>=LateMovePruningCounts[improving][depth]){
                    skipQuiets = 1;
                }

            //check the countermove and follow up moves
            //prune them if they have a low history performance
            R = LMRTable[MIN(depth, 63)][MIN(Legal, 63)];

            if ( !isSpecial
                && cmhist < CounterMoveHistoryLimit[improving]
                && depth - R <= CounterMovePruningDepth[improving]){
                    continue;
                }

            if ( !isSpecial
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
            && !isExempt
            &&  depth <= SEEPruningDepth
            && !StaticExchangeEvaluation(pos, moveInLoop, seeMargin[quietMove])){
            continue;
        }

        U64 nodesBeforeMove = info->nodes;
        if(!legal(pos, moveInLoop)) continue;
        StateInfo st;
        makeMove(pos, moveInLoop, &st);
        prefetchTT(table, pos->st->posKey);
        Legal++;

        //uci report the current move
        if(threadNum==0)if((getTimeMs()-info->starttime)>UciCurrMoveTime && rootNode)UciReportCurrentMove(depth,moveInLoop,Legal);


        //Singular Extensions and Multi Cut
        //decides whether the move is the only great move in the position
        //if the move being considered is the move probed in Transposition Table
        //then we search this line deeper
        if(!info->bruteForceMode){
            singular = !rootNode
                     &&  depth >= 7
                     &&  moveInLoop == ttMove
                     &&  ttDepth >= depth - 2
                     && (ttBound >= HFBETA)
                     && !isShuffling(pos, moveInLoop);

            //check if the move is singular or in check or quiet moves that performed based on history scores
            extension = singular ? Singularity(pos, info, table,threadNum,ttValue,depth,beta,ttMove, &multiCut, cutNode, &st)
                        :(inCheck || (quietMove && pvNode && cmhist > HistexLimit && fmhist > HistexLimit));

            newDepth = MIN(MAXDEPTH - 2,depth + (rootNode ? 0 : extension));

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
            R = LMRTable[MIN(depth, 63)][MIN(Legal, 63)];

            R += !improving + !pvNode + extension;

            R += inCheck && pieceKing[pos->pieces[TOSQ(moveInLoop)]];

            R -= isSpecial;

            R += cutNode;

            R -= MAX(-2, MIN(2, (hist + pawnHist) / 5000));

            //scale up reduction further at expected all-nodes, proportional to
            //existing R rather than a flat bump, so it doesn't dominate at low depth
            if(allNode) R += R * AllNodeScale / (256 * depth + AllNodeBase);

            R = MIN(depth - 1, MAX(R, 1));
        }
        //for non quiet moves
        //we reduce search based on their performance history (more granular)
        else if (!quietMove && depth > 2 && Legal > 1 && !info->bruteForceMode){
            R = LMRTable[MIN(depth, 63)][MIN(Legal, 63)];
            R += !pvNode;
            R -= MAX(-2, MIN(2, hist / 5000));
            R = MIN(depth - 1, MAX(R, 1));
        }else{
            R = 1;
        }

        //LMR
        //see if the move can exceed current alpha
        //if not, no need to explore deeply
        if(R != 1){
            pos->search->reduction_stack[pos->ply] = R;
            Score = -AlphaBeta(-alpha-1,-alpha,newDepth - R,pos,info, table,threadNum,TRUE, TRUE, &lpv);
            pos->search->reduction_stack[pos->ply] = 0;
        }

        //PVS
        if((R != 1 && Score > alpha) || (R == 1 && !(pvNode && Legal == 1))){
            Score = -AlphaBeta(-alpha-1,-alpha,newDepth - 1,pos,info, table,threadNum,TRUE, !cutNode, &lpv);
        }

        //Normal Search
        if(pvNode && (Legal == 1 || Score > alpha)){
            Score = -AlphaBeta(-beta,-alpha,newDepth - 1,pos,info, table,threadNum,TRUE, FALSE, &lpv);
        }


        takeMove(pos);
        if(rootNode){
            U64 spent = info->nodes - nodesBeforeMove;
            int fi;
            for(fi=0; fi<pos->search->rootEffortCount; ++fi)
                if(pos->search->rootEffortMove[fi]==moveInLoop) break;
            if(fi==pos->search->rootEffortCount && pos->search->rootEffortCount<MAXPOSMOVES){
                pos->search->rootEffortMove[fi]=moveInLoop;
                pos->search->rootEffortNodes[fi]=0;
                pos->search->rootEffortCount++;
            }
            if(fi<pos->search->rootEffortCount) pos->search->rootEffortNodes[fi]+=spent;
        }
        if(quietMove)quietsTried[quietsPlayed++] = moveInLoop;
        else capturesTried[capturesPlayed++]     = moveInLoop;

        if(info->stopped==TRUE)return 0;

        
        if(Score>bestScore){
            bestScore=Score;
            bestMove=moveInLoop;
            if(Score>alpha){
                alpha=Score;
                if (pv != NULL) {
                    pv->moves[0] = moveInLoop;
                    pv->count = 1 + lpv.count;
                    for (int i = 0; i < lpv.count; i++) {
                        pv->moves[i+1] = lpv.moves[i];
                    }
                }

                if(alpha>=beta)break;
            }
        }
    }

    //checkmate and stalemate
    if(Legal==0)return inCheck ? -AB_BOUND + pos->ply : 0;

    //soften fail-high scores toward beta — avoids overshoot noise,
    //skip this near mate scores where exact values matter
    // if(bestScore>=beta && abs(bestScore)<ISMATE && abs(alpha)<ISMATE){
    //     bestScore = (bestScore*depth + beta) / (depth+1);
    // }


    //update history counters on a fail high for a quiet move
    if(bestScore>=beta && !moveIsTactical(pos,bestMove))
        updateHistories(pos,quietsTried,quietsPlayed,depth);

    if(bestScore>=beta)
        updateCaptureHistory(pos,bestMove,capturesTried,capturesPlayed,depth);

    //ttMoveHistory: track how often the TT move actually turns out best,
    //as a trust signal for singular-extension margin scaling
    if(!pvNode){
        int bonus = (bestMove == ttMove) ? 918 : -747;
        int entry = pos->shared->ttMoveHistory;
        entry += bonus - entry * abs(bonus) / TTMoveHistoryMax;
        pos->shared->ttMoveHistory = entry;
    }

    //correction history update, only meaningful when staticEval was
    //actually used (not in check), and when the best move wasn't a
    //capture (captures move material, they don't tell you your eval
    //of the position's structure was wrong)
    if(!inCheck && (bestMove==NOMOVE || !moveIsTactical(pos,bestMove))){
        int diff = bestScore - staticEval;
        int alphaRaised = bestScore > oldAlpha;
        if((bestScore > staticEval) == alphaRaised){
            //updates pawn, minor and both non-pawn correction tables
            updateCorrectionHistory(pos, depth, diff);
        }
    }

    //update TT
    if(rootNode) pos->search->rootPvMove = bestMove;

    if(!rootNode || pos->currentPvNum==0){
        ttBound = bestScore>=beta    ? HFBETA
                : bestScore>oldAlpha ? HFEXACT : HFALPHA;
        StoreHashEntry(pos, table, bestMove, bestScore, ttBound, depth, rawEval);
    }

    return bestScore;
}

//Singularity
//checks if the move is truly singular or the only best move in the position
//also checks if a stornger move if found(MULTICUT)
int Singularity(S_BOARD *pos,S_SEARCHINFO *info, S_PVTABLE *table, int threadNum,int ttValue,int depth,int beta,int ttMove,int *multiCut, int cutNode, StateInfo *ttSt){

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
    S_MOVEPICKER *smp = &pos->search->singularMovePickers[pos->ply];
    initSingularMovePicker(smp, pos, ttMove);

    while((moveInLoop = selectNextMove(smp,pos,skipQuiets)) != NOMOVE){

        quietMove = !moveIsTactical(pos,moveInLoop);

        if(!legal(pos, moveInLoop)) continue;
        StateInfo singSt;
        makeMove(pos, moveInLoop, &singSt);
        prefetchTT(table, pos->st->posKey);
        value = -AlphaBeta(-rBeta-1,-rBeta,depth/2-1,pos,info, table,threadNum,TRUE, TRUE, NULL);
        takeMove(pos);
        if(info->stopped==TRUE)break;
        //if found a stronger move breaks, triggers MultiCut
        if(value>rBeta)break;

        quietMove ? quiets++ : tacticals++;
        skipQuiets = quiets >= SingularQuietLimit;

        //break the loop if skip quiets and has seen too many tactical moves
        if(skipQuiets && tacticals >= SingularTacticalLimit)break;
    }

    //MultiCut
    //if found a stronger move than ttMove
    //we are confident that this is a strong move, we cut off every move and returns early
    if(value>rBeta && rBeta >=beta && !info->stopped){
        if(moveInLoop != NOMOVE &&
           !moveIsTactical(pos,moveInLoop)){
                updateKillers(pos,moveInLoop);
        }
        *multiCut=TRUE;
        
    }

    //reapply
    makeMove(pos, ttMove, ttSt);

    if(*multiCut==TRUE)return 1;

    if(value <= rBeta){
        int extension = 1;

        int adjustedDoubleMargin = DoubleExtMargin - pos->shared->ttMoveHistory / TTMoveHistoryScale;

        if(value < rBeta - adjustedDoubleMargin) extension++;
        // if(value < rBeta - TripleExtMargin) extension++;
        return extension;
    }

    // Negative extensions
    // If other moves failed high over rBeta without the ttMove on a reduced search,
    // but we cannot do multi-cut because rBeta is lower than the original beta,
    // we do not know if the ttMove is singular or can do a multi-cut, so we reduce the
    // ttMove in favor of other moves based on some conditions:
    // If the ttMove is assumed to fail high over current beta or if we are on a cutNode
    if(ttValue >= beta || cutNode){
        return -3;
    }

    return 0;
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

    bishops = pos->byTypeBB[BISHOP];
    rooks   = pos->byTypeBB[ROOK];

    occupied = pos->byTypeBB[ALL_PIECES];
    occupied = (occupied ^ (1ull << from)) | (1ull << to);
    if(isEnpassant) {
        int epCapSq = (pos->side == WHITE) ? to - 8 : to + 8;
        occupied ^= (1ull << epCapSq);
    }

    attackers = allAttackersToSquare(pos,occupied,to) & occupied;

    colour = !pos->side;

    while(TRUE){

        myAttackers = attackers & pos->byColorBB[colour];
        if (myAttackers == 0ull) break;

        U64 bb = 0;
        int pt = PAWN;
        for (; pt <= KING; pt++){
            bb = myAttackers & pos->byTypeBB[pt];
            if (bb) break;
        }

        nextVictim = MAKE_PIECE(colour, pt);
        occupied ^= (1ull << LSBINDEX(bb));

        if (pt == PAWN || pt == BISHOP || pt == QUEEN){
            attackers |= get_bishop_attacks(to,occupied) & bishops;
        }

        if (pt == ROOK || pt == QUEEN){
            attackers |=   get_rook_attacks(to, occupied) & rooks;
        }

        attackers &= occupied;

        colour = !colour;

        balance = -balance - 1 - SEEPieceValues[nextVictim];

        if (balance >= 0) {
            if (pt == KING && (attackers & pos->byColorBB[colour])){
                colour = !colour;
            }
            break;
        }

    }

    return pos->side != colour;

}



static S_BOARD *launcherPos = NULL;

int SearchPositionThread(void *data){
    THREAD_DATA *thread_data = (THREAD_DATA*)data;
    if (!launcherPos) {
        launcherPos = malloc(sizeof(S_BOARD));
        launcherPos->search = alloc_search_thread();
    }
    S_SEARCH_THREAD *saved_search = launcherPos->search;
    memcpy(launcherPos, thread_data->originalPos, sizeof(S_BOARD));
    launcherPos->search = saved_search;

    launcherPos->stateTable[0].previous = NULL;
    for (int i = 1; i <= launcherPos->hisPly; i++) {
        launcherPos->stateTable[i].previous = &launcherPos->stateTable[i-1];
    }
    launcherPos->st = &launcherPos->stateTable[launcherPos->hisPly];

    memcpy(launcherPos->search, thread_data->originalPos->search, sizeof(S_SEARCH_THREAD));

    launcherPos->eTable->evalTable = threadEvalTable[0].evalTable;
    launcherPos->eTable->numEntries = threadEvalTable[0].numEntries;

    launcherPos->pawnKingTable->paTable = threadPawnTable[0].paTable;
    launcherPos->pawnKingTable->numEntries = threadPawnTable[0].numEntries;

    launcherPos->ply = 0;
    nnue_refresh_accumulator(launcherPos);

    SearchPosition(launcherPos, thread_data->info, thread_data->ttable);

    free(thread_data);
    return 0;
}

//MultiPV support
int countLegalRootMoves(S_BOARD *pos){
    S_MOVELIST list[1];
    GenerateAllMoves(pos,list);

    int legalCount=0, i;
    for(i=0;i<list->count;++i){
        int move=list->moves[i].move;
        if(pos->tbHit && !TBRootMoveAllowed(pos,move))continue;
        if(!legal(pos,move))continue;
        legalCount++;
    }
    return legalCount;
}

//passing threads
//searches for each threads
void IterativeDeepening(THREAD_SEARCH_WORKER *workerthread){

    S_SEARCHINFO *info   = workerthread->info;
    S_BOARD *pos         = workerthread->originalPos;
    S_PVTABLE *table     = workerthread->ttable;
    int threadNum        = workerthread->threadNumber;

    int currentDepth,numberOfPvMoves,bestScore;
    int pvNum;
    S_PVLINE rootPv;

    int prevBestMove        = NOMOVE;
    double bestMoveChanges  = 0.0;

    workerthread->bestMove       = NOMOVE;
    workerthread->ponderMove     = NOMOVE;

    if(threadNum==0){
        S_MOVELIST rootList[1];
        GenerateAllMoves(pos, rootList);
        int rmi;
        for(rmi=0;rmi<rootList->count;++rmi){
            int mv = rootList->moves[rmi].move;
            if(legal(pos, mv)){
                workerthread->bestMove = mv;
                break;
            }
        }
    }

    //MultiPV: only thread 0 (the reporting thread) searches multiple
    //root lines. 
    int rootLegalMoves = (threadNum==0) ? countLegalRootMoves(pos) : 1;
    int multiPV         = (threadNum==0) ? MIN(MAX(1,info->multiPV), MAX(1,rootLegalMoves)) : 1;

    //game over (checkmate/stalemate): no legal moves, nothing to search
    if(rootLegalMoves==0){
        workerthread->bestMove   = NOMOVE;
        workerthread->ponderMove = NOMOVE;
        return;
    }

    //per-PV-line aspiration window state (and last score), carried
    //across depths exactly like the single alpha/beta pair used to be.
    int pvAlpha[MAXPOSMOVES];
    int pvBeta[MAXPOSMOVES];
    int pvScore[MAXPOSMOVES];
    for(pvNum=0;pvNum<multiPV;++pvNum){
        pvAlpha[pvNum]=-INFINITE_BOUND;
        pvBeta[pvNum]= INFINITE_BOUND;
        pvScore[pvNum]=0;
    }

    int delta,alpha,beta, searchDepth;

    //iterative deepening
    for(currentDepth=1;currentDepth<=MAXDEPTH;++currentDepth){

        bestMoveChanges /= 2.0;

        //MultiPV: nothing has been reported yet at this depth, so every
        //root move is a candidate for PV line 1 again. No-op when
        //multiPV==1 (count is already always 0 in that case).
        pos->excludedRootMoveCount = 0;

        for(pvNum=0;pvNum<multiPV;++pvNum){

            pos->currentPvNum = pvNum;

            delta       = ScoreWindow;
            alpha       = -INFINITE_BOUND;
            beta        =  INFINITE_BOUND;
            searchDepth = currentDepth;

            
            if(currentDepth >= WindowDepth && !info->bruteForceMode){
                alpha = MAX(-INFINITE_BOUND, pvScore[0]-delta);
                beta  = MIN( INFINITE_BOUND, pvScore[0]+delta);
            }

            while(TRUE){

                rootPv.count = 0;
                bestScore = AlphaBeta(alpha,beta,MAX(1,searchDepth),pos,info,table,threadNum,TRUE, FALSE, &rootPv);

                if(info->stopped==TRUE)break;

                if(threadNum==0){
                    numberOfPvMoves = rootPv.count;
                    for (int i = 0; i < numberOfPvMoves; i++) {
                        pos->search->pvArray[i] = rootPv.moves[i];
                    }
                    
                    // checkPvLegality(pos, pos->search->pvArray, numberOfPvMoves);
                    if(pvNum==0){
                        workerthread->bestMove   = pos->search->pvArray[0];
                        workerthread->ponderMove = numberOfPvMoves > 1 ? pos->search->pvArray[1] : NOMOVE;
                    }
                }

                //brute force mode never aspirates, single full-window search only
                if(info->bruteForceMode)break;

                //report a fail-low/fail-high bound if it's taking a while
                if(threadNum==0
                    && (bestScore<=alpha || bestScore>=beta)
                    && (getTimeMs()-info->starttime)>BoundReportTime){
                        UciReport(info, table,pos,alpha,beta,bestScore,currentDepth,numberOfPvMoves,pvNum+1);
                        fflush(stdout);
                }

                //fail low: widen downward, reset depth to full requested depth
                if(bestScore<=alpha){
                    //window already fully open: the score is a valid lower
                    //bound and can never be re-windowed — accept it
                    if(alpha<=-INFINITE_BOUND)break;
                    beta        = (alpha+beta)/2;
                    alpha       = MAX(-INFINITE_BOUND, alpha-delta);
                    searchDepth = currentDepth;
                    if(threadNum==0) info->stopOnPonderhit = FALSE;
                }
                //fail high: widen upward, allow a shallow depth trim
                else if(bestScore>=beta){
                    //window already fully open: accept the upper bound
                    if(beta>=INFINITE_BOUND)break;
                    beta         = MIN(INFINITE_BOUND, beta+delta);
                    searchDepth -= (abs(bestScore) <= AB_BOUND/2);
                }
                //inside window: done aspirating for this PV line at this depth
                else{
                    break;
                }

                delta += delta/2;
                if(delta > INFINITE_BOUND) delta = INFINITE_BOUND;
            }

            if(currentDepth==1 && pvNum==0 && threadNum==0){
                info->depthOneComplete = TRUE;
            }

            if(info->stopped==TRUE)break;

            pvScore[pvNum]  = bestScore;
            pvAlpha[pvNum]  = bestScore-ScoreWindow;
            pvBeta[pvNum]   = bestScore+ScoreWindow;

            if (threadNum==0){
                //reporting to interface
                UciReport(info, table,pos,pvAlpha[pvNum],pvBeta[pvNum],bestScore,currentDepth,numberOfPvMoves,pvNum+1);
                fflush(stdout);

                if(pvNum==0){
                    if(prevBestMove != NOMOVE && workerthread->bestMove != prevBestMove){
                        bestMoveChanges += 1.0;
                    }
                    prevBestMove = workerthread->bestMove;
                }

            
                if(pos->search->pvArray[0] != NOMOVE && pos->excludedRootMoveCount < MAXPOSMOVES){
                    pos->excludedRootMoves[pos->excludedRootMoveCount++] = pos->search->pvArray[0];
                }
            }
        }

        if(info->stopped==TRUE)break;

        //limits -- based on the best (PV line 1) result only, same as before
        if(!info->UciInfinite){
            //limited by depth
            if(info->depthSet && currentDepth>=info->depth)break;

            if(threadNum==0 && info->softTimeSet && currentDepth > 4 && !info->stopOnPonderhit){
                
                if(prevBestMove == NOMOVE || workerthread->bestMove != prevBestMove)
                    info->lastBestMoveDepth = currentDepth;
                
                int iterIdx = currentDepth & 3;
                info->iterValue[iterIdx] = pvScore[0];
                
                //falling eval: is the score dropping compared to the previous move's
                //final score and a few iterations ago this move?
                double fallingEval = (11.396
                                    + 2.035 * (info->bestPreviousAverageScore - pvScore[0])
                                    + 0.968 * (info->iterValue[iterIdx] - pvScore[0])) / 100.0;
                if(fallingEval < 0.5786) fallingEval = 0.5786;
                if(fallingEval > 1.6752) fallingEval = 1.6752;
                
                //reduction: time saved if the best move has been stable a while,
                //with hysteresis carried from the previous move via previousTimeReduction
                double timeReduction = (info->lastBestMoveDepth + 8 < currentDepth) ? 1.4857 : 0.7046;
                double reduction = (1.4540 + info->previousTimeReduction) / (2.1593 * timeReduction);
                
                //instability: widen the budget if the root best move keeps flipping
                double bestMoveInstability = 1.0 + 1.8519 * bestMoveChanges;
                
                double totalTime = (info->optimumTime - info->starttime)
                                  * fallingEval * reduction * bestMoveInstability;
                
                if(rootLegalMoves == 1 && totalTime > 5.0) totalTime = 5.0;
                
                int elapsed = getTimeMs() - info->starttime;
                
                //nodesEffort: if nearly all nodes went into the current best move and
                //we're already past a chunk of budget, stop early regardless
                int fi, bestFi = -1;
                for(fi=0; fi<pos->search->rootEffortCount; ++fi)
                    if(pos->search->rootEffortMove[fi]==workerthread->bestMove){ bestFi=fi; break; }
                int nodesEffort = (bestFi>=0 && info->nodes>0)
                                 ? (int)((pos->search->rootEffortNodes[bestFi]*100000ULL)/info->nodes) : 0;
                
                int maxElapsed = info->timeSet ? (info->stoptime - info->starttime) : (int)totalTime;
                int shouldStop = (currentDepth>=10 && nodesEffort>=97000 && elapsed>totalTime*0.6539) ||
                                 (elapsed > totalTime) ||
                                 (elapsed > maxElapsed);

                if(shouldStop){
                    if(info->ponder){
                        info->stopOnPonderhit = TRUE;
                    } else {
                        info->stopped = TRUE;
                        break;
                    }
                }
                
                info->previousTimeReduction = timeReduction;
            }

            //mate limits
            if(abs(pvScore[0]) > ISMATE && workerthread->bestMove != NOMOVE){
                int mateIn  = (AB_BOUND - abs(pvScore[0]) + 1) / 2;
                if(info->mateLimit != -1){
                    if(mateIn <= info->mateLimit){
                        if(info->ponder){
                            info->stopOnPonderhit = TRUE;
                        } else {
                            break;
                        }
                    }
                }else{
                    //mate break to avoid losing time
                    if(currentDepth >= (mateIn*2) + 10){
                        if(info->ponder){
                            info->stopOnPonderhit = TRUE;
                        } else {
                            break;
                        }
                    }
                }
            }
        }
    }
    if (threadNum == 0) {
        if (!info->ponder) {
            info->bestPreviousScore = pvScore[0];
            //running average: smooth the fallingEval TM signal across moves
            //to prevent overreaction to single-depth score fluctuations
            info->bestPreviousAverageScore =
                (info->bestPreviousAverageScore != INFINITE_BOUND)
                ? (info->bestPreviousAverageScore + pvScore[0]) / 2
                : pvScore[0];
        }
    }
}


// Persistent Thread Pool
typedef struct {
    thrd_t handle;
    mtx_t mutex;
    cnd_t cv_start;
    cnd_t cv_done;
    int threadNumber;
    volatile int searching;
    volatile int exit;
    S_BOARD *originalPos;
    THREAD_SEARCH_WORKER workerData;
} POOL_WORKER;

static POOL_WORKER threadPool[MAXTHREADS];
static int poolSize = 0;

static int workerLoop(void *data) {
    POOL_WORKER *worker = (POOL_WORKER*)data;

    mtx_lock(&worker->mutex);
    while (1) {
        while (!worker->searching && !worker->exit) {
            cnd_wait(&worker->cv_start, &worker->mutex);
        }
        if (worker->exit) {
            break;
        }

        mtx_unlock(&worker->mutex);

        worker->originalPos->ply = 0;
        nnue_refresh_accumulator(worker->originalPos);
        IterativeDeepening(&worker->workerData);

        if (worker->threadNumber == 0) {
            // When pondering or in infinite search, wait until GUI sends "ponderhit" or "stop"
            while (!worker->workerData.info->stopped && 
                   (worker->workerData.info->ponder || worker->workerData.info->UciInfinite)) {
                struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
                thrd_sleep(&ts, NULL);
            }

            worker->workerData.info->stopped = TRUE;

            //safety net: verify the bestmove is actually legal before sending it
            //to the GUI — a TT collision or threading issue could leave a stale
            //move that passed makeMove in a different internal state
            if (worker->workerData.bestMove != NOMOVE &&
                !MoveExists(worker->originalPos, worker->workerData.bestMove)) {
                S_MOVELIST fallbackList[1];
                GenerateAllMoves(worker->originalPos, fallbackList);
                worker->workerData.bestMove = NOMOVE;
                worker->workerData.ponderMove = NOMOVE;
                for (int i = 0; i < fallbackList->count; ++i) {
                    if (legal(worker->originalPos, fallbackList->moves[i].move)) {
                        worker->workerData.bestMove = fallbackList->moves[i].move;
                        break;
                    }
                }
            }

            if (worker->workerData.info->setOptionPonder && worker->workerData.bestMove != NOMOVE) {
                if (worker->workerData.ponderMove == NOMOVE) {
                    StateInfo st;
                    if (legal(worker->originalPos, worker->workerData.bestMove)) {
                        makeMove(worker->originalPos, worker->workerData.bestMove, &st);
                        int pMove = ProbePvTable(worker->originalPos, worker->workerData.ttable);
                        if (pMove != NOMOVE && MoveExists(worker->originalPos, pMove) && legal(worker->originalPos, pMove)) {
                            worker->workerData.ponderMove = pMove;
                        }
                        takeMove(worker->originalPos);
                    }
                }
                if (worker->workerData.ponderMove != NOMOVE) {
                    printf("bestmove %s ", PrMove(worker->workerData.bestMove));
                    printf("ponder %s\n", PrMove(worker->workerData.ponderMove));
                } else {
                    printf("bestmove %s\n", PrMove(worker->workerData.bestMove));
                }
            } else {
                printf("bestmove %s\n", PrMove(worker->workerData.bestMove));
            }
            fflush(stdout);
        }

        mtx_lock(&worker->mutex);
        worker->searching = 0;
        cnd_signal(&worker->cv_done);
    }
    mtx_unlock(&worker->mutex);

    return 0;
}

void EnsureThreadPool(int numThreads) {
    if (numThreads <= 0) return;
    if (numThreads > MAXTHREADS) numThreads = MAXTHREADS;
    if (numThreads <= poolSize) return;

    for (int i = poolSize; i < numThreads; i++) {
        POOL_WORKER *w = &threadPool[i];
        w->threadNumber = i;
        w->searching = 0;
        w->exit = 0;
        mtx_init(&w->mutex, mtx_plain);
        cnd_init(&w->cv_start);
        cnd_init(&w->cv_done);

        w->originalPos = malloc(sizeof(S_BOARD));
        if (!w->originalPos) {
            fprintf(stderr, "Error: failed to allocate board for thread %d\n", i);
            break;
        }
        w->originalPos->search = alloc_search_thread();
        if (!w->originalPos->search) {
            fprintf(stderr, "Error: failed to allocate search thread for thread %d\n", i);
            free(w->originalPos);
            w->originalPos = NULL;
            break;
        }

        thrd_create(&w->handle, &workerLoop, (void*)w);
        poolSize++;
    }
}

static void setupWorkerData(int threadNum, S_BOARD *pos, S_SEARCHINFO *info, S_PVTABLE *table) {
    POOL_WORKER *w = &threadPool[threadNum];

    S_SEARCH_THREAD *saved_search = w->originalPos->search;
    memcpy(w->originalPos, pos, sizeof(S_BOARD));
    w->originalPos->search = saved_search;

    w->originalPos->stateTable[0].previous = NULL;
    for (int i = 1; i <= pos->hisPly; i++) {
        w->originalPos->stateTable[i].previous = &w->originalPos->stateTable[i-1];
    }
    w->originalPos->st = &w->originalPos->stateTable[pos->hisPly];

    memcpy(w->originalPos->search, pos->search, sizeof(S_SEARCH_THREAD));

    w->originalPos->eTable->evalTable = threadEvalTable[threadNum].evalTable;
    w->originalPos->eTable->numEntries = threadEvalTable[threadNum].numEntries;

    w->originalPos->pawnKingTable->paTable = threadPawnTable[threadNum].paTable;
    w->originalPos->pawnKingTable->numEntries = threadPawnTable[threadNum].numEntries;

    w->workerData.originalPos  = w->originalPos;
    w->workerData.info         = info;
    w->workerData.ttable       = table;
    w->workerData.threadNumber = threadNum;
    w->workerData.bestMove     = NOMOVE;
    w->workerData.ponderMove   = NOMOVE;
}

static void startWorkerSearch(int threadNum, S_BOARD *pos, S_SEARCHINFO *info, S_PVTABLE *table) {
    POOL_WORKER *w = &threadPool[threadNum];
    mtx_lock(&w->mutex);
    setupWorkerData(threadNum, pos, info, table);
    w->searching = 1;
    cnd_signal(&w->cv_start);
    mtx_unlock(&w->mutex);
}

static void waitWorkerSearch(int threadNum) {
    POOL_WORKER *w = &threadPool[threadNum];
    mtx_lock(&w->mutex);
    while (w->searching) {
        cnd_wait(&w->cv_done, &w->mutex);
    }
    mtx_unlock(&w->mutex);
}

void FreeThreadPool(void) {
    for (int i = 0; i < poolSize; i++) {
        POOL_WORKER *w = &threadPool[i];
        mtx_lock(&w->mutex);
        w->exit = 1;
        cnd_signal(&w->cv_start);
        mtx_unlock(&w->mutex);

        thrd_join(w->handle, NULL);

        cnd_destroy(&w->cv_start);
        cnd_destroy(&w->cv_done);
        mtx_destroy(&w->mutex);

        if (w->originalPos) {
            if (w->originalPos->search) {
                free(w->originalPos->search);
                w->originalPos->search = NULL;
            }
            free(w->originalPos);
            w->originalPos = NULL;
        }
    }
    poolSize = 0;

    if (launcherPos) {
        if (launcherPos->search) {
            free(launcherPos->search);
            launcherPos->search = NULL;
        }
        free(launcherPos);
        launcherPos = NULL;
    }
}

void SearchPosition(S_BOARD *pos, S_SEARCHINFO *info, S_PVTABLE *table) {
    ASSERT(checkBoard(pos));

    int bestMove   = NOMOVE;
    int ponderMove = NOMOVE;

    //init search things
    InitSearcher(pos, info, table);
    nnue_refresh_accumulator(pos);

    //Syzygy root probe
    TBProbeRoot(pos);

    //ensure persistent per-thread tables are allocated
    EnsureThreadTables(info->threadNum);

    //ensure persistent thread pool has enough workers
    EnsureThreadPool(info->threadNum);

    //start workers
    for (int i = 0; i < info->threadNum; ++i) {
        startWorkerSearch(i, pos, info, table);
    }

    //wait for all workers to complete
    for (int i = 0; i < info->threadNum; ++i) {
        waitWorkerSearch(i);
    }
}