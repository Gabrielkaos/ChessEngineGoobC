#include "defs.h"
#include "stdio.h"
#include "math.h"

//aspiration window
#define ScoreWindow 50

//queen value
const int delta=1000;
//   0,pawn value, knight value, rook value
const int futilityMargin[4]={0,100,300,500};

static inline void PickNextMove(int moveNum,S_MOVELIST *list){
    S_MOVE temp;
    int index=0;
    int bestNum=moveNum;
    int bestScore=0;

    for(index=moveNum;index<list->count;++index){
        if(list->moves[index].score>bestScore){
            bestScore=list->moves[index].score;
            bestNum=index;
        }
    }

    temp=list->moves[moveNum];
    list->moves[moveNum]=list->moves[bestNum];
    list->moves[bestNum]=temp;

}
static inline void checkUp(S_SEARCHINFO *info){
    if(info->timeset ==TRUE && getTimeMs()>info->stoptime){
        info->stopped=TRUE;
    }

    ReadInput(info);
}
static inline int isRepitition(const S_BOARD *pos){
    int index=0;
    for(index=(pos->hisPly-pos->fiftyMove);index<pos->hisPly-1;++index){

        if(pos->posKey==pos->history[index].posKey){
            return TRUE;
        }
    }

    return FALSE;
}
static inline void ClearForSearch(S_BOARD *pos,S_SEARCHINFO *info){
    int index=0;
    int index2=0;

    for(index=0;index<13;++index){
        for(index2=0;index2<BOARD_NUMS_SQ;++index2){
            pos->searchHistory[index][index2]=0;
        }
    }
    for(index=0;index<2;++index){
        for(index2=0;index2<MAXDEPTH;++index2){
            pos->searchKillers[index][index2]=0;
        }
    }

    //clearPvTable(pos->pvTable);

    pos->ply=0;
    /*pos->pvTable->overwrite=0;
	pos->pvTable->hit=0;
	pos->pvTable->cut=0;*/

    info->stopped=0;
    info->nodes=0ULL;

}

