#include "defs.h"
#include "stdio.h"
#include "math.h"
#include "search.h"
#include "see.h"
#include "some_maths.h"
#include "recog.h"
#include "inttypes.h"
#include "evaluate.h"
#include "bitboards.h"
#include "uci.h"
#include "movepicker.h"
#include "history.h"

//NOTE
/*
    Some Search code are based on Ethereal 12.75, big thanks credit to Andrew Grant
*/

int LMRTable[MAXDEPTH][MAXPOSMOVES];
void initLMRTable(){
    int i,j;
    for(i=0;i<MAXDEPTH;++i){
        for(j=0;j<MAXPOSMOVES;++j){
            LMRTable[i][j]=0.75 + log(i) * log(j) / 2.25;
        }
    }
}

//search helper functions
INLINE void checkUp(S_SEARCHINFO *info){
    if(!info->UciInfinite && !info->ponder){
        if((!info->analyzeMode && info->EloNodeSet==TRUE && info->nodes>=info->EloNodelimit) ||
           (info->timeSet==TRUE && getTimeMs()>info->stoptime)         ||
           (info->nodeSet==TRUE && info->nodes>=info->nodeLimit)){
            info->stopped=TRUE;
           }
    }

    ReadInput(info);
}
INLINE void InitSearcher(S_BOARD *pos,S_SEARCHINFO *info){
    int index=0;
    int index2=0;
    updateAge(pos->pvTable);

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
int Singularity(S_BOARD *pos,S_SEARCHINFO *info,int ttValue,int depth,int beta,int ttMove,int *multiCut);
int StaticExchangeEvaluation(S_BOARD *pos,int move,int threshold);
//main search functions
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

    //pruning standing pat
    int eval      = pos->eval_stack[pos->ply] = EvalPosition(pos);

    //Pruning
    if(eval>=beta)return beta;
    alpha=MAX(alpha,eval);

    //int threshold = MAX(1,alpha-eval-QSSeeMargin);

    S_MOVELIST list[1];
    GenerateAllNoisy(pos,list);
    InitAllScore(pos,list,NOMOVE,MAX(1,alpha-eval-QSSeeMargin));
    for(moveNum=0;moveNum<list->count;++moveNum){
        PickNextMove(moveNum,list);
        moveInLoop = list->moves[moveNum].move;

        /*//SEE Pruning
        if(!StaticExchangeEvaluation(pos, moveInLoop, threshold)){
            continue;
        }*/
        if(!info->bruteForceMode){
            //SEE Pruning
            if(list->moves[moveNum].score < 0){
                continue;
            }

            //DELTA PRUNING
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
int AlphaBeta(int alpha,int beta,int depth,S_BOARD *pos,S_SEARCHINFO *info,int doNULL){

    int R,improving,quietMove,moveInLoop,newDepth,singular,extension,seeMargin[2];
    int fmhist          =0;
    int cmhist          =0;
    int multiCut        =FALSE;
    int value           =0;
    int Score           =-INFINITE;
    int pvNode          =(alpha != beta-1);
    int rootNode        =pos->ply==0;
    int quietsSeen      =0;
    int bestMove        =NOMOVE;
    int oldAlpha        =alpha;
    int moveNum         =0;
    int Legal           =0;
    int bestScore       =-INFINITE;
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
    if(depth<=0){
        if(!inCheck)return Quiescence(alpha,beta,pos,info);
        else depth = 1;
    }

    //cleaning
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
        int rAlpha = alpha > -INFINITE + pos->ply     ? alpha : -INFINITE + pos->ply;
        int rBeta  =  beta <  INFINITE - pos->ply - 1 ?  beta :  INFINITE - pos->ply - 1;
        if (rAlpha >= rBeta) return rAlpha;
    }

    //probing Transposition Table
    if((ttHit=ProbeHashEntry(pos, &ttMove, &ttValue, &ttDepth, &ttBound,&ttEval))){

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
    int staticEval=pos->eval_stack[pos->ply] = (ttEval != VALUE_NONE) ? ttEval:EvalPosition(pos);
    improving = pos->ply >= 2 && staticEval>pos->eval_stack[pos->ply-2];

    seeMargin[0] = SEENoisyMargin * depth * depth;
    seeMargin[1] = SEEQuietMargin * depth;

    if(!info->bruteForceMode && !inCheck && !pvNode){

        //beta pruning
        if(depth < 3 && abs(beta-1)>-ISMATE){
            int evalMargin=SEEPieceValues[wP]*depth;
            if(staticEval - evalMargin>=beta){
                return staticEval-evalMargin;
            }
        }

        //null move
        if(doNULL){
            if(!rootNode &&
               staticEval >= beta &&
               depth >= defaultNullMoveDepth &&
               boardHasNonPawnMaterial(pos,pos->side) &&
               (!ttHit || !(ttBound == HFALPHA) || ttValue >= beta)){

                makeNullMove(pos);

                R = 4 + depth / 6 + MIN(3, (staticEval - beta) / 200);

                int valueNull=-AlphaBeta(-beta,-beta+1,depth-R,pos,info,FALSE);
                takeNullMove(pos);
                if(info->stopped==TRUE)return 0;
                if(valueNull >= beta)return beta;
            }

            //razoring
            Score=staticEval+SEEPieceValues[wP];
            int newScore;

            if(Score<beta){
                if(depth==1){
                    newScore=Quiescence(alpha,beta,pos,info);
                    return (newScore>Score) ? newScore:Score;
                }
            }

            Score+=SEEPieceValues[wP];
            if(Score<beta && depth < 4){
                newScore=Quiescence(alpha,beta,pos,info);
                if(newScore<beta)return (newScore>Score) ? newScore:Score;
            }
        }
    }

    //PROBCUT
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

                /*//SEE Pruning
                if(!StaticExchangeEvaluation(pos, move_in_prob, probThresh)){
                    continue;
                }*/
                //SEE Pruning
                if(problist->moves[move_num].score < 0){
                    continue;
                }

                if (!makeMove(pos,move_in_prob))continue;

                if(depth>=2*probCutDepth)value=-AlphaBeta(-rBeta,-rBeta+1,1,pos,info,TRUE);
                if (depth<2*probCutDepth || value>=rBeta)value=-AlphaBeta(-rBeta,-rBeta+1,depth-4,pos,info,TRUE);

                takeMove(pos);

                if(value>=rBeta)return value;

            }
    }

    //IIR
    if(!info->bruteForceMode && depth>=4 && ttMove==NOMOVE)depth--;

    S_MOVELIST list[1];
    GenerateAllMoves(pos,list);
    InitAllScore(pos,list,ttMove,0);
    Score = -INFINITE;
    int skipQuiets = 0;
    for(moveNum=0;moveNum<list->count;++moveNum){
        PickNextMove(moveNum,list);
        moveInLoop=list->moves[moveNum].move;

        quietsSeen+=(quietMove=!moveIsTactical(pos,moveInLoop));

        if(skipQuiets
            && quietMove
            && moveInLoop != ttMove){
            continue;
        }

        //get history
        hist = !quietMove ? getCaptureHistory(pos,moveInLoop):getHistory(pos,moveInLoop,&fmhist,&cmhist);

        //Quiet late Move pruning
        if (!info->bruteForceMode && quietMove && bestScore > -ISMATE){

            if (   depth <= FutilityPruningDepth
                && (staticEval + FutilityMargin) <= alpha
                && hist < FutilityPruningHistoryLimit[improving]){
                skipQuiets = 1;
                }

            if (   depth <= FutilityPruningDepth
                && (staticEval + FutilityMargin + FutilityMarginNoHistory) <= alpha){
                skipQuiets = 1;
                }

            if (depth<=LateMovePruningDepth &&
                quietsSeen>=LateMovePruningCounts[improving][depth]){
                    skipQuiets = 1;
                }

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
        if((getTimeMs()-info->starttime)>UciCurrMoveTime && rootNode)UciReportCurrentMove(depth,moveInLoop,Legal);


        if(!info->bruteForceMode){
            //singular extension
            singular = !rootNode
                     &&  depth >= 8
                     &&  moveInLoop == ttMove
                     &&  ttDepth >= depth - 2
                     && (ttBound == HFBETA);

            extension = singular ? Singularity(pos,info,ttValue,depth,beta,ttMove,&multiCut)
                        :inCheck || (quietMove && pvNode && cmhist > HistexLimit && fmhist > HistexLimit);

            newDepth = MIN(MAXDEPTH - 2,depth + (extension && !rootNode));

            if(multiCut==TRUE){
                takeMove(pos);
                return MAX(ttValue-depth,-INFINITE);
            }
        }else{
            newDepth  = depth;
            extension = 0;
        }

        if (quietMove && depth > 2 && Legal > 1 && !info->bruteForceMode){
            R = LMRTable[MIN(depth, MAXDEPTH/2-1)][MIN(Legal, MAXDEPTH/2-1)];

            R += !improving + !pvNode + extension;

            R += inCheck && pieceKing[pos->pieces[TOSQ(moveInLoop)]];

            R -= list->moves[moveNum].score >= SORT_COUNTER;

            R -= MAX(-2, MIN(2, hist / 5000));

            R = MIN(depth - 1, MAX(R, 1));
        }
        else if (!quietMove && depth > 2 && Legal > 1 && !info->bruteForceMode){
            R = MIN(depth - 1, MAX(1, MIN(3, 3 - (hist + 4000) / 2000)));
        }

        else R = 1;

        //LMR
        if(R != 1) Score = -AlphaBeta(-alpha-1,-alpha,newDepth - R,pos,info,TRUE);

        //PVS
        if((R != 1 && Score > alpha) || (R == 1 && !(pvNode && Legal == 1))){
            Score = -AlphaBeta(-alpha-1,-alpha,newDepth - 1,pos,info,TRUE);
        }

        //Normal Search
        if(pvNode && (Legal == 1 || Score > alpha)){
            Score = -AlphaBeta(-beta,-alpha,newDepth - 1,pos,info,TRUE);
        }


        takeMove(pos);
        if(quietMove)quietsTried[quietsPlayed++] = moveInLoop;
        else capturesTried[capturesPlayed++]     = moveInLoop;

        if(info->stopped==TRUE)return 0;
        if(Score>bestScore){
            bestScore=Score;
            bestMove=moveInLoop;
            if(Score>alpha){
                alpha=Score;
                if(alpha>=beta){
                    if(!moveIsTactical(pos,bestMove))updateHistories(pos,quietsTried,quietsPlayed,depth);
                    updateCaptureHistory(pos,bestMove,capturesTried,capturesPlayed,depth);
                    StoreHashEntry(pos, bestMove, beta, HFBETA, depth,staticEval);
                    return beta;
                }
                //if(quietMove)pos->searchHistory[pos->pieces[FROMSQ(bestMove)]][TOSQ(bestMove)]+=depth;
            }
        }
    }

    //checkmate and stalemate
    if(Legal==0)return inCheck ? -INFINITE + pos->ply : 0;

    //update TT
    if(oldAlpha != alpha)StoreHashEntry(pos,bestMove,bestScore,HFEXACT,depth,staticEval);
    else                 StoreHashEntry(pos,bestMove,alpha    ,HFALPHA,depth,staticEval);

    return alpha;
}
int Singularity(S_BOARD *pos,S_SEARCHINFO *info,int ttValue,int depth,int beta,int ttMove,int *multiCut){

    int moveNum;
    int moveInLoop = NOMOVE;
    int skipQuiets = 0;
    int quiets     = 0;
    int tacticals  = 0;
    int value      = -INFINITE;
    int rBeta      = MAX(ttValue-depth,-INFINITE);
    int quietMove;

    //revert the move
    takeMove(pos);

    S_MOVELIST singularList[1];
    GenerateAllMoves(pos,singularList);
    InitAllScore(pos,singularList,NOMOVE,0);
    for(moveNum=0;moveNum<singularList->count;++moveNum){
        PickNextMove(moveNum,singularList);
        moveInLoop = singularList->moves[moveNum].move;

        quietMove = !moveIsTactical(pos,moveInLoop);

        //skipQuiets
        if(skipQuiets
            && quietMove
            && moveInLoop != ttMove){
            continue;
        }

        //skip the pvMove
        if(moveInLoop==ttMove)continue;

        if(!makeMove(pos,moveInLoop))continue;
        value = -AlphaBeta(-rBeta-1,-rBeta,depth/2-1,pos,info,TRUE);
        takeMove(pos);

        if(value>rBeta)break;

        quietMove ? tacticals++:quiets++;
        skipQuiets = quiets >= SingularQuietLimit;

        if(skipQuiets && tacticals >= SingularTacticalLimit)break;
    }

    //multicut
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



void SearchPosition(S_BOARD *pos,S_SEARCHINFO *info){

    ASSERT(checkBoard(pos));

    int currentDepth,numberOfPvMoves,bestScore;

    int bestMove       = NOMOVE;
    int ponderMove     = NOMOVE;

    //aspiration window
    int alpha          = -INFINITE;
    int beta           = INFINITE;

    //init search things
    InitSearcher(pos,info);

    //iterative deepening
    for(currentDepth=1;currentDepth<=MAXDEPTH;++currentDepth){

        //call alpha beta to get score
        bestScore=AlphaBeta(alpha,beta,currentDepth,pos,info,TRUE);
        //say out of time for gui
        if(info->stopped==TRUE)break;
        //get Pv Lines
        numberOfPvMoves=getPvLine(currentDepth,pos);
        //get best move from pv lines
        bestMove=pos->pvArray[0];
        ponderMove = numberOfPvMoves > 1 ? pos->pvArray[1] : NOMOVE;

        //////////////////////////////////////////////////////////
        //aspiration window
        if(!info->bruteForceMode){
            if((bestScore <= alpha) || (bestScore >= beta)){
                if((getTimeMs()-info->starttime)>BoundReportTime){
                    UciReport(info,pos,alpha,beta,bestScore,currentDepth,numberOfPvMoves);
                    fflush(stdout);
                }
                alpha=-INFINITE;
                beta=INFINITE;
                continue;
            }
            alpha=bestScore-ScoreWindow;
            beta=bestScore+ScoreWindow;
        }
        //////////////////////////////////////////////////////////

        //reporting to interface
        UciReport(info,pos,alpha,beta,bestScore,currentDepth,numberOfPvMoves);
        fflush(stdout);


        //limits
        if(!info->ponder && !info->UciInfinite){
            //limited by depth
            if(info->depthSet && currentDepth>=info->depth)break;
            //mate limits
            if(abs(bestScore) > ISMATE && bestMove != NOMOVE){
                int mateIn  = (INFINITE - abs(bestScore) + 1) / 2;
                if(info->mateLimit != -1){
                    if(mateIn <= info->mateLimit)break;
                }else{
                    //mate break to avoid losing time
                    if(currentDepth >= (mateIn*2) + 10){
                        break;
                    }
                }
            }
        }
    }


    if(info->setOptionPonder){
        if(ponderMove != NOMOVE){
            printf("bestmove %s ",PrMove(bestMove));
            printf("ponder %s\n",PrMove(ponderMove));
        }else{
            printf("bestmove %s\n",PrMove(bestMove));
        }
    }else{
        printf("bestmove %s\n",PrMove(bestMove));
    }
    fflush(stdout);
}
