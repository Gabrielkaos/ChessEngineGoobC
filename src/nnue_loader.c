// SPDX-License-Identifier: GPL-3.0-or-later AND AGPL-3.0-or-later
// Copyright (C) 2022-2026 Gabriel Montes

// src/nnue_loader.c
// NNUE inference implementation (see nnue_loader.h for architecture and licensing details)

#define INCBIN_SILENCE_BITCODE_WARNING
#include "incbin.h"

#ifndef EVALFILE
#define EVALFILE "weights/quantised.bin"
#endif

INCBIN(EmbeddedNet, EVALFILE);

#define NNUE_IMPLEMENTATION
#include "nnue_loader.h"