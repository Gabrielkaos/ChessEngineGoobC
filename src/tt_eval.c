
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

    int index=pos->posKey & (pos->eTable->numEntries - 1);
    ASSERT(index>=0 && index <= pos->eTable->numEntries-1);

	pos->eTable->evalTable[index].EvalScore=Eval;
	pos->eTable->evalTable[index].posKey=pos->posKey;
}

int ProbeTTEval(const S_BOARD *pos){

    int index=pos->posKey & (pos->eTable->numEntries - 1);
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
    table->numEntries=floorPowerOf2(PvSize/sizeof(PAWNKING_ENTRY));
    if(table->paTable != NULL) free(table->paTable);


    table->paTable=(PAWNKING_ENTRY *) malloc(table->numEntries*sizeof(PAWNKING_ENTRY));

    if(table->paTable==NULL){
        if (mb <= 1) {
            if (noisy) printf("info string Pawn HashTable Initialization failed completely\n");
            table->paTable = NULL;
            table->numEntries = 0;
            return;
        }
        if(noisy)printf("info string Pawn HashTable Initialization failed with %d MB\n",mb);
        InitPawnKingTable(table,mb/2,noisy);
    }else{
    clearPawnKingTable(table);
    if(noisy)printf("info string Pawn HashTable initialized size %d MB, entries %d\n",mb,table->numEntries);
    }
}

void StorePawnKingEval(S_BOARD *pos, EVAL_INFO *eval_info){

    int index=pos->pkHash & (pos->pawnKingTable->numEntries - 1);
    ASSERT(index>=0 && index <= pos->pawnKingTable->numEntries-1);

	pos->pawnKingTable->paTable[index].whiteScore=eval_info->pawnEval[WHITE];
	pos->pawnKingTable->paTable[index].blackScore=eval_info->pawnEval[BLACK];
	pos->pawnKingTable->paTable[index].pawnPosKey=pos->pkHash;
	pos->pawnKingTable->paTable[index].passed[BLACK]=eval_info->passers[BLACK];
	pos->pawnKingTable->paTable[index].passed[WHITE]=eval_info->passers[WHITE];
}

int ProbePawnKingEval(S_BOARD *pos, EVAL_INFO *eval_info){

    int index=pos->pkHash & (pos->pawnKingTable->numEntries - 1);
    ASSERT(index>=0 && index <= pos->pawnKingTable->numEntries-1);

    if(pos->pawnKingTable->paTable[index].pawnPosKey==pos->pkHash){
        eval_info->pawnEval[WHITE]=pos->pawnKingTable->paTable[index].whiteScore;
        eval_info->pawnEval[BLACK]=pos->pawnKingTable->paTable[index].blackScore;
        eval_info->passers[WHITE] =pos->pawnKingTable->paTable[index].passed[WHITE];
        eval_info->passers[BLACK] =pos->pawnKingTable->paTable[index].passed[BLACK];
        return 1;
    }

    return 0;
}

// Persistent per-thread pawn/eval hash tables (allocated once, reused across searches)
PAWNKING_TABLE threadPawnTable[MAXTHREADS];
EVAL_TABLE threadEvalTable[MAXTHREADS];
static int numAllocatedThreadTables = 0;
int currentPawnHashMB = pawnHashMB;
int currentEvalHashMB = evalHashMB;

// Ensure persistent tables exist for threads 0..numThreads-1
void EnsureThreadTables(int numThreads){
    if(numThreads <= numAllocatedThreadTables) return;
    for(int i = numAllocatedThreadTables; i < numThreads; i++){
        threadPawnTable[i].paTable = NULL;
        InitPawnKingTable(&threadPawnTable[i], currentPawnHashMB, 0);
        threadEvalTable[i].evalTable = NULL;
        InitEvalTable(&threadEvalTable[i], currentEvalHashMB, 0);
    }
    numAllocatedThreadTables = numThreads;
}

// Clear all allocated thread tables (ucinewgame / Clear Hash)
void ClearThreadTables(int numThreads){
    int limit = numThreads < numAllocatedThreadTables ? numThreads : numAllocatedThreadTables;
    for(int i = 0; i < limit; i++){
        if(threadPawnTable[i].paTable != NULL)
            clearPawnKingTable(&threadPawnTable[i]);
        if(threadEvalTable[i].evalTable != NULL)
            clearEvalTable(&threadEvalTable[i]);
    }
}

// Re-allocate all thread tables with new sizes (setoption PawnHash/EvalHash)
void ReallocThreadTables(int newPawnMB, int newEvalMB){
    currentPawnHashMB = newPawnMB;
    currentEvalHashMB = newEvalMB;
    for(int i = 0; i < numAllocatedThreadTables; i++){
        InitPawnKingTable(&threadPawnTable[i], newPawnMB, 0);
        InitEvalTable(&threadEvalTable[i], newEvalMB, 0);
    }
}

// Free all thread tables at engine exit
void FreeAllThreadTables(void){
    for(int i = 0; i < numAllocatedThreadTables; i++){
        if(threadPawnTable[i].paTable != NULL){
            free(threadPawnTable[i].paTable);
            threadPawnTable[i].paTable = NULL;
        }
        if(threadEvalTable[i].evalTable != NULL){
            free(threadEvalTable[i].evalTable);
            threadEvalTable[i].evalTable = NULL;
        }
    }
    numAllocatedThreadTables = 0;
}

