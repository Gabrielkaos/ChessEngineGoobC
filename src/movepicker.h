// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2022-2026 Gabriel Montes

#ifndef MOVEPICKER_H
#define MOVEPICKER_H

#include "defs.h"
#include "board.h"



extern void computeMovePickerThreats(S_MOVEPICKER *mp, const S_BOARD *pos);
extern void initMovePicker(S_MOVEPICKER *mp, S_BOARD *pos, int ttMove);
extern void initSingularMovePicker(S_MOVEPICKER *mp, S_BOARD *pos, int ttMove);
extern void initNoisyMovePicker(S_MOVEPICKER *mp, int threshold, int ttMove);
extern int  selectNextMove(S_MOVEPICKER *mp, S_BOARD *pos, int skipQuiets);

#endif // MOVEPICKER_H