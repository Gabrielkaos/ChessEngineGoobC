/*
 * pknet_loader.h
 * ==============
 * King + Pawn network inference for GOOB.
 * Replaces EvalPawn + evaluateKingsPawns with a learned function.
 * All other classical eval (pieces, mobility, threats) stays untouched.
 *
 * Architecture:
 *   Input  : 256 floats
 *              [  0.. 63] white pawn squares  (1.0 = pawn present)
 *              [ 64..127] black pawn squares
 *              [128..191] white king (one-hot)
 *              [192..255] black king (one-hot)
 *   Hidden1: 64 neurons (ReLU)
 *   Hidden2: 32 neurons (ReLU)
 *   Output : 1  scalar  (centipawns, White POV, PK component only)
 *
 * ── Integration ──────────────────────────────────────────────────────────────
 *
 * 1. Add pknet_loader.c to your Makefile (or use wildcard - it picks up *.c).
 *
 * 2. In board.h add to S_BOARD:
 *        int usePKNet;
 *
 * 3. In uci.c UCILoop init:
 *        pos->usePKNet = FALSE;
 *
 * 4. In uciPrint() add:
 *        printf("option name UsePKNet type check default false\n");
 *        printf("option name PKNetFile type string default <empty>\n");
 *
 * 5. In UciSetOption() add:
 *
 *    else if (!strncmp(line, "setoption name UsePKNet value ", 30)) {
 *        pos->usePKNet = strstr(line,"true") ? TRUE : FALSE;
 *        printf("info string UsePKNet set to %s\n", pos->usePKNet?"true":"false");
 *        clearEvalTable(pos->eTable);
 *    }
 *    else if (!strncmp(line, "setoption name PKNetFile value ", 31)) {
 *        char path[512]={0};
 *        sscanf(line, "%*s %*s %*s %*s %511s", path);
 *        if (strlen(path)>0 && strcmp(path,"<empty>")!=0) {
 *            pknet_init(path);
 *        }
 *        clearEvalTable(pos->eTable);
 *    }
 *
 * 6. In evaluate.c, inside getClassicalEval(), replace the pawn eval block:
 *
 *    // OLD:
 *    if (!ProbePawnKingEval(pos, eval_info)){
 *        EvalPawn(pos, eval_info);
 *        StorePawnKingEval(pos, eval_info);
 *    }
 *
 *    // NEW:
 *    if (pos->usePKNet && pknet_loaded) {
 *        // network returns full PK score, store in pawnEval so evaluatePieces
 *        // picks it up normally via eval_info->pawnEval[WHITE/BLACK]
 *        int pk = pknet_eval(pos);
 *        eval_info->pawnEval[WHITE] =  pk;
 *        eval_info->pawnEval[BLACK] =  0;  // already baked into WHITE score
 *    } else {
 *        if (!ProbePawnKingEval(pos, eval_info)){
 *            EvalPawn(pos, eval_info);
 *            StorePawnKingEval(pos, eval_info);
 *        }
 *    }
 *    // evaluateKingsPawns is called later inside evaluatePieces - you may want
 *    // to skip it too when usePKNet is on. Simplest: guard it with !pos->usePKNet.
 */

#ifndef PKNET_LOADER_H
#define PKNET_LOADER_H

#include "defs.h"
#include "board.h"

/* ── Dimensions ─────────────────────────────────────────────────────────── */
#define PKNET_INPUT  256
#define PKNET_H1     64
#define PKNET_H2     32
#define PKNET_SCALE  200

/* ── Weights ────────────────────────────────────────────────────────────── */
typedef struct {
    float fc1_weight[PKNET_H1][PKNET_INPUT];
    float fc1_bias  [PKNET_H1];
    float fc2_weight[PKNET_H2][PKNET_H1];
    float fc2_bias  [PKNET_H2];
    float fc3_weight[1][PKNET_H2];
    float fc3_bias  [1];
} PKNET_WEIGHTS;

