#include "defs.h"
#include "stdio.h"

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
    }
    //table->newwrite=0;
}

void InitPvTable(S_PVTABLE *table,const int mb){

    int PvSize = 0x100000 * mb;
    table->numEntries=PvSize/sizeof(S_PVENTRY);
    table->numEntries-=2;
    if(table->pTable != NULL) free(table->pTable);


    table->pTable=(S_PVENTRY *) malloc(table->numEntries*sizeof(S_PVENTRY));

    if(table->pTable==NULL){
        printf("HashTable Initialization failed with %d MB\n",mb/2);
        InitPvTable(table,mb/2);
    }else{
    clearPvTable(table);
    printf("HashTable initialized size %d MB\n",mb);
    }
}

void StorePvTable(S_BOARD *pos,const int move, int score, const int flags, const int depth){

    int index=pos->posKey % pos->pvTable->numEntries;

    /*if( pos->pvTable->pTable[index].posKey == 0) {
		pos->pvTable->newwrite++;
	} else {
		pos->pvTable->overwrite++;
	}*/

	if(score > ISMATE) score += pos->ply;
    else if(score < -ISMATE) score -= pos->ply;

    pos->pvTable->pTable[index].move=move;
    pos->pvTable->pTable[index].posKey=pos->posKey;
    pos->pvTable->pTable[index].flags = flags;
	pos->pvTable->pTable[index].score = score;
	pos->pvTable->pTable[index].depth = depth;
}

int ProbePvTable(const S_BOARD *pos){

    int index=pos->posKey % pos->pvTable->numEntries;

    if(pos->pvTable->pTable[index].posKey==pos->posKey){
        return pos->pvTable->pTable[index].move;
    }

    return NOMOVE;
}

int ProbeHashEntry(S_BOARD *pos, int *move, int *score, int alpha, int beta, int depth) {

	int index = pos->posKey % pos->pvTable->numEntries;

	if( pos->pvTable->pTable[index].posKey == pos->posKey ) {
		*move = pos->pvTable->pTable[index].move;
		if(pos->pvTable->pTable[index].depth >= depth){
			//pos->pvTable->hit++;


			*score = pos->pvTable->pTable[index].score;
			if(*score > ISMATE) *score -= pos->ply;
            else if(*score < -ISMATE) *score += pos->ply;

			switch(pos->pvTable->pTable[index].flags) {


                case HFALPHA: if(*score<=alpha) {
                    *score=alpha;
                    return TRUE;
                    }
                    break;
                case HFBETA: if(*score>=beta) {
                    *score=beta;
                    return TRUE;
                    }
                    break;
                case HFEXACT:
                    return TRUE;
                    break;
            }
		}
	}

	return FALSE;
}
