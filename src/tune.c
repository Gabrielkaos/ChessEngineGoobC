//Runtime-tunable search parameters, only compiled into -DTUNE builds.
//See tune.h for the parameter table and tools/spsa/README.md for the tuner.
#ifdef TUNE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tune.h"
#include "search.h"

#define TP_DEFINE(name, def, lo, hi, c) int name = (def);
SEARCH_PARAMS(TP_DEFINE)
#undef TP_DEFINE

typedef struct {
    const char *name;
    int *value;
    int min, max;
    double cEnd;
} TUNE_PARAM;

#define TP_ENTRY(name, def, lo, hi, c) { #name, &name, (lo), (hi), (c) },
static const TUNE_PARAM tuneParams[] = { SEARCH_PARAMS(TP_ENTRY) };
#undef TP_ENTRY

#define TUNE_COUNT ((int)(sizeof(tuneParams) / sizeof(tuneParams[0])))
#define TUNE_R_END 0.002

void tunePrintUciOptions(void){
    for(int i = 0; i < TUNE_COUNT; ++i){
        printf("option name %s type spin default %d min %d max %d\n",
               tuneParams[i].name, *tuneParams[i].value, tuneParams[i].min, tuneParams[i].max);
    }
}

int tuneSetOption(const char *line){
    const char *prefix = "setoption name ";
    size_t prefixLen = strlen(prefix);
    if(strncmp(line, prefix, prefixLen)) return 0;
    const char *name = line + prefixLen;

    for(int i = 0; i < TUNE_COUNT; ++i){
        size_t len = strlen(tuneParams[i].name);
        if(strncmp(name, tuneParams[i].name, len) || strncmp(name + len, " value ", 7)) continue;

        int v = atoi(name + len + 7);
        if(v < tuneParams[i].min) v = tuneParams[i].min;
        if(v > tuneParams[i].max) v = tuneParams[i].max;
        *tuneParams[i].value = v;

        //the LMR table is precomputed from these two
        if(tuneParams[i].value == &LMRBase || tuneParams[i].value == &LMRDivisor) initLMRTable();
        return 1;
    }
    return 0;
}

void tunePrintSpsa(void){
    for(int i = 0; i < TUNE_COUNT; ++i){
        printf("%s, int, %d, %d, %d, %g, %g\n", tuneParams[i].name, *tuneParams[i].value,
               tuneParams[i].min, tuneParams[i].max, tuneParams[i].cEnd, TUNE_R_END);
    }
}

#endif // TUNE