/* ── API ────────────────────────────────────────────────────────────────── */
int  pknet_init(const char *path);   /* 1 on success, 0 on fail */
int  pknet_eval(const S_BOARD *pos); /* centipawns White POV     */
extern int pknet_loaded;

#endif /* PKNET_LOADER_H */


/* ═══════════════════════════════════════════════════════════════════════════
 * IMPLEMENTATION — define PKNET_IMPLEMENTATION in exactly one .c file
 * ═══════════════════════════════════════════════════════════════════════════ */
#ifdef PKNET_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PKNET_WEIGHTS *g_pk = NULL;
int pknet_loaded = 0;

/* ── Init ───────────────────────────────────────────────────────────────── */
int pknet_init(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[PKNet] Cannot open: %s\n", path);
        return 0;
    }
    if (!g_pk) {
        g_pk = (PKNET_WEIGHTS *)malloc(sizeof(PKNET_WEIGHTS));
        if (!g_pk) { fclose(f); return 0; }
    }
    size_t ok = 1;
    ok &= fread(g_pk->fc1_weight, sizeof(float), PKNET_H1 * PKNET_INPUT, f) == (size_t)(PKNET_H1 * PKNET_INPUT);
    ok &= fread(g_pk->fc1_bias,   sizeof(float), PKNET_H1,               f) == (size_t)PKNET_H1;
    ok &= fread(g_pk->fc2_weight, sizeof(float), PKNET_H2 * PKNET_H1,    f) == (size_t)(PKNET_H2 * PKNET_H1);
    ok &= fread(g_pk->fc2_bias,   sizeof(float), PKNET_H2,               f) == (size_t)PKNET_H2;
    ok &= fread(g_pk->fc3_weight, sizeof(float), PKNET_H2,               f) == (size_t)PKNET_H2;
    ok &= fread(g_pk->fc3_bias,   sizeof(float), 1,                      f) == (size_t)1;
    fclose(f);

    if (!ok) {
        fprintf(stderr, "[PKNet] File corrupt or truncated: %s\n", path);
        free(g_pk); g_pk = NULL;
        return 0;
    }
    pknet_loaded = 1;
    printf("[PKNet] Loaded from %s\n", path);
    return 1;
}

/* ── Eval ───────────────────────────────────────────────────────────────── */
int pknet_eval(const S_BOARD *pos) {
    if (!pknet_loaded) return 0;

    /* Build input vector */
    float inp[PKNET_INPUT];
    memset(inp, 0, sizeof(inp));

    for (int sq = 0; sq < 64; sq++) {
        int p = pos->pieces[sq];
        if      (p == 1)  inp[sq]       = 1.0f;   /* wP */
        else if (p == 7)  inp[64 + sq]  = 1.0f;   /* bP */
        else if (p == 6)  inp[128 + sq] = 1.0f;   /* wK */
        else if (p == 12) inp[192 + sq] = 1.0f;   /* bK */
    }

    /* FC1 */
    float h1[PKNET_H1];
    for (int o = 0; o < PKNET_H1; o++) {
        float s = g_pk->fc1_bias[o];
        for (int i = 0; i < PKNET_INPUT; i++)
            s += g_pk->fc1_weight[o][i] * inp[i];
        h1[o] = s > 0.0f ? s : 0.0f;   /* ReLU */
    }

    /* FC2 */
    float h2[PKNET_H2];
    for (int o = 0; o < PKNET_H2; o++) {
        float s = g_pk->fc2_bias[o];
        for (int i = 0; i < PKNET_H1; i++)
            s += g_pk->fc2_weight[o][i] * h1[i];
        h2[o] = s > 0.0f ? s : 0.0f;   /* ReLU */
    }

    /* FC3 */
    float score = g_pk->fc3_bias[0];
    for (int i = 0; i < PKNET_H2; i++)
        score += g_pk->fc3_weight[0][i] * h2[i];

    score *= PKNET_SCALE;

    /* Always return White POV - classical eval handles side adjustment */
    return (int)score;
}

#endif /* PKNET_IMPLEMENTATION */