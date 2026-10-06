

#include "pvtable.h"
#include "stdio.h"
#include "some_maths.h"
#include "movegen.h"
#include "board.h"
#include "makemove.h"
#include "io.h"
#include "string.h"


#define EXTRACT_SCORE(x) ((int)((x & 0xFFFF) - INFINITE_BOUND))
#define EXTRACT_DEPTH(x) ((int)(int8_t)((x >> 16) & 0xFF))
#define EXTRACT_FLAGS(x) ((int)((x >> 24) & 0x3))
#define EXTRACT_MOVE(x) ((int)(x>>26))

#define FOLD_DATA(sc,de,fl,mv) ((U64)(sc + INFINITE_BOUND) | ((U64)(de & 0xFF) << 16) | ((U64)(fl & 0x3) << 24) | ((U64)mv << 26))


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
    pos->search = alloc_search_thread();
    ParseFEN(fen, pos);

    S_MOVELIST list[1];
    GenerateAllMoves(pos,list);

    int moveNum;
    for(moveNum=0;moveNum<list->count;++moveNum){
        if(!legal(pos, list->moves[moveNum].move)){
            continue;
        }
        StateInfo st;
        makeMove(pos, list->moves[moveNum].move, &st);
        takeMove(pos);
        DataCheck(list->moves[moveNum].move);
    }
    if (pos->search) free_search_thread(pos->search);
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

//one tick per search, wrapping every 256 searches; entries store the same
//uint8_t, so "entry.generation != table->generation" keeps meaning "stale"
void updateAge(S_PVTABLE *table){
    table->generation++;
}

int valueFromTT(int score,int ply){
    if(score > ISMATE)       score -= ply;
    else if(score < -ISMATE) score += ply;
    else if(score > TB_WIN_VALUE - MAXDEPTH && score < TB_WIN_VALUE + MAXDEPTH) score -= ply;
    else if(score < -(TB_WIN_VALUE - MAXDEPTH) && score > -(TB_WIN_VALUE + MAXDEPTH)) score += ply;

    return score;
}

int valueToTT(int score,int ply){
    if(score > ISMATE)       score += ply;
    else if(score < -ISMATE) score -= ply;
    else if(score > TB_WIN_VALUE - MAXDEPTH && score < TB_WIN_VALUE + MAXDEPTH) score += ply;
    else if(score < -(TB_WIN_VALUE - MAXDEPTH) && score > -(TB_WIN_VALUE + MAXDEPTH)) score -= ply;

    return score;
}

void clearPvTable(S_PVTABLE *table){
    memset(table->pTable, 0, table->numEntries * sizeof(S_PVBUCKET));
}

void InitPvTable(S_PVTABLE *table,const int mb,int noisy){
    table->generation = 0;
    int PvSize = 0x100000 * mb;
    int rawEntries = PvSize / sizeof(S_PVBUCKET);
    table->numEntries = floorPowerOf2(rawEntries);

    if(table->pTable != NULL) goob_aligned_free(table->pTable);

    table->pTable=(S_PVBUCKET *) goob_aligned_alloc(64, table->numEntries*sizeof(S_PVBUCKET));

    if(table->pTable==NULL){
        if(noisy)printf("info string PV HashTable Initialization failed with %d MB\n",mb);
        InitPvTable(table,mb/2,noisy);
    }else{
        clearPvTable(table);
        if(noisy)printf("info string PV HashTable initialized size %d MB, entries %d (buckets x%d)\n",mb,table->numEntries,TT_BUCKET_SIZE);
    }
}

