#ifndef THREAD_H
#define THREAD_H

#include "defs.h"
#include "board.h"


#define MAXTHREADS 2048

typedef struct{
    S_PVTABLE *ttable;
    S_SEARCHINFO *info;
    S_BOARD *originalPos;
} THREAD_DATA;

typedef struct{
    S_PVTABLE *ttable;
    S_SEARCHINFO *info;
    S_BOARD *originalPos;
    
    int ponderMove, bestMove;
    int threadNumber;
} THREAD_SEARCH_WORKER;


#endif