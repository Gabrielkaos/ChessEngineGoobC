#ifndef PKNET_LOADER_H
#define PKNET_LOADER_H

#include "defs.h"
#include "board.h"

extern int pknet_loaded;

int  pknet_init(const char *path);
int  pknet_eval(const S_BOARD *pos);

#endif