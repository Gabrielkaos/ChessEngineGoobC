

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pknet_loader.h"

#define PK_INPUT 256
#define PK_H1    128
#define PK_H2    64

int pknet_loaded = 0;

static float pk_scale = 400.0f;
static float pk_w1[PK_H1][PK_INPUT];
static float pk_b1[PK_H1];
static float pk_w2[PK_H2][PK_H1];
static float pk_b2[PK_H2];
static float pk_w3[1][PK_H2];
static float pk_b3[1];

static inline float pk_relu(float x) { return x > 0.0f ? x : 0.0f; }

int pknet_init(const char *path) { 
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[PKNet] Cannot open %s\n", path);
        return 0;
    }

    // read scale factor written first by export_bin
    fread(&pk_scale, sizeof(float), 1, f);

    // fc1.weight [H1 x INPUT], fc1.bias [H1]
    fread(pk_w1, sizeof(float), PK_H1 * PK_INPUT, f);
    fread(pk_b1, sizeof(float), PK_H1,            f);

    // fc2.weight [H2 x H1], fc2.bias [H2]
    fread(pk_w2, sizeof(float), PK_H2 * PK_H1, f);
    fread(pk_b2, sizeof(float), PK_H2,          f);

    // fc3.weight [1 x H2], fc3.bias [1]
    fread(pk_w3, sizeof(float), PK_H2, f);
    fread(pk_b3, sizeof(float), 1,     f);

    fclose(f);
    pknet_loaded = 1;
    fprintf(stderr, "[PKNet] Loaded %s  scale=%.0f\n", path, pk_scale);
    return 1;
 }
int pknet_eval(const S_BOARD *pos) { 
    // ── Build input vector (257 floats) ──────────────────────────────────────
    float input[PK_INPUT];
    memset(input, 0, sizeof(input));

    // sq 0=a1 ... 63=h8, same layout as fen_to_features in pknet.py
    for (int sq = 0; sq < 64; sq++) {
        int piece = pos->pieces[sq];
        if      (piece == wP) input[sq]       = 1.0f;  // white pawns  [0..63]
        else if (piece == bP) input[64 + sq]  = 1.0f;  // black pawns  [64..127]
        else if (piece == wK) input[128 + sq] = 1.0f;  // white king   [128..191]
        else if (piece == bK) input[192 + sq] = 1.0f;  // black king   [192..255]
    }
    // side to move feature
    // input[256] = (pos->side == WHITE) ? 1.0f : 0.0f;

    // ── Forward pass ─────────────────────────────────────────────────────────
    float h1[PK_H1], h2[PK_H2];

    // layer 1
    for (int i = 0; i < PK_H1; i++) {
        float acc = pk_b1[i];
        for (int j = 0; j < PK_INPUT; j++)
            acc += pk_w1[i][j] * input[j];
        h1[i] = pk_relu(acc);
    }

    // layer 2
    for (int i = 0; i < PK_H2; i++) {
        float acc = pk_b2[i];
        for (int j = 0; j < PK_H1; j++)
            acc += pk_w2[i][j] * h1[j];
        h2[i] = pk_relu(acc);
    }

    // output layer
    float out = pk_b3[0];
    for (int j = 0; j < PK_H2; j++)
        out += pk_w3[0][j] * h2[j];

    // multiply by scale to get back to centipawns (White's POV)
    return (int)(out * pk_scale);
 }