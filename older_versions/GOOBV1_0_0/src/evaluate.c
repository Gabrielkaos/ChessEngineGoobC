#include "defs.h"
#include "stdio.h"
#include "math.h"

/* Functions*/
static inline int max(int num1, int num2){
    if (num1>num2){
        return num1;
    }else{
        return num2;
    }
};
static inline int MaterialDraw(const S_BOARD *pos) {
    if (!pos->numPieces[wR] && !pos->numPieces[bR] && !pos->numPieces[wQ] && !pos->numPieces[bQ]) {
	  if (!pos->numPieces[bB] && !pos->numPieces[wB]) {
	      if (pos->numPieces[wN] < 3 && pos->numPieces[bN] < 3) {  return TRUE; }
	  } else if (!pos->numPieces[wN] && !pos->numPieces[bN]) {
	     if (abs(pos->numPieces[wB] - pos->numPieces[bB]) < 2) { return TRUE; }
	  } else if ((pos->numPieces[wN] < 3 && !pos->numPieces[wB]) || (pos->numPieces[wB] == 1 && !pos->numPieces[wN])) {
	    if ((pos->numPieces[bN] < 3 && !pos->numPieces[bB]) || (pos->numPieces[bB] == 1 && !pos->numPieces[bN]))  { return TRUE; }
	  }
	} else if (!pos->numPieces[wQ] && !pos->numPieces[bQ]) {
        if (pos->numPieces[wR] == 1 && pos->numPieces[bR] == 1) {
            if ((pos->numPieces[wN] + pos->numPieces[wB]) < 2 && (pos->numPieces[bN] + pos->numPieces[bB]) < 2)	{ return TRUE; }
        } else if (pos->numPieces[wR] == 1 && !pos->numPieces[bR]) {
            if ((pos->numPieces[wN] + pos->numPieces[wB] == 0) && (((pos->numPieces[bN] + pos->numPieces[bB]) == 1) || ((pos->numPieces[bN] + pos->numPieces[bB]) == 2))) { return TRUE; }
        } else if (pos->numPieces[bR] == 1 && !pos->numPieces[wR]) {
            if ((pos->numPieces[bN] + pos->numPieces[bB] == 0) && (((pos->numPieces[wN] + pos->numPieces[wB]) == 1) || ((pos->numPieces[wN] + pos->numPieces[wB]) == 2))) { return TRUE; }
        }
    }
  return FALSE;
}
static inline int ForceKing(int oppKing,int friendKing){
        int evaluation=0;
        int oppRank=ranksBoard[oppKing];
        int oppFile=filesBoard[oppKing];


        int oppDstToCenterFile=max(3-oppFile,oppFile-4);
        int oppDstToCenterRank=max(3-oppRank,oppRank-4);
        int oppDstFromCenter=oppDstToCenterFile+oppDstToCenterRank;
        evaluation+=oppDstFromCenter;

        int frRank = ranksBoard[friendKing];
        int frFile = filesBoard[friendKing];

        int dstBetweenKingFile=abs(frFile-oppFile);
        int dstBetweenKingRank=abs(frRank-oppRank);
        int dstBetweenKings=dstBetweenKingFile+dstBetweenKingRank;
        evaluation-=dstBetweenKings;

        return evaluation*24;

}

//#define ENDGAME (pieceVal[wR]+(2*pieceVal[wN])+(4*pieceVal[wP])+pieceVal[wK])

static inline int isItEndGame(const S_BOARD *pos,int myColor){

    if(myColor==WHITE){
        if(pos->numPieces[bQ]==0 && pos->numPieces[bP]<=6 && pos->bigPiece[BLACK]<=4){
            return TRUE;
        }
        if(pos->minPiece[BLACK]==0 && pos->majPiece[BLACK]<=1){
            return TRUE;
        }
        if(pos->majPiece[BLACK]==0 && pos->minPiece[BLACK]<=2){
            return TRUE;
        }
        if(pos->bigPiece[BLACK]<=3){
            return TRUE;
        }
    }
    else{
        if(pos->numPieces[wQ]==0 && pos->numPieces[wP]<=6 && pos->bigPiece[WHITE]<=4){
            return TRUE;
        }
        if(pos->minPiece[WHITE]==0 && pos->majPiece[WHITE]<=1){
            return TRUE;
        }
        if(pos->majPiece[WHITE]==0 && pos->minPiece[WHITE]<=2){
            return TRUE;
        }
        if(pos->bigPiece[WHITE]<=3){
            return TRUE;
        }
    }

    return FALSE;
}

