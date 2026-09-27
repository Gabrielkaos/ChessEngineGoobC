
#include "stdio.h"
#include <string.h>
#include "misc.h"

#if defined(_WIN32) || defined(WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

int getTimeMs(){
#if defined(_WIN32) || defined(WIN32)
    return GetTickCount();
#else
    struct timeval t;
    gettimeofday(&t,NULL);
    return t.tv_sec*1000+t.tv_usec/1000;
#endif
}
