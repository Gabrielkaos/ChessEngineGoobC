// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2022-2026 Gabriel Montes

// correction.h
#ifndef CORRECTION_H
#define CORRECTION_H


#include "correction_types.h"

int  correctedStaticEval(const S_BOARD *pos, int rawEval);
void updateCorrectionHistory(S_BOARD *pos, int depth, int diff);
void clearCorrectionHistory(S_BOARD *pos);

#endif