const int IsolatedPawn=-7;
const int pawnPassed[8]={0,5,20,30,50,70,100};
const int rookOpenFile=15;
const int rookSemiOpenFile=10;
const int queenOpenFile=4;
const int queenSemiOpenFile=3;
const int bishopPair=30;
//const int knightPair=10;
//const int rookPair=15;
const int semiOpenKing=-6;
const int OpenKing=-10;
const int pieceNearKing=5;
const int doubledPawn=-7;
const int pieceTrapped=-10;
const int queenTrapped=-3;
const int majPieceBigger=20;
const int minPieceBigger=20;
const int hasMorePawns=5;
const int queenlessAndBetter=8;
const int outpost_ranks[8]={0,0,2,5,6,12,8,5};

const int knight_movemap[64]={
    2,3,4,4,4,4,3,2,
    3,4,6,6,6,6,4,3,
    4,6,8,8,8,8,6,4,
    4,6,8,8,8,8,6,4,
    4,6,8,8,8,8,6,4,
    4,6,8,8,8,8,6,4,
    3,4,6,6,6,6,4,3,
    2,3,4,4,4,4,3,2
};
const int bishop_movemap[64]={
    7,7,7,7,7,7,7,7,
    7,9,9,9,9,9,9,7,
    7,9,11,11,11,11,9,7,
    7,9,11,13,13,11,9,7,
    7,9,11,13,13,11,9,7,
    7,9,11,11,11,11,9,7,
    7,9,9,9,9,9,9,7,
    7,7,7,7,7,7,7,7
};

