// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2022-2026 Gabriel Montes

#ifndef PERFT_H
#define PERFT_H

#include "defs.h"
#include "board.h"

//perft.c
extern void PerftTest(int depth,S_BOARD *pos);
extern void BenchTest(int depth,S_BOARD *pos);
extern void PerftSuiteTest(S_BOARD *pos);
extern U64 countNps(U64 nodes, int time);


#endif