static inline int Quiescence(int alpha,int beta,S_BOARD *pos,S_SEARCHINFO *info){

    if((info->nodes & 2047)==0){
        checkUp(info);
    }
    info->nodes++;

    if(isRepitition(pos) || pos->fiftyMove >= 100){
        return 0;
    }
    if(pos->ply >MAXDEPTH-1){
        return EvalPosition(pos);
    }
    int Score=EvalPosition(pos);
    if(Score>=beta){
        return beta;
    }

    //Delta pruning
    ///////////////////////
    if(Score<alpha-delta){
        return alpha;
    }
    ///////////////////////

    if(Score>alpha){
        alpha=Score;
    }

    S_MOVELIST list[1];
    GenerateAllCaptures(pos,list);
    Score=-INFINITE;
    int moveNum=0;
    int Legal=0;

    for(moveNum=0;moveNum<list->count;++moveNum){

        PickNextMove(moveNum,list);

        if(!makeMove(pos,list->moves[moveNum].move)){
            continue;
        }

        Legal++;
        Score=-Quiescence(-beta,-alpha,pos,info);
        takeMove(pos);

        if(info->stopped==TRUE){
            return 0;
        }

        if(Score>alpha){
            if(Score>=beta){

                return beta;
            }
            alpha=Score;
        }
    }
    return alpha;
}
static inline int AlphaBeta(int alpha,int beta,int depth,S_BOARD *pos,S_SEARCHINFO *info,int doNULL){

    if(depth<=0){
        return Quiescence(alpha,beta,pos,info);
    }

    if((info->nodes & 2047)==0){
        checkUp(info);
    }

    info->nodes++;

    if((isRepitition(pos) || pos->fiftyMove>=100) && pos->ply){
        return 0;
    }

    if(pos->ply >MAXDEPTH-1){
        return EvalPosition(pos);
    }

    int inCheck=is_square_attacked_BB(pos->kingSq[pos->side],pos->side^1,pos);
    if(inCheck){
        depth++;
    }

    int Score=-INFINITE;
    int pvNode=(beta-alpha)>1;
    int pvMove=NOMOVE;

    if((ProbeHashEntry(pos, &pvMove, &Score, alpha, beta, depth) == TRUE)) {
		return Score;
	}

	/////////////////////////////////////

	//NEWLY ADDED THINGS

	////////////////////////////////////

    int futilityPruning=0;
    int movesSearched=0;

    if(!inCheck && !pvNode){

        //for pruning
        int staticEval=EvalPosition(pos);

        //evaluation pruning
        if(depth < 3 && abs(beta-1)>-MATE+100){
            int evalMargin=pieceVal[wP]*depth;
            if(staticEval - evalMargin>=beta){
                return staticEval-evalMargin;
            }
        }

        //razoring

        if(doNULL){

            if(pos->ply && (pos->bigPiece[pos->side] >=2) && depth >=4){
                makeNullMove(pos);
                Score=-AlphaBeta(-beta,-beta+1,depth-4,pos,info,FALSE);
                takeNullMove(pos);
                if(info->stopped==TRUE){
                    return 0;
                }
                if (Score >= beta && abs(Score) < ISMATE) {
                  return beta;
                }
            }

            Score=staticEval+pieceVal[wP];
            int newScore;

            if(Score<beta){
                if(depth==1){
                    newScore=Quiescence(alpha,beta,pos,info);
                    return (newScore>Score) ? newScore:Score;
                }
            }

            Score+=pieceVal[wP];
            if(Score<beta && depth < 4){
                newScore=Quiescence(alpha,beta,pos,info);
                if(newScore<beta){
                    return (newScore>Score) ? newScore:Score;
                }
            }
        }
        //futility pruning
        if(depth<4 && abs(alpha)< (MATE-1000) && staticEval+futilityMargin[depth] <= alpha){
            futilityPruning=1;
        }
    }
    //////////////////////////////////////

    S_MOVELIST list[1];
    GenerateAllMoves(pos,list);

    int bestMove=NOMOVE;
    //Score=-INFINITE;
    int oldAlpha=alpha;
    int moveNum=0;
    int Legal=0;
    int bestScore=-INFINITE;

    if(pvMove != NOMOVE){
        for(moveNum=0;moveNum<list->count;++moveNum){
            if(list->moves[moveNum].move==pvMove){
                list->moves[moveNum].score=2000000;
                break;
            }
        }
    }

    //int foundPV=FALSE;

    for(moveNum=0;moveNum<list->count;++moveNum){
        PickNextMove(moveNum,list);
        int moveInLoop=list->moves[moveNum].move;
        if(!makeMove(pos,moveInLoop)){
            continue;
        }
        Legal++;

        /*//pawn promote extension
        if(piecePawn[pos->pieces[FROMSQ(moveInLoop)]] &&
           (ranksBoard[TOSQ(moveInLoop)]==RANK_7 || ranksBoard[TOSQ(moveInLoop)]==RANK_2)){
                depth++;
           }*/

        //futility pruning
        if(futilityPruning && movesSearched && CAPTURED(moveInLoop)==0 && PROMOTED(moveInLoop)==0 &&
            !is_square_attacked_BB(pos->kingSq[pos->side],(pos->side^1),pos)){
                takeMove(pos);
                continue;
            }

        //LMR
        if(movesSearched==0) Score=-AlphaBeta(-beta,-alpha,depth-1,pos,info,TRUE);
        else{
            if(movesSearched >= 4 &&
               CAPTURED(moveInLoop)==0 &&
               PROMOTED(moveInLoop)==0 &&
               depth >=3 &&
               !inCheck &&
               !pvNode &&
               (FROMSQ(moveInLoop) != FROMSQ(pos->searchKillers[0][pos->ply]) || TOSQ(moveInLoop) != TOSQ(pos->searchKillers[0][pos->ply])) &&
               (FROMSQ(moveInLoop) != FROMSQ(pos->searchKillers[1][pos->ply]) || TOSQ(moveInLoop) != TOSQ(pos->searchKillers[1][pos->ply]))){
                    Score=-AlphaBeta(-alpha-1,-alpha,depth-2,pos,info,TRUE);
               }else {
                   Score = alpha +1;
                   }
            //PVS
            if(Score>alpha){
                Score=-AlphaBeta(-alpha-1,-alpha,depth-1,pos,info,TRUE);
                if(Score>alpha && Score<beta){
                    Score=-AlphaBeta(-beta,-alpha,depth-1,pos,info,TRUE);
                }
            }
        }

        takeMove(pos);
        movesSearched++;

        if(info->stopped==TRUE){
            return 0;
        }
        if(Score>bestScore){
            bestScore=Score;
            bestMove=moveInLoop;
            if(Score>alpha){
                if(Score>=beta){

                    if(CAPTURED(moveInLoop)==0){
                        pos->searchKillers[1][pos->ply]=pos->searchKillers[0][pos->ply];
                        pos->searchKillers[0][pos->ply]=moveInLoop;
                    }
                    StorePvTable(pos, bestMove, beta, HFBETA, depth);
                    return beta;
                }
                alpha=Score;

                if(CAPTURED(moveInLoop)==0){
                    pos->searchHistory[pos->pieces[FROMSQ(bestMove)]][TOSQ(bestMove)]+=depth;
                }
            }
        }
    }

    //checkmate and stalemate
    if(Legal==0){
        if(inCheck){
            return -INFINITE +pos->ply;
        }else{
            return 0;
        }
    }

    if (oldAlpha != alpha){
        StorePvTable(pos, bestMove, bestScore, HFEXACT, depth);
    }else{
        StorePvTable(pos, bestMove, alpha, HFALPHA, depth);
    }

    return alpha;
}
void SearchPosition(S_BOARD *pos,S_SEARCHINFO *info){

    int bestMove=NOMOVE;
    int bestScore=-INFINITE;
    int currentDepth=0;
    int pvMoves=0;
    int pvNum=0;

    //aspiration window
    int alpha=-INFINITE;
    int beta=INFINITE;

    ClearForSearch(pos,info);

    if(EngineOptions->useBook==TRUE){
        bestMove=getBookMove(pos);
    }

    if(bestMove == NOMOVE){
        for(currentDepth=1;currentDepth<=info->depth;++currentDepth){

            //call alpha beta to get score
            bestScore=AlphaBeta(alpha,beta,currentDepth,pos,info,TRUE);

            //say out of time for gui
            if(info->stopped==TRUE){
                break;
            }
            //////////////////////////////////////////////////////////
            //aspiration window
            if((bestScore <= alpha) || (bestScore >= beta)){
                alpha=-INFINITE;
                beta=INFINITE;
                //currentDepth--;
                continue;
            }
            alpha=bestScore-ScoreWindow;
            beta=bestScore+ScoreWindow;
            //////////////////////////////////////////////////////////

            //get Pv Lines
            pvMoves=getPvLine(currentDepth,pos);

            //get best move from pv lines
            bestMove=pos->pvArray[0];

            if(info->GAMEMODE==UCIMODE){
                printf("info score cp %d depth %d nodes %llu time %d ",
                       bestScore,currentDepth,info->nodes,getTimeMs()-info->starttime);
            }else if(info->GAMEMODE==XBOARDMODE && info->POST_THINKING==TRUE){
                printf("%d %d %d %llu ",
                        currentDepth,bestScore,(getTimeMs()-info->starttime)/10,info->nodes);
            }else if(info->POST_THINKING==TRUE){

                printf("depth %d score %d nodes %llu time %dms ",
                currentDepth,bestScore,info->nodes,(getTimeMs()-info->starttime));
            }
            if(info->GAMEMODE==UCIMODE || info->POST_THINKING==TRUE){
                pvMoves=getPvLine(currentDepth,pos);
                printf("pv");
                for(pvNum=0;pvNum<pvMoves;++pvNum){
                    printf(" %s",PrMove(pos->pvArray[pvNum]));
                }
                printf("\n");
                //printf("Hits:%d Overwrite:%d NewWrite:%d Cut:%d\n",pos->pvTable->hit,pos->pvTable->overwrite,pos->pvTable->newwrite,pos->pvTable->cut);
            }
        }
    }

    if(info->GAMEMODE==UCIMODE){
        printf("bestmove %s\n",PrMove(bestMove));
    }else if(info->GAMEMODE==XBOARDMODE){
        printf("move %s\n",PrMove(bestMove));
        makeMove(pos,bestMove);
    }else{
        makeMove(pos,bestMove);
        //PrintBoard(pos);
        printf("\n\n%s's bestMove -> %s \n",NAME,PrMove(bestMove));
    }
}
