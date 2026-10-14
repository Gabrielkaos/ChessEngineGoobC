
#include "stdio.h"
#include <string.h>
#include <stdint.h>
#include "misc.h"

#if defined(_WIN32) || defined(WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

uint64_t getTimeMs(void){
#if defined(_WIN32) || defined(WIN32)
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
#endif
}
