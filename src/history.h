// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2022-2026 Gabriel Montes

#ifndef HISTORY_H
#define HISTORY_H

#include "board.h"
#include "some_maths.h"
#include "tune.h"

static const int HistoryMax = 16384;
static const int HistoryDivisor = 16384;

INLINE int stat_bonus(int depth) {
    return MIN(HistBonusMax, HistBonusQuad * depth * depth + HistBonusLinear * depth - 118);
}

typedef const int16_t (*ContHistSlice)[64];

INLINE void getContHistSlices(const S_BOARD *pos, ContHistSlice out[CONT_HIST_SLOTS]) {
    static const int offsets[CONT_HIST_SLOTS] = {1, 2, 4, 6};
    for (int slot = 0; slot < CONT_HIST_SLOTS; ++slot) {
        int back = offsets[slot];
        if (pos->ply >= back) {
            int m = pos->search->moveStack[pos->ply - back];
            if (m != NOMOVE && m != NULLMOVE) {
                out[slot] = pos->shared->continuation[slot][pos->search->pieceStack[pos->ply - back]][TOSQ(m)];
                continue;
            }
        }
        out[slot] = NULL;
    }
}

INLINE int getHistoryFast(const S_BOARD *pos, int move, int *fmhist, int *cmhist,
                          U64 threats, const ContHistSlice cont[CONT_HIST_SLOTS]) {
    int from  = FROMSQ(move);
    int to    = TOSQ(move);
    int piece = PTYPE_OF(pos->pieces[from]);

    int threat_from = (threats & (1ULL << from)) ? 1 : 0;
    int threat_to   = (threats & (1ULL << to)) ? 1 : 0;

    *cmhist = cont[0] ? cont[0][piece][to] : 0;
    *fmhist = cont[1] ? cont[1][piece][to] : 0;

    int total = *cmhist + *fmhist
              + pos->shared->histtable[pos->side][threat_from][threat_to][from][to];
    if (cont[2]) total += cont[2][piece][to];
    if (cont[3]) total += cont[3][piece][to];
    return total;
}

extern int getCaptureHistory(S_BOARD *pos,int move, U64 threats);
extern void updateKillers(S_BOARD *pos,int move);
extern void updateHistories(S_BOARD *pos,int *moves,int length, int depth, U64 threats);
extern int getHistory(S_BOARD *pos,int move,int *fmhist,int *cmhist, U64 threats);
extern void updateCaptureHistory(S_BOARD *pos,int best,int *moves,int length,int depth, U64 threats);

//pawn history: [pawn structure key][piece][to], shared across threads
extern int pawnHistIndex(const S_BOARD *pos);
extern int getPawnHistory(S_BOARD *pos,int move);

//low-ply history helpers (per-thread, cleared to 102 every search like Stockfish)
extern void clearLowPlyHistory(S_BOARD *pos);

//continuation history update for a single move with arbitrary bonus/malus
extern void updateContinuationHistories(S_BOARD *pos, int move, int bonus);

#endif // HISTORY_H
