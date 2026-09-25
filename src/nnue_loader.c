// src/nnue_loader.c
// NNUE inference implementation (see nnue_loader.h for architecture and licensing details)

#include "incbin.h"

#ifndef EVALFILE
#define EVALFILE "weights/quantised.bin"
#endif

INCBIN(EmbeddedNet, EVALFILE);

#define NNUE_IMPLEMENTATION
#include "nnue_loader.h"