#ifndef PVTABLE_H
#define PVTABLE_H

#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include "defs.h"
#include "board.h"

#define EXTRACT_SCORE(x) ((int)((x & 0xFFFF) - INFINITE_BOUND))
#define EXTRACT_DEPTH(x) ((int)(int8_t)((x >> 16) & 0xFF))
#define EXTRACT_FLAGS(x) ((int)((x >> 24) & 0x3))
#define EXTRACT_MOVE(x) ((int)(x >> 26))

#define FOLD_DATA(sc,de,fl,mv) ((U64)(sc + INFINITE_BOUND) | ((U64)(de & 0xFF) << 16) | ((U64)(fl & 0x3) << 24) | ((U64)mv << 26))

// pvtable.c exports
extern int hashfullTT(S_PVTABLE *table);
extern void updateAge(S_PVTABLE *table);
extern void InitPvTable(S_PVTABLE *table, const int mb, int noisy);
extern void clearPvTable(S_PVTABLE *table);
extern void TestHASH(char *fen);
extern int runTTReplacementTests(void);

// Fast inlined mate / Syzygy score scaling
INLINE int valueFromTT(int score, int ply) {
    if (__builtin_expect(abs(score) >= TB_WIN_VALUE - MAXDEPTH, 0)) {
        if (score > ISMATE)       score -= ply;
        else if (score < -ISMATE) score += ply;
        else if (score > TB_WIN_VALUE - MAXDEPTH && score < TB_WIN_VALUE + MAXDEPTH) score -= ply;
        else if (score < -(TB_WIN_VALUE - MAXDEPTH) && score > -(TB_WIN_VALUE + MAXDEPTH)) score += ply;
    }
    return score;
}

INLINE int valueToTT(int score, int ply) {
    if (__builtin_expect(abs(score) >= TB_WIN_VALUE - MAXDEPTH, 0)) {
        if (score > ISMATE)       score -= ply;
        else if (score < -ISMATE) score += ply;
        else if (score > TB_WIN_VALUE - MAXDEPTH && score < TB_WIN_VALUE + MAXDEPTH) score -= ply;
        else if (score < -(TB_WIN_VALUE - MAXDEPTH) && score > -(TB_WIN_VALUE + MAXDEPTH)) score += ply;
    }
    return score;
}

// Software prefetch: load the TT bucket into L1 cache before we need it.
INLINE void prefetchTT(S_PVTABLE *table, U64 key) {
    int index = key & table->hashMask;
    __builtin_prefetch(&table->pTable[index]);
}

// Direct pointer probe: avoids 7-parameter passing and stack spills.
// Returns pointer to the matching entry on hit, or NULL on miss.
INLINE S_PVENTRY* ProbeTTEntry(S_PVTABLE *table, U64 key) {
    int index = key & table->hashMask;
    S_PVBUCKET *bucket = &table->pTable[index];
    uint32_t posFold = (uint32_t)(key ^ (key >> 32));

    for (int i = 0; i < TT_BUCKET_SIZE; ++i) {
        uint64_t data = bucket->entries[i].smp_data;
        if (!data) continue;
        uint32_t test_key = posFold ^ (uint32_t)(data ^ (data >> 32));
        if (bucket->entries[i].smp_key == test_key) {
            bucket->entries[i].generation = table->generation;
            return &bucket->entries[i];
        }
    }
    return NULL;
}

// Backward-compatible probe wrapper for legacy callers and unit tests
INLINE int ProbeHashEntry(S_BOARD *pos, S_PVTABLE *table, int *move, int *score, int *ttDepth, int *ttBound, int *ttEval) {
    S_PVENTRY *entry = ProbeTTEntry(table, pos->st->posKey);
    if (entry) {
        uint64_t data = entry->smp_data;
        *ttEval  = entry->eval;
        *move    = EXTRACT_MOVE(data);
        *ttDepth = EXTRACT_DEPTH(data);
        *ttBound = EXTRACT_FLAGS(data);
        *score   = EXTRACT_SCORE(data);
        return TRUE;
    }
    return FALSE;
}

// Probe for PV line extraction
INLINE int ProbePvTable(const S_BOARD *pos, S_PVTABLE *table) {
    S_PVENTRY *entry = ProbeTTEntry(table, pos->st->posKey);
    return entry ? EXTRACT_MOVE(entry->smp_data) : NOMOVE;
}

// Inlined store entry with streamlined replacement search
INLINE void StoreHashEntry(S_BOARD *pos, S_PVTABLE *table, const int move, int score, const int flags, const int depth, const int eval) {
    int index = pos->st->posKey & table->hashMask;
    S_PVBUCKET *bucket = &table->pTable[index];

    score = valueToTT(score, pos->ply);
    U64 new_data = FOLD_DATA(score, depth, flags, move);
    uint32_t posFold = (uint32_t)(pos->st->posKey ^ (pos->st->posKey >> 32));
    uint32_t new_key = posFold ^ (uint32_t)(new_data ^ (new_data >> 32));

    int replaceIdx = -1;
    int worstScore = INT_MAX;

    for (int i = 0; i < TT_BUCKET_SIZE; ++i) {
        uint64_t data = bucket->entries[i].smp_data;
        if (!data) {
            replaceIdx = i;
            worstScore = -1000000;
            continue;
        }

        uint32_t test_key = posFold ^ (uint32_t)(data ^ (data >> 32));
        if (bucket->entries[i].smp_key == test_key) {
            int existingDepth = EXTRACT_DEPTH(data);
            if ((flags != HFEXACT || depth < 0) && depth < existingDepth - 3) return;
            replaceIdx = i;
            break;
        }

        if (worstScore == -1000000) continue;

        int entryDepth = EXTRACT_DEPTH(data);
        int genPenalty = (bucket->entries[i].generation != table->generation) ? 1000 : 0;
        int replacementScore = entryDepth - genPenalty;

        if (replaceIdx == -1 || replacementScore < worstScore) {
            worstScore = replacementScore;
            replaceIdx = i;
        }
    }

    bucket->entries[replaceIdx].eval = eval;
    bucket->entries[replaceIdx].generation = table->generation;
    bucket->entries[replaceIdx].smp_data = new_data;
    bucket->entries[replaceIdx].smp_key = new_key;
}

#endif //PVTABLE_H