
#include "defs.h"
#include "stdio.h"
#include "tt_eval.h"
#include "some_maths.h"

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
    table->numEntries=floorPowerOf2(PvSize/sizeof(EVAL_ENTRY));
    if(table->evalTable != NULL) free(table->evalTable);


    table->evalTable=(EVAL_ENTRY *) malloc(table->numEntries*sizeof(EVAL_ENTRY));

    if(table->evalTable==NULL){
        if (mb <= 1) {
            if (noisy) printf("info string Eval HashTable Initialization failed completely\n");
            table->evalTable = NULL;
            table->numEntries = 0;
            return;
        }
        if(noisy)printf("info string Eval HashTable Initialization failed with %d MB\n",mb);
        InitEvalTable(table,mb/2,noisy);
    }else{
        clearEvalTable(table);
        if(noisy)printf("info string Eval HashTable initialized size %d MB, entries %d\n",mb,table->numEntries);
    }
}

void StoreTTEval(S_BOARD *pos,int Eval){

    int index=pos->st->posKey & (pos->eTable->numEntries - 1);
    ASSERT(index>=0 && index <= pos->eTable->numEntries-1);

	pos->eTable->evalTable[index].EvalScore=Eval;
	pos->eTable->evalTable[index].posKey=pos->st->posKey;
}

int ProbeTTEval(const S_BOARD *pos){

    int index=pos->st->posKey & (pos->eTable->numEntries - 1);
    ASSERT(index>=0 && index <= pos->eTable->numEntries-1);

    if(pos->eTable->evalTable[index].posKey==pos->st->posKey){
        ASSERT(pos->eTable->evalTable[index].EvalScore != VALUE_NONE);
        return pos->eTable->evalTable[index].EvalScore;
    }

    return VALUE_NONE;
}

// Persistent per-thread eval hash tables (allocated once, reused across searches)
EVAL_TABLE threadEvalTable[MAXTHREADS];
static int numAllocatedThreadTables = 0;
int currentEvalHashMB = evalHashMB;

// Ensure persistent tables exist for threads 0..numThreads-1
void EnsureThreadTables(int numThreads){
    if(numThreads <= numAllocatedThreadTables) return;
    for(int i = numAllocatedThreadTables; i < numThreads; i++){
        threadEvalTable[i].evalTable = NULL;
        InitEvalTable(&threadEvalTable[i], currentEvalHashMB, 0);
    }
    numAllocatedThreadTables = numThreads;
}

// Clear all allocated thread tables (ucinewgame / Clear Hash)
void ClearThreadTables(int numThreads){
    int limit = numThreads < numAllocatedThreadTables ? numThreads : numAllocatedThreadTables;
    for(int i = 0; i < limit; i++){
        if(threadEvalTable[i].evalTable != NULL)
            clearEvalTable(&threadEvalTable[i]);
    }
}

// Re-allocate all thread tables with new sizes (setoption EvalHash)
void ReallocThreadTables(int newEvalMB){
    currentEvalHashMB = newEvalMB;
    for(int i = 0; i < numAllocatedThreadTables; i++){
        InitEvalTable(&threadEvalTable[i], newEvalMB, 0);
    }
}

// Free all thread tables at engine exit
void FreeAllThreadTables(void){
    for(int i = 0; i < numAllocatedThreadTables; i++){
        if(threadEvalTable[i].evalTable != NULL){
            free(threadEvalTable[i].evalTable);
            threadEvalTable[i].evalTable = NULL;
        }
    }
    numAllocatedThreadTables = 0;
}

