

#include "pvtable.h"
#include "stdio.h"
#include "some_maths.h"
#include "movegen.h"
#include "board.h"
#include "makemove.h"
#include "io.h"
#include "string.h"


#define EXTRACT_SCORE(x) ((int)((x & 0xFFFF) - INFINITE_BOUND))
#define EXTRACT_DEPTH(x) ((int)((x >> 16) & 0x7F))
#define EXTRACT_FLAGS(x) ((int)((x >> 24) & 0x3))
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
    int sampleBuckets = MIN(1000, table->numEntries);

    for(int i=0;i<sampleBuckets;++i){
        for(int j=0;j<TT_BUCKET_SIZE;++j){
            used += table->pTable[i].entries[j].generation==table->generation
                    && table->pTable[i].entries[j].smp_data != 0;
        }
    }

    return used * 1000 / (sampleBuckets * TT_BUCKET_SIZE);
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
    int startPly = pos->ply;

    while(move != NOMOVE && count <depth){

        if(MoveExists(pos,move)){
            makeMove(pos,move);
            pos->pvArray[count++]=move;
        }else{
            break;
        }
        move=ProbePvTable(pos, table);
    }

    while(pos->ply > startPly){
        takeMove(pos);
    }

    return count;
}

void clearPvTable(S_PVTABLE *table){
    memset(table->pTable, 0, table->numEntries * sizeof(S_PVBUCKET));
}

void InitPvTable(S_PVTABLE *table,const int mb,int noisy){
    table->generation = 0;
    int PvSize = 0x100000 * mb;
    int rawEntries = PvSize / sizeof(S_PVBUCKET);
    table->numEntries = floorPowerOf2(rawEntries);

    if(table->pTable != NULL) free(table->pTable);

    table->pTable=(S_PVBUCKET *) malloc(table->numEntries*sizeof(S_PVBUCKET));

    if(table->pTable==NULL){
        if(noisy)printf("info string PV HashTable Initialization failed with %d MB\n",mb);
        InitPvTable(table,mb/2,noisy);
    }else{
        clearPvTable(table);
        if(noisy)printf("info string PV HashTable initialized size %d MB, entries %d (buckets x%d)\n",mb,table->numEntries,TT_BUCKET_SIZE);
    }
}

void StoreHashEntry(S_BOARD *pos, S_PVTABLE *table,const int move, int score, const int flags, const int depth,const int eval){

    int index = pos->posKey & (table->numEntries - 1);
    S_PVBUCKET *bucket = &table->pTable[index];

    score = valueToTT(score,pos->ply);
    U64 new_data = FOLD_DATA(score,depth,flags,move);
    U64 new_key  = pos->posKey ^ new_data;

    // 1. Look for an existing entry with the same key (update in place)
    int replaceIdx = -1;
    int worstScore = INT32_MAX;

    for(int i=0;i<TT_BUCKET_SIZE;++i){
        U64 test_key = pos->posKey ^ bucket->entries[i].smp_data;

        if(bucket->entries[i].smp_key == test_key && bucket->entries[i].smp_data != 0){
            // same position — always allowed to overwrite, but keep your
            // existing depth-preference guard for non-exact bounds
            int existingDepth = EXTRACT_DEPTH(bucket->entries[i].smp_data);
            if(flags != HFEXACT && depth < existingDepth - 3) return;
            replaceIdx = i;
            break;
        }

        // 2. Track the least valuable slot as a fallback replacement target.
        // Score = depth, penalized heavily for being from an older generation
        // (stale entries should be evicted first regardless of their depth)
        int entryDepth = EXTRACT_DEPTH(bucket->entries[i].smp_data);
        int genPenalty = (bucket->entries[i].generation != table->generation) ? 1000 : 0;
        int replacementScore = entryDepth - genPenalty;

        // empty slot (smp_data==0) is always the best replacement candidate
        if(bucket->entries[i].smp_data == 0){ replaceIdx = i; worstScore = -1000000; }
        else if(replaceIdx == -1 || replacementScore < worstScore){
            if(replaceIdx == -1 || bucket->entries[replaceIdx].smp_data != 0){
                worstScore = replacementScore;
                replaceIdx = i;
            }
        }
    }

    bucket->entries[replaceIdx].eval = eval;
    bucket->entries[replaceIdx].generation = table->generation;
    bucket->entries[replaceIdx].smp_data = new_data;
    bucket->entries[replaceIdx].smp_key = new_key;
}

int ProbePvTable(const S_BOARD *pos, S_PVTABLE *table){
    int index = pos->posKey & (table->numEntries - 1);
    S_PVBUCKET *bucket = &table->pTable[index];

    for(int i=0;i<TT_BUCKET_SIZE;++i){
        U64 test_key = pos->posKey ^ bucket->entries[i].smp_data;
        if(bucket->entries[i].smp_key == test_key && bucket->entries[i].smp_data != 0)
            return EXTRACT_MOVE(bucket->entries[i].smp_data);
    }
    return NOMOVE;
}

