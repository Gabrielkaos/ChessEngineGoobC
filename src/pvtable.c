

#include "pvtable.h"
#include "stdio.h"
#include "some_maths.h"
#include "movegen.h"
#include "board.h"
#include "makemove.h"
#include "io.h"


#define EXTRACT_SCORE(x) ((x & 0xFFFF) - INFINITE_BOUND)
#define EXTRACT_DEPTH(x) ((x >> 16) & 0x7F)
#define EXTRACT_FLAGS(x) ((x >> 24) & 0x3)
#define EXTRACT_MOVE(x) ((int)(x>>26))

#define FOLD_DATA(sc,de,fl,mv) ((sc + INFINITE_BOUND) | (de << 16) | (fl << 24) | ((U64)mv << 26))


void DataCheck(int move){
    int depth = rand() % MAXDEPTH;
    int score = rand() % AB_BOUND;
    int flags = rand() % 3;

    U64 data = FOLD_DATA(score, depth, flags, move);
    printf("Original - move:%s depth:%d score:%d flags:%d\n", PrMove(move),depth,score,flags);
    printf("Created - move:%s depth:%d score:%d flags:%d\n\n", PrMove(EXTRACT_MOVE(data)),EXTRACT_DEPTH(data),EXTRACT_SCORE(data),EXTRACT_FLAGS(data));
}

void TestHASH(char *fen){
    S_BOARD pos[1];
    ParseFEN(fen, pos);

    S_MOVELIST list[1];
    GenerateAllMoves(pos,list);

    int moveNum;
    for(moveNum=0;moveNum<list->count;++moveNum){
        if(!makeMove(pos,list->moves[moveNum].move)){
            continue;
        }

        takeMove(pos);
        DataCheck(list->moves[moveNum].move);
    }
}


S_PVTABLE pvTable[1];

int hashfullTT(S_PVTABLE *table){
    int used = 0;
    int i;

    for(i=0;i<1000;++i){
        used += table->pTable[i].generation==table->generation
                && EXTRACT_FLAGS(table->pTable[i].smp_data) != 0;
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

int getPvLine(const int depth,S_BOARD *pos, S_PVTABLE *table){

    int move=ProbePvTable(pos, table);
    int count =0;

    while(move != NOMOVE && count <depth){

        if(MoveExists(pos,move)){
            makeMove(pos,move);
            pos->pvArray[count++]=move;
        }else{
            break;
        }
        move=ProbePvTable(pos, table);
    }

    while(pos->ply>0){
        takeMove(pos);
    }

    return count;
}

void clearPvTable(S_PVTABLE *table){
    S_PVENTRY *pvEntry;

    for(pvEntry=table->pTable;pvEntry<table->pTable+table->numEntries;pvEntry++){
        pvEntry->generation=0;
        pvEntry->smp_data = 0ULL;
        pvEntry->smp_key  = 0ULL;
        pvEntry->eval = 0;
    }
    
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

void StoreHashEntry(S_BOARD *pos, S_PVTABLE *table,const int move, int score, const int flags, const int depth,const int eval){

    int index=pos->posKey % table->numEntries;
    ASSERT(index>=0 && index <=table->numEntries-1);

    int extracted_depth = EXTRACT_DEPTH(table->pTable[index].smp_data);

    //dont overwrite if lower depth and not exact
    if(flags != HFEXACT &&
       table->pTable[index].smp_key != 0 &&
       depth < extracted_depth - 3){
        return;
       }

    score = valueToTT(score,pos->ply);

    U64 smp_data = FOLD_DATA(score,depth,flags,move);
    U64 smp_key  = pos->posKey ^ smp_data;

    table->pTable[index].eval=eval;
	table->pTable[index].generation = table->generation;

    table->pTable[index].smp_data = smp_data;
    table->pTable[index].smp_key = smp_key;

}

int ProbePvTable(const S_BOARD *pos, S_PVTABLE *table){

    int index=pos->posKey % table->numEntries;
    ASSERT(index>=0 && index<=table->numEntries-1);

    U64 test_key = pos->posKey ^ table->pTable[index].smp_data;

    if(table->pTable[index].smp_key==test_key){
        return EXTRACT_MOVE(table->pTable[index].smp_data);
    }

    return NOMOVE;
}

int ProbeHashEntry(S_BOARD *pos, S_PVTABLE *table, int *move, int *score,int *ttDepth,int *ttBound,int *ttEval) {

	int index = pos->posKey % table->numEntries;
	ASSERT(index>=0 && index<=table->numEntries-1);

    U64 test_key = pos->posKey ^ table->pTable[index].smp_data;

	if( table->pTable[index].smp_key == test_key) {

        table->pTable[index].generation = table->generation;
        *ttEval = table->pTable[index].eval;

		*move    = EXTRACT_MOVE(table->pTable[index].smp_data);
		*ttDepth = EXTRACT_DEPTH(table->pTable[index].smp_data);
		*ttBound = EXTRACT_FLAGS(table->pTable[index].smp_data);
		*score   = EXTRACT_SCORE(table->pTable[index].smp_data);
        return TRUE;
	}

	return FALSE;
}


