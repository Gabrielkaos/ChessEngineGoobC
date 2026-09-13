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

    //Lazy SMP best-thread voting record, published by each worker as it
    //completes a full iterative-deepening level
    int voteScore;
    int voteDepth;
    int votePvLineCount;
    S_PVLINE completedPv;
} THREAD_SEARCH_WORKER;


extern void EnsureThreadPool(int numThreads);
extern void FreeThreadPool(void);

#endif