void StoreHashEntry(S_BOARD *pos, S_PVTABLE *table,const int move, int score, const int flags, const int depth,const int eval){

    int index = pos->st->posKey & (table->numEntries - 1);
    S_PVBUCKET *bucket = &table->pTable[index];

    score = valueToTT(score,pos->ply);
    U64 new_data = FOLD_DATA(score,depth,flags,move);
    uint32_t new_key = (uint32_t)(pos->st->posKey ^ (pos->st->posKey >> 32) ^ new_data ^ (new_data >> 32));

    // 1. Look for an existing entry with the same key (update in place)
    int replaceIdx = -1;
    int worstScore = INT32_MAX;

    for(int i=0;i<TT_BUCKET_SIZE;++i){
        uint32_t test_key = (uint32_t)(pos->st->posKey ^ (pos->st->posKey >> 32) ^ bucket->entries[i].smp_data ^ (bucket->entries[i].smp_data >> 32));

        if(bucket->entries[i].smp_key == test_key && bucket->entries[i].smp_data != 0){
            // same position — always allowed to overwrite, but keep your
            // existing depth-preference guard for non-exact bounds
            int existingDepth = EXTRACT_DEPTH(bucket->entries[i].smp_data);
            if((flags != HFEXACT || depth < 0) && depth < existingDepth - 3) return;
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
    int index = pos->st->posKey & (table->numEntries - 1);
    S_PVBUCKET *bucket = &table->pTable[index];

    for(int i=0;i<TT_BUCKET_SIZE;++i){
        uint32_t test_key = (uint32_t)(pos->st->posKey ^ (pos->st->posKey >> 32) ^ bucket->entries[i].smp_data ^ (bucket->entries[i].smp_data >> 32));
        if(bucket->entries[i].smp_key == test_key && bucket->entries[i].smp_data != 0)
            return EXTRACT_MOVE(bucket->entries[i].smp_data);
    }
    return NOMOVE;
}

int ProbeHashEntry(S_BOARD *pos, S_PVTABLE *table, int *move, int *score,int *ttDepth,int *ttBound,int *ttEval) {

    int index = pos->st->posKey & (table->numEntries - 1);
    S_PVBUCKET *bucket = &table->pTable[index];

    for(int i=0;i<TT_BUCKET_SIZE;++i){
        uint64_t data = bucket->entries[i].smp_data;
        uint32_t test_key = (uint32_t)(pos->st->posKey ^ (pos->st->posKey >> 32) ^ data ^ (data >> 32));
        if(bucket->entries[i].smp_key == test_key && data != 0){
            bucket->entries[i].generation = table->generation;   // refresh on hit
            *ttEval  = bucket->entries[i].eval;
            *move    = EXTRACT_MOVE(data);
            *ttDepth = EXTRACT_DEPTH(data);
            *ttBound = EXTRACT_FLAGS(data);
            *score   = EXTRACT_SCORE(data);
            return TRUE;
        }
    }
    return FALSE;
}

//probe helper used by the TT replacement tests
static int ttProbe(S_BOARD *pos, S_PVTABLE *table, U64 key){
    pos->st->posKey = key;
    int move, score, depth, bound, eval;
    return ProbeHashEntry(pos, table, &move, &score, &depth, &bound, &eval);
}

//Two deep entries go stale, fresh shallow entries fill the rest of the bucket,
//and one more fresh store must evict the shallower of the two stale entries.
//k holds TT_BUCKET_SIZE + 1 keys that all map to the same bucket.
static int staleEvictionCase(S_BOARD *pos, S_PVTABLE *table, const U64 *k){
    clearPvTable(table);
    pos->st->posKey = k[0]; StoreHashEntry(pos, table, 100, 50, HFEXACT, 10, 40);
    pos->st->posKey = k[1]; StoreHashEntry(pos, table, 101, 60, HFEXACT, 9, 45);
    updateAge(table);                                          // k0,k1 now stale
    for(int i = 2; i <= TT_BUCKET_SIZE; ++i){                  // fresh, shallower than both
        pos->st->posKey = k[i]; StoreHashEntry(pos, table, 100 + i, 70, HFEXACT, i, 55);
    }
    int ok = ttProbe(pos, table, k[0]) && !ttProbe(pos, table, k[1]);
    for(int i = 2; i <= TT_BUCKET_SIZE; ++i) ok = ok && ttProbe(pos, table, k[i]);
    return ok;
}

//Unit test for the bucket replacement logic. Uses its own private table,
//only posKey/ply of the (zeroed) board are ever read by probe/store.
int runTTReplacementTests(void){
    S_PVTABLE table[1];
    S_BOARD pos[1];
    memset(pos, 0, sizeof(S_BOARD));
    memset(table, 0, sizeof(S_PVTABLE));   // InitPvTable frees a non-NULL pTable
    pos->search = alloc_search_thread();
    pos->st = &pos->stateTable[0];
    pos->ply = 0;

    InitPvTable(table, 1, 0);

    //TT_BUCKET_SIZE + 1 keys forced into the SAME bucket (same modulo result)
    U64 k[TT_BUCKET_SIZE + 1];
    for(int i = 0; i <= TT_BUCKET_SIZE; ++i) k[i] = 12345 + (U64)i * (U64)table->numEntries;

    int fails = 0;
    int ok;

    printf("\n== TT bucket replacement tests ==\n");

    //Test 1: a full bucket of distinct entries is all retrievable
    for(int i = 0; i < TT_BUCKET_SIZE; ++i){
        pos->st->posKey = k[i]; StoreHashEntry(pos, table, 100 + i, 50, HFEXACT, 5 + i, 40);
    }
    ok = 1;
    for(int i = 0; i < TT_BUCKET_SIZE; ++i) ok = ok && ttProbe(pos, table, k[i]);
    printf("Test 1 (fill %d slots): %s\n", TT_BUCKET_SIZE, ok ? "PASS" : "FAIL");
    fails += !ok;

    //Test 2: one more distinct key evicts the shallowest (k0, depth 5)
    pos->st->posKey = k[TT_BUCKET_SIZE]; StoreHashEntry(pos, table, 99, 80, HFEXACT, 10, 65);
    ok = !ttProbe(pos, table, k[0]);
    for(int i = 1; i <= TT_BUCKET_SIZE; ++i) ok = ok && ttProbe(pos, table, k[i]);
    printf("Test 2 (evict shallowest): %s\n", ok ? "PASS" : "FAIL");
    fails += !ok;

    //Test 3: updating an existing key must never evict a different key
    clearPvTable(table);
    pos->st->posKey = k[0]; StoreHashEntry(pos, table, 100, 50, HFEXACT, 5, 40);
    pos->st->posKey = k[1]; StoreHashEntry(pos, table, 101, 60, HFEXACT, 8, 45);
    pos->st->posKey = k[0]; StoreHashEntry(pos, table, 999, 55, HFEXACT, 6, 42);   // update k0, deeper
    pos->st->posKey = k[0];
    int move, score, depth, bound, eval;
    ok = ProbeHashEntry(pos, table, &move, &score, &depth, &bound, &eval);
    ok = ok && (move == 999);
    ok = ok && ttProbe(pos, table, k[1]);
    printf("Test 3 (update in place): %s\n", ok ? "PASS" : "FAIL");
    fails += !ok;

    //Test 4: stale entries are preferred for eviction over fresh ones.
    //k0(d10,stale) and k1(d9,stale) both score depth-1000; the shallower
    //stale entry (k1) is evicted first, so k0 and every fresh entry survive.
    ok = staleEvictionCase(pos, table, k);
    printf("Test 4 (stale evicted before fresh): %s\n", ok ? "PASS" : "FAIL");
    fails += !ok;

    //Test 5: same after the 8-bit generation has wrapped around (300 searches)
    for(int i = 0; i < 300; ++i) updateAge(table);
    ok = staleEvictionCase(pos, table, k);
    printf("Test 5 (stale evicted before fresh, generation wrapped): %s\n", ok ? "PASS" : "FAIL");
    fails += !ok;

    //Test 6: hashfull counts this search's entries after the wrap
    //(one entry in each of the 1000 sampled buckets)
    clearPvTable(table);
    for(int i = 0; i < 1000; ++i){
        pos->st->posKey = (U64)i; StoreHashEntry(pos, table, 100, 0, HFEXACT, 5, 0);
    }
    ok = hashfullTT(table) == 1000 / TT_BUCKET_SIZE;
    printf("Test 6 (hashfull after wrap = %d): %s\n", hashfullTT(table), ok ? "PASS" : "FAIL");
    fails += !ok;

    printf(fails == 0 ? "\nAll TT tests PASSED\n\n" : "\nTT tests FAILED (%d)\n\n", fails);

    if (pos->search) free_search_thread(pos->search);
    if (table->pTable) goob_aligned_free(table->pTable);
    return fails == 0;
}


