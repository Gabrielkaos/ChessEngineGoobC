#include "defs.h"
#include "stdio.h"
#include "tt_eval.h"

//for EVAL HASH
void clearEvalTable(EVAL_TABLE *eTable){
    EVAL_ENTRY *eEntry;

    for(eEntry=eTable->evalTable;eEntry<eTable->evalTable+eTable->numEntries;eEntry++){
        eEntry->EvalScore=0;
        eEntry->posKey=0ULL;
    }
    //table->newwrite=0;
}

void InitEvalTable(EVAL_TABLE *table,const int mb,int noisy){

    int PvSize = 0x100000 * mb;
    table->numEntries=PvSize/sizeof(EVAL_ENTRY);
    table->numEntries-=2;
    if(table->evalTable != NULL) free(table->evalTable);


    table->evalTable=(EVAL_ENTRY *) malloc(table->numEntries*sizeof(EVAL_ENTRY));

    if(table->evalTable==NULL){
        if(noisy)printf("info string Eval HashTable Initialization failed with %d MB\n",mb);
        InitEvalTable(table,mb/2,noisy);
    }else{
    clearEvalTable(table);
    if(noisy)printf("info string Eval HashTable initialized size %d MB, entries %d\n",mb,table->numEntries);
    }
}

void StoreTTEval(S_BOARD *pos,int Eval){

    int index=pos->posKey % pos->eTable->numEntries;
    ASSERT(index>=0 && index <= pos->eTable->numEntries-1);

	pos->eTable->evalTable[index].EvalScore=Eval;
	pos->eTable->evalTable[index].posKey=pos->posKey;
}

int ProbeTTEval(const S_BOARD *pos){

    int index=pos->posKey % pos->eTable->numEntries;
    ASSERT(index>=0 && index <= pos->eTable->numEntries-1);

    if(pos->eTable->evalTable[index].posKey==pos->posKey){
        ASSERT(pos->eTable->evalTable[index].EvalScore != VALUE_NONE);
        return pos->eTable->evalTable[index].EvalScore;
    }

    return VALUE_NONE;
}


//for PAWNKING HASH
void clearPawnKingTable(PAWNKING_TABLE *eTable){
    PAWNKING_ENTRY *eEntry;
    int i;

    for(eEntry=eTable->paTable;eEntry<eTable->paTable+eTable->numEntries;eEntry++){
        eEntry->whiteScore=0;
        eEntry->blackScore=0;
        //eEntry->pkEval=0;
        eEntry->pawnPosKey=0ULL;
        for(i=0;i<2;++i){
                eEntry->passed[i]=0ULL;
        }
    }
    //table->newwrite=0;
}

void InitPawnKingTable(PAWNKING_TABLE *table,const int mb,int noisy){

    int PvSize = 0x100000 * mb;
    table->numEntries=PvSize/sizeof(PAWNKING_ENTRY);
    table->numEntries-=2;
    if(table->paTable != NULL) free(table->paTable);


    table->paTable=(PAWNKING_ENTRY *) malloc(table->numEntries*sizeof(PAWNKING_ENTRY));

    if(table->paTable==NULL){
        if(noisy)printf("info string Pawn HashTable Initialization failed with %d MB\n",mb);
        InitPawnKingTable(table,mb/2,noisy);
    }else{
    clearPawnKingTable(table);
    if(noisy)printf("info string Pawn HashTable initialized size %d MB, entries %d\n",mb,table->numEntries);
    }
}

void StorePawnKingEval(S_BOARD *pos){

    int index=pos->pkHash % pos->pawnKingTable->numEntries;
    ASSERT(index>=0 && index <= pos->pawnKingTable->numEntries-1);

    //pos->pawnKingTable->paTable[index].pkEval = pos->pkEval;
	pos->pawnKingTable->paTable[index].whiteScore=pos->pawnEval[WHITE];
	pos->pawnKingTable->paTable[index].blackScore=pos->pawnEval[BLACK];
	pos->pawnKingTable->paTable[index].pawnPosKey=pos->pkHash;
	pos->pawnKingTable->paTable[index].passed[BLACK]=pos->passers[BLACK];
	pos->pawnKingTable->paTable[index].passed[WHITE]=pos->passers[WHITE];
}

int ProbePawnKingEval(S_BOARD *pos){

    int index=pos->pkHash % pos->pawnKingTable->numEntries;
    ASSERT(index>=0 && index <= pos->pawnKingTable->numEntries-1);

    if(pos->pawnKingTable->paTable[index].pawnPosKey==pos->pkHash){
        //pos->pkEval =        pos->pawnKingTable->paTable[index].pkEval;
        pos->pawnEval[WHITE]=pos->pawnKingTable->paTable[index].whiteScore;
        pos->pawnEval[BLACK]=pos->pawnKingTable->paTable[index].blackScore;
        pos->passers[WHITE]= pos->pawnKingTable->paTable[index].passed[WHITE];
        pos->passers[BLACK]= pos->pawnKingTable->paTable[index].passed[BLACK];
        return 1;
    }

    return 0;
}




