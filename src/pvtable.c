

#include "pvtable.h"
#include "stdio.h"
#include "some_maths.h"
#include "board.h"
#include "string.h"

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

void clearPvTable(S_PVTABLE *table){
    if(!table->pTable) return;
    memset(table->pTable, 0, (size_t)table->numEntries * sizeof(S_PVBUCKET));
}

void InitPvTable(S_PVTABLE *table,const int mb,int noisy){
    if(mb < 1){
        if(table->pTable != NULL) goob_aligned_free(table->pTable);
        table->pTable = NULL;
        table->numEntries = 0;
        table->hashMask = 0;
        if(noisy) printf("info string PV HashTable Initialization failed (size < 1 MB)\n");
        return;
    }
    table->generation = 0;
    size_t PvSize = (size_t)mb * 1024 * 1024;
    size_t rawEntries = PvSize / sizeof(S_PVBUCKET);
    table->numEntries = (int)floorPowerOf2(rawEntries);
    table->hashMask = (uint32_t)(table->numEntries - 1);

    if(table->pTable != NULL) goob_aligned_free(table->pTable);

    table->pTable=(S_PVBUCKET *) goob_aligned_alloc(64, (size_t)table->numEntries * sizeof(S_PVBUCKET));

    if(table->pTable==NULL){
        if(noisy)printf("info string PV HashTable Initialization failed with %d MB\n",mb);
        InitPvTable(table,mb/2,noisy);
    }else{
        clearPvTable(table);
        if(noisy)printf("info string PV HashTable initialized size %d MB, entries %d (buckets x%d)\n",mb,table->numEntries,TT_BUCKET_SIZE);
    }
}

//probe helper used by the TT replacement tests
static int ttProbe(S_BOARD *pos, S_PVTABLE *table, U64 key){
    pos->st->posKey = key;
    int move, score, depth, bound, eval;
    return ProbeHashEntry(pos, table, &move, &score, &depth, &bound, &eval);
}

//Unit test for the bucket replacement logic. Uses its own private table,
//only posKey/ply of the (zeroed) board are ever read by probe/store.
int runTTReplacementTests(void){
    S_PVTABLE table[1];
    S_BOARD pos[1];
    memset(pos, 0, sizeof(S_BOARD));
    memset(table, 0, sizeof(S_PVTABLE));
    pos->search = alloc_search_thread();
    pos->st = &pos->stateTable[0];
    pos->ply = 0;

    InitPvTable(table, 1, 0);

    //five keys forced into the SAME bucket (same modulo result)
    U64 base = 12345;
    U64 k0 = base;
    U64 k1 = base + (U64)table->numEntries;
    U64 k2 = base + 2ULL * (U64)table->numEntries;
    U64 k3 = base + 3ULL * (U64)table->numEntries;
    U64 k4 = base + 4ULL * (U64)table->numEntries;

    int fails = 0;
    int ok;

    printf("\n== TT bucket replacement tests ==\n");

    //Test 1: distinct entries fit in bucket
    pos->st->posKey = k0; StoreHashEntry(pos, table, 100, 50, HFEXACT, 5, 40);
    pos->st->posKey = k1; StoreHashEntry(pos, table, 101, 60, HFEXACT, 8, 45);
    pos->st->posKey = k2; StoreHashEntry(pos, table, 102, 70, HFEXACT, 3, 55);
    pos->st->posKey = k3; StoreHashEntry(pos, table, 103, 75, HFEXACT, 6, 60);
    ok = ttProbe(pos, table, k0) && ttProbe(pos, table, k1) && ttProbe(pos, table, k2) && ttProbe(pos, table, k3);
    printf("Test 1 (fill %d slots): %s\n", TT_BUCKET_SIZE, ok ? "PASS" : "FAIL");
    fails += !ok;

    //Test 2: extra distinct key evicts the shallowest (k2, depth 3)
    pos->st->posKey = k4; StoreHashEntry(pos, table, 104, 80, HFEXACT, 10, 65);
    ok = ttProbe(pos, table, k0) && ttProbe(pos, table, k1)
         && !ttProbe(pos, table, k2) && ttProbe(pos, table, k3) && ttProbe(pos, table, k4);
    printf("Test 2 (evict shallowest): %s\n", ok ? "PASS" : "FAIL");
    fails += !ok;

    //Test 3: updating an existing key must never evict a different key
    clearPvTable(table);
    pos->st->posKey = k0; StoreHashEntry(pos, table, 100, 50, HFEXACT, 5, 40);
    pos->st->posKey = k1; StoreHashEntry(pos, table, 101, 60, HFEXACT, 8, 45);
    pos->st->posKey = k0; StoreHashEntry(pos, table, 999, 55, HFEXACT, 6, 42);   // update k0, deeper
    pos->st->posKey = k0;
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
    pos->st->posKey = k0; StoreHashEntry(pos, table, 100, 50, HFEXACT, 10, 40);
    pos->st->posKey = k1; StoreHashEntry(pos, table, 101, 60, HFEXACT, 8, 45);
    updateAge(table);                                          // k0,k1 now stale
    pos->st->posKey = k2; StoreHashEntry(pos, table, 102, 70, HFEXACT, 2, 55);  // fresh, shallow
    pos->st->posKey = k3; StoreHashEntry(pos, table, 103, 75, HFEXACT, 4, 60);  // fresh
    pos->st->posKey = k4; StoreHashEntry(pos, table, 104, 80, HFEXACT, 3, 65);  // forces one eviction
    ok = ttProbe(pos, table, k0) && !ttProbe(pos, table, k1)
         && ttProbe(pos, table, k2) && ttProbe(pos, table, k3) && ttProbe(pos, table, k4);
    printf("Test 4 (stale evicted before fresh): %s\n", ok ? "PASS" : "FAIL");
    fails += !ok;

    //Test 5: after 65+ searches (generation wraps past 256), stale eviction and hashfull still work
    clearPvTable(table);
    for (int i = 0; i < 65; ++i) updateAge(table);
    pos->st->posKey = k0; StoreHashEntry(pos, table, 100, 50, HFEXACT, 10, 40);
    pos->st->posKey = k1; StoreHashEntry(pos, table, 101, 60, HFEXACT, 8, 45);
    updateAge(table);
    pos->st->posKey = k2; StoreHashEntry(pos, table, 102, 70, HFEXACT, 2, 55);
    pos->st->posKey = k3; StoreHashEntry(pos, table, 103, 75, HFEXACT, 4, 60);
    pos->st->posKey = k4; StoreHashEntry(pos, table, 104, 80, HFEXACT, 3, 65);
    ok = ttProbe(pos, table, k0) && !ttProbe(pos, table, k1)
         && ttProbe(pos, table, k2) && ttProbe(pos, table, k3) && ttProbe(pos, table, k4);
    for (int i = 0; i < 1000; ++i) {
        pos->st->posKey = (U64)i; StoreHashEntry(pos, table, 100, 0, HFEXACT, 5, 0);
    }
    ok = ok && (hashfullTT(table) == 1000 / TT_BUCKET_SIZE);
    printf("Test 5 (after 65+ searches wrap, stale eviction + hashfull=%d): %s\n", hashfullTT(table), ok ? "PASS" : "FAIL");
    fails += !ok;

    printf(fails == 0 ? "\nAll TT tests PASSED\n\n" : "\nTT tests FAILED (%d)\n\n", fails);

    if (pos->search) free_search_thread(pos->search);
    if (table->pTable) goob_aligned_free(table->pTable);
    return fails == 0;
}


