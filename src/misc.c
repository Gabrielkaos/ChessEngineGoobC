
#include "stdio.h"
#include <unistd.h>
#include <string.h>
#include "misc.h"

#ifdef WIN32
#include "windows.h"
#else
#include <sys/time.h>

#endif // WIN32


int getTimeMs(){
#ifdef WIN32
    return GetTickCount();
#else
    struct timeval t;
    gettimeofday(&t,NULL);
    return t.tv_sec*1000+t.tv_usec/1000;
#endif // WIN32
}
