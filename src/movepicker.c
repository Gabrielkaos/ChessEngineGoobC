#include "movepicker.h"
#include "defs.h"
#include "search.h"

void PickNextMove(int moveNum,S_MOVELIST *list){
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

void InitAllScore(S_BOARD *pos, S_MOVELIST *movelist, int ttMove, int threshold){

    static const int MVVAugment[] = {0, 2400, 2400, 4800, 9600};

    int index;
    int move;
    int pce;
    int captured,to;

    int counter = pos->ply > 0 ? pos->moveStack[pos->ply - 1]:NOMOVE;
    int cmPiece = pos->pieceStack[pos->ply - 1];
    int cmTo    = TOSQ(counter);

    int follow = pos->ply > 1 ? pos->moveStack[pos->ply - 2]:NOMOVE;
    int fmPiece = pos->pieceStack[pos->ply - 2];
    int fmTo    = TOSQ(follow);


    for(index=0;index<movelist->count;++index){
        move = movelist->moves[index].move;
        pce = pos->pieces[FROMSQ(move)];

        //table move
        if(move==ttMove){
            movelist->moves[index].score = SORT_PV_MOVE;
            continue;
        }

        //quiet moves
        if(!moveIsTactical(pos,move)){
            if(move==pos->searchKillers[0][pos->ply])     movelist->moves[index].score = SORT_KILLER0;
            else if(move==pos->searchKillers[1][pos->ply])movelist->moves[index].score = SORT_KILLER1;
            else if(move==pos->cmtable[!pos->side][cmPiece][cmTo])movelist->moves[index].score = SORT_COUNTER;
            else{
                movelist->moves[index].score = pos->histtable[pos->side][FROMSQ(move)][TOSQ(move)];
                if(counter != NOMOVE && counter != NULLMOVE){
                    movelist->moves[index].score += pos->continuation[0][cmPiece][cmTo][pieceType[pce]][TOSQ(move)];
                }
                if(follow != NOMOVE && follow != NULLMOVE){
                    movelist->moves[index].score += pos->continuation[1][fmPiece][fmTo][pieceType[pce]][TOSQ(move)];
                }
            }
        }

        //capture moves
        else{

            if(!StaticExchangeEvaluation(pos,move,threshold)){
                movelist->moves[index].score = -1;
                continue;
            }

            to = TOSQ(move);
            captured = pieceType[pos->pieces[to]];
            if(move & MVFLAGEP) captured = p_pawn;
            if(move & MVFLAGPROM) captured = p_pawn;

            movelist->moves[index].score = pos->chist[pieceType[pce]][to][captured];
            if(pieceType[PROMOTED(move)]==p_queen)movelist->moves[index].score += 100;
            movelist->moves[index].score += MVVAugment[captured] + SORT_CAPTURE;

        }

    }
}



