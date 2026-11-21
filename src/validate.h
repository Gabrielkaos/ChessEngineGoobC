// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2022-2026 Gabriel Montes

#ifndef VALIDATE_H
#define VALIDATE_H

#include "board.h"

//validate.c
extern int moveValid(const int move);
extern int SqOnBoard(const int sq);
extern int SideValid(const int side);
extern int PieceValidEmpty(const int pce);
extern int PieceValid(const int pce);

#endif