const int PawnTableE[64] = {
0	,	0	,	0	,	0	,	0	,	0	,	0	,	0	,
0	,	0	,	0	,	-5	,	-5	,	0	,	0	,	0	,
5	,	0	,	0	,	5	,	5	,	0	,	0	,	5	,
0	,	0	,	10	,	20	,	20	,	10	,	0	,	0	,
5	,	5	,	10	,	10	,	10	,	10	,	5	,	5	,
20	,	20	,	20	,	30	,	30	,	20	,	20	,	20	,
50	,	50	,	50	,	50	,	50	,	50	,	50	,	50	,
10	,	10	,	10	,	10	,	10	,	10	,	10	,	10
};
const int KingE[64] = {
	0	,	0	,	0	,	0	,	0	,	0	,	0	,	0	,
	0   ,	5	,	5	,	5	,	5	,	5	,	5	,	0	,
	0	,	5	,	10	,	10	,	10	,	10	,	5	,	0	,
	0	,	5	,	10	,	15	,	15	,	10	,	5	,	0	,
	0	,	5	,	10	,	15	,	15	,	10	,	5	,	0	,
	0	,	5	,	10	,	10	,	10	,	10	,	5	,	0	,
	0	,	5	,	5	,	5	,	5   ,   5	,	5	,	0	,
	0	,	0	,	0	,	0	,	0	,	0	,	0	,	0
};
const int PawnTable[64] = {
0	,	0	,	0	,	0	,	0	,	0	,	0	,	0	,
5	,	5	,	0	,	-5	,	-5	,	0	,	5	,	5	,
5	,	0	,	0	,	5	,	5	,	0	,	0	,	5	,
0	,	0	,	10	,	20	,	20	,	10	,	0	,	0	,
5	,	5	,	10	,	10	,	10	,	10	,	5	,	5	,
10	,	10	,	10	,	20	,	20	,	10	,	10	,	10	,
20	,	20	,	20	,	20	,	20	,	20	,	20	,	20	,
10	,	10	,	10	,	10	,	10	,	10	,	10	,	10
};
const int KnightTable[64] = {
-5	,	-10	,	0	,	0	,	0	,	0	,	-10	,	-5	,
-5	,	0	,	0	,	5	,	5	,	0	,	0	,	-5	,
-5	,	0	,	10	,	5	,	5	,	10	,	0	,	-5	,
-5	,	0	,	10	,	15	,	15	,	10	,	0	,	-5	,
-5	,	10	,	15	,	15	,	15	,	15	,	10	,	-5	,
-5	,	10	,	10	,	10	,	10	,	10	,	10	,	-5	,
0	,	0	,	5	,	5	,	5	,	5	,	0	,	0	,
0	,	0	,	0	,	0	,	0	,	0	,	0	,	0
};
const int BishopTable[64] = {
0	,	0	,	-5	,	0	,	0	,	-5	,	0	,	0	,
0	,	5	,	0	,	5	,	5	,	0	,	5	,	0	,
0	,	0	,	10	,	5	,	5	,	10	,	0	,	0	,
0	,	10	,	15	,	20	,	20	,	15	,	10	,	0	,
0	,	10	,	15	,   20	,	20	,	15	,	10	,	0	,
0	,	0	,	15	,	10	,	10	,	15	,	0	,	0	,
0	,	5	,	0	,	10	,	10	,	0	,	5	,	0	,
5	,	0	,	0	,	0	,	0	,	0	,	0	,	5
};
const int RookTable[64] = {
0	,	0	,	10	,	10	,	10	,	10	,	0	,	0	,
0	,	0	,	5	,	10	,	10	,	5	,	0	,	0	,
0	,	0	,	5	,	10	,	10	,	5	,	0	,	0	,
0	,	0	,	5	,	10	,	10	,	5	,	0	,	0	,
0	,	0	,	5	,	10	,	10	,	5	,	0	,	0	,
0	,	0	,	5	,	10	,	10	,	5	,	0	,	0	,
20	,	20	,	20	,	20	,	20	,	20	,	20	,	20	,
0	,	0	,	5	,	10	,	10	,	5	,	0	,	0
};
const int KingO[64] = {
	0	,	5	,	30	,	-10	,	-10	,	-5	,	30	,	5	,
	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,
	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,
	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,
	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,
	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,
	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,
	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,	-10	,	-10
};
const int QueenTable[64]={
    0	,	0	,	0	,	0	,	0	,	0	,	0	,	0	,
    -5	,	0	,	0	,	0	,	0	,	0	,	0	,	-5	,
    0	,	0	,	0	,	0	,	0	,	0	,	0	,	0	,
    0	,	0	,	0	,	10	,	10	,	0	,	0	,	0	,
    0	,	0	,	5	,	20	,	20	,	5	,	0	,	0	,
    0	,	0	,	10	,	10	,	10	,	10	,	0	,	0	,
    5	,	5	,	10	,	20	,	20	,	10	,	5	,	5	,
    0	,	0	,	5	,	5	,	5	,	5	,	0	,	0

};