int ProbeHashEntry(S_BOARD *pos, S_PVTABLE *table, int *move, int *score,int *ttDepth,int *ttBound,int *ttEval) {

    int index = pos->posKey & (table->numEntries - 1);
    S_PVBUCKET *bucket = &table->pTable[index];

    for(int i=0;i<TT_BUCKET_SIZE;++i){
        U64 test_key = pos->posKey ^ bucket->entries[i].smp_data;
        if(bucket->entries[i].smp_key == test_key && bucket->entries[i].smp_data != 0){
            bucket->entries[i].generation = table->generation;   // refresh on hit
            *ttEval  = bucket->entries[i].eval;
            *move    = EXTRACT_MOVE(bucket->entries[i].smp_data);
            *ttDepth = EXTRACT_DEPTH(bucket->entries[i].smp_data);
            *ttBound = EXTRACT_FLAGS(bucket->entries[i].smp_data);
            *score   = EXTRACT_SCORE(bucket->entries[i].smp_data);
            return TRUE;
        }
    }
    return FALSE;
}

//probe helper used by the TT replacement tests
static int ttProbe(S_BOARD *pos, S_PVTABLE *table, U64 key){
    pos->posKey = key;
    int move, score, depth, bound, eval;
    return ProbeHashEntry(pos, table, &move, &score, &depth, &bound, &eval);
}

//Unit test for the bucket replacement logic. Uses its own private table,
//only posKey/ply of the (zeroed) board are ever read by probe/store.
int runTTReplacementTests(void){
    S_PVTABLE table[1];
    S_BOARD pos[1];
    memset(pos, 0, sizeof(S_BOARD));
    pos->ply = 0;

    InitPvTable(table, 1, 0);

    //four keys forced into the SAME bucket (same modulo result)
    U64 base = 12345;
    U64 k0 = base;
    U64 k1 = base + (U64)table->numEntries;
    U64 k2 = base + 2ULL * (U64)table->numEntries;
    U64 k3 = base + 3ULL * (U64)table->numEntries;

    int fails = 0;
    int ok;

    printf("\n== TT bucket replacement tests ==\n");

    //Test 1: three distinct entries fit in a 3-slot bucket
    pos->posKey = k0; StoreHashEntry(pos, table, 100, 50, HFEXACT, 5, 40);
    pos->posKey = k1; StoreHashEntry(pos, table, 101, 60, HFEXACT, 8, 45);
    pos->posKey = k2; StoreHashEntry(pos, table, 102, 70, HFEXACT, 3, 55);
    ok = ttProbe(pos, table, k0) && ttProbe(pos, table, k1) && ttProbe(pos, table, k2);
    printf("Test 1 (fill 3 slots): %s\n", ok ? "PASS" : "FAIL");
    fails += !ok;

    //Test 2: 4th distinct key evicts the shallowest (k2, depth 3)
    pos->posKey = k3; StoreHashEntry(pos, table, 103, 80, HFEXACT, 10, 65);
    ok = ttProbe(pos, table, k0) && ttProbe(pos, table, k1)
         && !ttProbe(pos, table, k2) && ttProbe(pos, table, k3);
    printf("Test 2 (evict shallowest): %s\n", ok ? "PASS" : "FAIL");
    fails += !ok;

    //Test 3: updating an existing key must never evict a different key
    clearPvTable(table);
    pos->posKey = k0; StoreHashEntry(pos, table, 100, 50, HFEXACT, 5, 40);
    pos->posKey = k1; StoreHashEntry(pos, table, 101, 60, HFEXACT, 8, 45);
    pos->posKey = k0; StoreHashEntry(pos, table, 999, 55, HFEXACT, 6, 42);   // update k0, deeper
    pos->posKey = k0;
    int move, score, depth, bound, eval;
    ok = ProbeHashEntry(pos, table, &move, &score, &depth, &bound, &eval);
    ok = ok && (move == 999);
    ok = ok && ttProbe(pos, table, k1);
    printf("Test 3 (update in place): %s\n", ok ? "PASS" : "FAIL");
    fails += !ok;

    //Test 4: stale entries are preferred for eviction over fresh ones.
    //k0(d10,stale) and k1(d8,stale) both score depth-1000; the shallower
    //stale entry (k1) is evicted first, so k0 must survive.
    clearPvTable(table);
    pos->posKey = k0; StoreHashEntry(pos, table, 100, 50, HFEXACT, 10, 40);
    pos->posKey = k1; StoreHashEntry(pos, table, 101, 60, HFEXACT, 8, 45);
    updateAge(table);                                          // k0,k1 now stale
    pos->posKey = k2; StoreHashEntry(pos, table, 102, 70, HFEXACT, 2, 55);  // fresh, shallow
    pos->posKey = k3; StoreHashEntry(pos, table, 103, 80, HFEXACT, 3, 65);  // forces one eviction
    ok = ttProbe(pos, table, k0) && !ttProbe(pos, table, k1)
         && ttProbe(pos, table, k2) && ttProbe(pos, table, k3);
    printf("Test 4 (stale evicted before fresh): %s\n", ok ? "PASS" : "FAIL");
    fails += !ok;

    printf(fails == 0 ? "\nAll TT tests PASSED\n\n" : "\nTT tests FAILED (%d)\n\n", fails);

    free(table->pTable);
    return fails == 0;
}


