#include "defs.h"
#include "stdio.h"
#include "some_maths.h"

int hashfullTT(S_PVTABLE *table){
    int used = 0;
    int i;

    for(i=0;i<1000;++i){
        used += table->pTable[i].generation==table->generation
                && table->pTable[i].flags != 0;
    }

    return used;
}

void updateAge(S_PVTABLE *table){
    table->generation += HFEXACT + 1;
}

int valueFromTT(int score,int ply){
    if(score > ISMATE)       score -= ply;
    else if(score < -ISMATE) score += ply;

    return score;
}

int valueToTT(int score,int ply){
    if(score > ISMATE)       score += ply;
    else if(score < -ISMATE) score -= ply;

    return score;
}

int getPvLine(const int depth,S_BOARD *pos){

    int move=ProbePvTable(pos);
    int count =0;

    while(move != NOMOVE && count <depth){

        if(MoveExists(pos,move)){
            makeMove(pos,move);
            pos->pvArray[count++]=move;
        }else{
            break;
        }
        move=ProbePvTable(pos);
    }

    while(pos->ply>0){
        takeMove(pos);
    }

    return count;
}

void clearPvTable(S_PVTABLE *table){
    S_PVENTRY *pvEntry;

    for(pvEntry=table->pTable;pvEntry<table->pTable+table->numEntries;pvEntry++){
        pvEntry->posKey=0ULL;
        pvEntry->move=NOMOVE;
        pvEntry->depth=0;
        pvEntry->flags=0;
        pvEntry->score=0;
        pvEntry->generation=0;
    }
    //table->newwrite=0;
}

void InitPvTable(S_PVTABLE *table,const int mb,int noisy){
    table->generation = 0;
    int PvSize = 0x100000 * mb;
    table->numEntries=PvSize/sizeof(S_PVENTRY);
    table->numEntries-=2;
    if(table->pTable != NULL) free(table->pTable);


    table->pTable=(S_PVENTRY *) malloc(table->numEntries*sizeof(S_PVENTRY));

    if(table->pTable==NULL){
        if(noisy)printf("info string PV HashTable Initialization failed with %d MB\n",mb);
        InitPvTable(table,mb/2,noisy);
    }else{
    clearPvTable(table);
    if(noisy)printf("info string PV HashTable initialized size %d MB, entries %d\n",mb,table->numEntries);
    }
}

void StoreHashEntry(S_BOARD *pos,const int move, int score, const int flags, const int depth,const int eval){

    int index=pos->posKey % pos->pvTable->numEntries;
    ASSERT(index>=0 && index <=pos->pvTable->numEntries-1);

    //dont overwrite if lower depth and not exact
    if(flags != HFEXACT &&
       pos->pvTable->pTable[index].posKey != 0 &&
       depth < pos->pvTable->pTable[index].depth - 3){
        return;
       }
    /*if(flags != HFEXACT &&
       pos->pvTable->pTable[index].posKey == pos->posKey &&
       depth < pos->pvTable->pTable[index].depth - 2){
        return;
       }*/

    score = valueToTT(score,pos->ply);

    pos->pvTable->pTable[index].eval=eval;
    pos->pvTable->pTable[index].move=move;
    pos->pvTable->pTable[index].posKey=pos->posKey;
    pos->pvTable->pTable[index].flags = flags;
	pos->pvTable->pTable[index].score = score;
	pos->pvTable->pTable[index].depth = depth;
	pos->pvTable->pTable[index].generation = pos->pvTable->generation;
}

int ProbePvTable(const S_BOARD *pos){

    int index=pos->posKey % pos->pvTable->numEntries;
    ASSERT(index>=0 && index<=pos->pvTable->numEntries-1);

    if(pos->pvTable->pTable[index].posKey==pos->posKey){
        return pos->pvTable->pTable[index].move;
    }

    return NOMOVE;
}

int ProbeHashEntry(S_BOARD *pos, int *move, int *score,int *ttDepth,int *ttBound,int *ttEval) {

	int index = pos->posKey % pos->pvTable->numEntries;
	ASSERT(index>=0 && index<=pos->pvTable->numEntries-1);

	if( pos->pvTable->pTable[index].posKey == pos->posKey ) {
        pos->pvTable->pTable[index].generation = pos->pvTable->generation;
		*move    = pos->pvTable->pTable[index].move;
		*ttDepth = pos->pvTable->pTable[index].depth;
		*ttBound = pos->pvTable->pTable[index].flags;
		*score   = pos->pvTable->pTable[index].score;
//		*ttEval  = pos->pvTable->pTable[index].eval;
        return TRUE;
	}

	return FALSE;
}