int EvalPosition(const S_BOARD *pos){

    int pce;
    int score;
    score=pos->material[WHITE]-pos->material[BLACK];
    int sq;
    U64 pawnsBOTH=(pos->bitboards[wP] | pos->bitboards[bP]);

    int isEndGameBlack=isItEndGame(pos,BLACK);
    int isEndGameWhite=isItEndGame(pos,WHITE);

    if(!pos->numPieces[wP] && !pos->numPieces[bP]&&MaterialDraw(pos)==TRUE){
        return 0;
    }

    for(sq=0;sq<64;sq++){
        pce=pos->pieces[sq];

        if(pce==EMPTY){
            continue;
        }
            switch(pce){
            //white pawns
            case wP:
                //king heat map
                if(GETBIT(king_attacks[pos->kingSq[BLACK]],sq)){
                    score+=pieceNearKing;
                }

                //piece square tables
                if(isEndGameWhite){
                    score+=PawnTableE[sq];
                }else score+=PawnTable[sq];

                //doubled pawn
                if(pos->pieces[sq+8]==wP){
                    score+=doubledPawn;
                }

                //isolated pawn
                if((IsolatedMask[sq] & pos->bitboards[wP])==0){
                    score+=IsolatedPawn;
                }
                //passed pawn
                if((WhitePassedMark[sq] & pos->bitboards[bP])==0){
                    score+=pawnPassed[ranksBoard[sq]];
                }
                break;
            //black pawns
            case bP:

                //king heatmap
                if(GETBIT(king_attacks[pos->kingSq[WHITE]],sq)){
                    score-=pieceNearKing;
                }

                //piece square tables
                if(isEndGameBlack){
                    score-=PawnTableE[MIRROR64(sq)];
                }else score-=PawnTable[MIRROR64(sq)];

                 //doubled pawn
                if(pos->pieces[sq-8]==bP){
                    score-=doubledPawn;
                }

                //isolated pawn
                if((IsolatedMask[sq] & pos->bitboards[bP])==0){
                    score-=IsolatedPawn;
                }
                //passed pawn
                if((BlackPassedMask[sq] & pos->bitboards[wP])==0){
                    score-=pawnPassed[7-ranksBoard[sq]];
                }
                break;
            //white knights
            case wN:
                //piece square tables
                score+=KnightTable[sq];
                //movemap
                score+=knight_movemap[sq];

                //king heatmap
                if(GETBIT(king_attacks[pos->kingSq[BLACK]],sq)){
                    score+=pieceNearKing;
                }

                //piece trapped
                if(COUNTBIT(knight_attacks[sq])<=1){
                    score+=pieceTrapped;
                }

                //semi outpost
                if(filesBoard[sq]>FILE_A && filesBoard[sq] < FILE_H &&
                   (pos->pieces[sq-7]==wP || pos->pieces[sq-9]==wP)){
                            score+=outpost_ranks[ranksBoard[sq]];
                }
                //outposts
                if((IsolatedMask[sq] & pos->bitboards[bP])==0){
                    score+=outpost_ranks[ranksBoard[sq]]+2;
                }

                break;
            //black knights
            case bN:
                //piece square tables
                score-=KnightTable[MIRROR64(sq)];
                //movemap
                score-=knight_movemap[sq];

                //king heatmap
                if(GETBIT(king_attacks[pos->kingSq[WHITE]],sq)){
                    score-=pieceNearKing;
                }

                //piece trapped
                if(COUNTBIT(knight_attacks[sq])<=1){
                    score-=pieceTrapped;
                }

                //semi outposts
                if(filesBoard[sq]>FILE_A && filesBoard[sq] < FILE_H &&
                   (pos->pieces[sq+7]==bP || pos->pieces[sq+9]==bP )){
                            score-=outpost_ranks[7-ranksBoard[sq]];
                }

                //outposts
                if((IsolatedMask[sq] & pos->bitboards[wP])==0){
                    score-=outpost_ranks[7-ranksBoard[sq]]+2;
                }

                break;
            //white rook
            case wR:

                //piece square tables
                score+=RookTable[sq];

                //piece trapped
                if(COUNTBIT(get_rook_attacks(sq,pos->occupancy[BOTH]))<=4){
                    score+=pieceTrapped;
                }
                //king heatmap
                if(GETBIT(king_attacks[pos->kingSq[BLACK]],sq)){
                    score+=pieceNearKing;
                }

                //open files
                if(!(pawnsBOTH & FileBBMask[filesBoard[sq]])){
                    score+=rookOpenFile;
                }else if(!(pos->bitboards[wP] & FileBBMask[filesBoard[sq]])){
                    score+=rookSemiOpenFile;
                }
                break;
            //black rook
            case bR:

                //piece square tables
                score-=RookTable[MIRROR64(sq)];

                //piece trapped
                if(COUNTBIT(get_rook_attacks(sq,pos->occupancy[BOTH]))<=4){
                    score-=pieceTrapped;
                }

                //king heatmap
                if(GETBIT(king_attacks[pos->kingSq[WHITE]],sq)){
                    score-=pieceNearKing;
                }

                //open files
                if(!(pawnsBOTH & FileBBMask[filesBoard[sq]])){
                    score-=rookOpenFile;
                }else if(!(pos->bitboards[bP] & FileBBMask[filesBoard[sq]])){
                    score-=rookSemiOpenFile;
                }
                break;
            //white bishop
            case wB:

                //piece square tables
                score+=BishopTable[sq];
                //movemap
                score+=bishop_movemap[sq];

                //piece trapped
                if(COUNTBIT(get_bishop_attacks(sq,pos->occupancy[BOTH]))<=4){
                    score+=pieceTrapped;
                }

                //heatmap
                if(GETBIT(king_attacks[pos->kingSq[BLACK]],(sq))){
                    score+=pieceNearKing;
                }

                //outpost
                if(filesBoard[sq]>FILE_A && filesBoard[sq] < FILE_H &&
                   (pos->pieces[sq-7]==wP || pos->pieces[sq-9]==wP )){
                            score+=outpost_ranks[ranksBoard[sq]];
                }

                if((IsolatedMask[sq] & pos->bitboards[bP])==0){
                    score+=outpost_ranks[ranksBoard[sq]]+2;
                }


                break;
            //black bishop
            case bB:
                //piece square tables
                score-=BishopTable[MIRROR64(sq)];
                //movemap
                score-=bishop_movemap[sq];

                //piece trapped
                if(COUNTBIT(get_bishop_attacks(sq,pos->occupancy[BOTH]))<=4){
                    score-=pieceTrapped;
                }
                //heatmap
                if(GETBIT(king_attacks[pos->kingSq[WHITE]],(sq))){
                    score-=pieceNearKing;
                }
                //outpost
                if(filesBoard[sq]>FILE_A && filesBoard[sq] < FILE_H &&
                   (pos->pieces[sq+7]==bP || pos->pieces[sq+9]==bP )){
                            score-=outpost_ranks[7-ranksBoard[sq]];
                }
                if((IsolatedMask[sq] & pos->bitboards[wP])==0){
                    score-=outpost_ranks[7-ranksBoard[sq]]+2;
                }

                break;
            //white queen
            case wQ:

                //piece square tables
                score+=QueenTable[(sq)];

                //king heat map
                if(GETBIT(king_attacks[pos->kingSq[BLACK]],(sq))){
                    score+=pieceNearKing+5;
                }

                //queen trapped
                if(COUNTBIT(get_queen_attacks(sq,pos->occupancy[BOTH]))<=8){
                    score+=queenTrapped;
                }

                if(!(FileBBMask[filesBoard[sq]] & pawnsBOTH)){
                    score+=queenOpenFile;
                }else if(!(FileBBMask[filesBoard[sq]] & pos->bitboards[wP])){
                    score+=queenSemiOpenFile;
                }
                break;
            //black queen
            case bQ:

                //piece square tables
                score-=QueenTable[MIRROR64((sq))];

                //king heat map
                if(GETBIT(king_attacks[pos->kingSq[WHITE]],(sq))){
                    score-=pieceNearKing+5;
                }

                //queen trapped
                if(COUNTBIT(get_queen_attacks(sq,pos->occupancy[BOTH]))<=8){
                    score-=queenTrapped;
                }

                if(!(FileBBMask[filesBoard[sq]] & pawnsBOTH)){
                    score-=queenOpenFile;
                }else if(!(FileBBMask[filesBoard[sq]] & pos->bitboards[bP])){
                    score-=queenSemiOpenFile;
                }
                break;
            //white king
            case wK:
                if(!(pawnsBOTH & FileBBMask[filesBoard[sq]])){
                    score+=OpenKing;
                }else if(!(pos->bitboards[wP] & FileBBMask[filesBoard[sq]])){
                    score+=semiOpenKing;
                }
                if(isEndGameWhite){
                    score+=KingE[(sq)];
                }else {
                    score+=KingO[(sq)];
                    //no pawns in front of king
                    if(filesBoard[sq]==FILE_G && ranksBoard[sq]==RANK_1 &&
                       (!(GETBIT(pos->bitboards[wP],(sq+8)))&&
                                !(GETBIT(pos->bitboards[wP],(sq+7))))){
                            score+=OpenKing;
                    }
                    if((filesBoard[sq]==FILE_C || filesBoard[sq]==FILE_B) && ranksBoard[sq]==RANK_1 &&
                       (!(GETBIT(pos->bitboards[wP],(sq+8))) &&
                            !(GETBIT(pos->bitboards[wP],(sq+9))))){
                            score+=OpenKing;
                    }
                }
                break;
            //black king
            case bK:
                if(!(pawnsBOTH & FileBBMask[filesBoard[sq]])){
                    score-=OpenKing;
                }else if(!(pos->bitboards[bP] & FileBBMask[filesBoard[sq]])){
                    score-=semiOpenKing;
                }
                if(isEndGameBlack){
                    score-=KingE[MIRROR64((sq))];
                }else {
                    score-=KingO[MIRROR64((sq))];
                    //no pawns in front of king
                    if(filesBoard[sq]==FILE_G && ranksBoard[sq]==RANK_8 &&
                       (!(GETBIT(pos->bitboards[bP],(sq-8))) &&
                            !(GETBIT(pos->bitboards[bP],(sq-9))))){
                            score-=OpenKing;
                        }
                    if((filesBoard[sq]==FILE_C || filesBoard[sq]==FILE_B) && ranksBoard[sq]==RANK_8
                       &&(!(GETBIT(pos->bitboards[bP],(sq-8))) &&
                                !(GETBIT(pos->bitboards[bP],(sq-7))))){
                            score-=OpenKing;
                        }
                    }
                break;
            }
    }

    //bishop pair
    if(pos->numPieces[wB]>1)score+=bishopPair;
    if(pos->numPieces[bB]>1)score-=bishopPair;

    if (pos->side==WHITE){
        //majpiece superiority
        if(pos->majPiece[WHITE] > pos->majPiece[BLACK]){
            score+=majPieceBigger;
            if(pos->numPieces[wQ]==0 && pos->numPieces[bQ]==0){
                score+=queenlessAndBetter;
            }
        }
        //minpiece superiority
        if(pos->minPiece[WHITE] > pos->minPiece[BLACK]){
            score+=minPieceBigger;
            if(pos->numPieces[wQ]==0 && pos->numPieces[bQ]==0){
                score+=queenlessAndBetter;
            }
        }

        if(isEndGameWhite){
            if(pos->numPieces[wP] > pos->numPieces[bP]){
                score+=hasMorePawns;
            }
            score+=ForceKing(pos->kingSq[BLACK],pos->kingSq[WHITE]);
        }

        //bonus side to move
        //score+=sideToMoveBonus;
        return score;
    }else{
        //majpiece superiority
        if(pos->majPiece[WHITE] < pos->majPiece[BLACK]){
            score-=majPieceBigger;
            if(pos->numPieces[wQ]==0 && pos->numPieces[bQ]==0){
                score-=queenlessAndBetter;
            }
        }
        //minpiece superiority
        if(pos->minPiece[WHITE] < pos->minPiece[BLACK]){
            score-=minPieceBigger;
            if(pos->numPieces[wQ]==0 && pos->numPieces[bQ]==0){
                score-=queenlessAndBetter;
            }
        }

        if(isEndGameBlack){
            if(pos->numPieces[wP] < pos->numPieces[bP]){
                score-=hasMorePawns;
            }
            score-=ForceKing(pos->kingSq[WHITE],pos->kingSq[BLACK]);
        }
        //bonus side to move
        //score-=sideToMoveBonus;
        return -score;
    }

}
