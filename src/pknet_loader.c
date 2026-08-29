#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pknet_loader.h"
#include "bitboards.h"  // poplsb
#include "evaluate.h"   // MakeScore / S

/*
   Ethereal-style PK network.

   Architecture:  [224 inputs] -> [32 hidden] -> [2 outputs (MG, EG)]

   Inputs are the pawn + king placement only (everything else is classical
   eval). Each input is a 0/1 indicator for "piece of type T on square s for
   colour C", indexed exactly like Ethereal's computePKNetworkIndex():

       idx = 112 * colour + (48 if KING) + sq - (8 if PAWN)

   so white pawns use 0..47, white kings 48..111, black pawns 112..159,
   black kings 160..223  (224 total). Pawns are restricted to ranks 2-7
   (sq 8..55) since sq-8 makes rank-1/8 pawns negative and illegal.

   Forward pass follows Ethereal: since the inputs are 0/1, the hidden
   layer is a plain (unactivated) dot product, and we only loop over the
   ~2 kings + ~16 pawns actually on the board using a TRANSPOSED weight
   matrix inputWeights[idx][neuron]. A ReLU gate is applied only when
   accumulating the hidden neurons into the output (hidden >= 0).

   The two outputs are returned as a tapered MakeScore(MG, EG).
*/

#define PK_INPUT 224
#define PK_H1    32
#define PK_OUT   2

/* "PK22" little-endian, rejects the old 256x128x64x1 binary format */
#define PK_MAGIC 0x32324B50u

int pknet_loaded = 0;

static float pk_scale = 1.0f;

/* transposed: row = input index (0..223), column = hidden neuron (0..31) */
static float pk_w1[PK_INPUT][PK_H1];
static float pk_b1[PK_H1];
static float pk_w2[PK_OUT][PK_H1];
static float pk_b2[PK_OUT];

static inline int pkIndex(int colour, int piece, int sq) {
    int idx = 112 * colour;
    if (piece == KING) idx += 48;
    if (piece == PAWN) sq  -= 8;
    return idx + sq;
}

int pknet_init(const char *path) {

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[PKNet] Cannot open %s\n", path);
        return 0;
    }

    uint32_t magic = 0;
    if (fread(&magic, sizeof(uint32_t), 1, f) != 1 || magic != PK_MAGIC) {
        fclose(f);
        fprintf(stderr, "[PKNet] %s: bad magic (not a PK22 net)\n", path);
        return 0;
    }

    if (fread(&pk_scale, sizeof(float), 1, f) != 1) { fclose(f); return 0; }

    if (fread(pk_w1, sizeof(float), PK_INPUT * PK_H1, f) != PK_INPUT * PK_H1) { fclose(f); return 0; }
    if (fread(pk_b1, sizeof(float), PK_H1,            f) != PK_H1)            { fclose(f); return 0; }

    if (fread(pk_w2, sizeof(float), PK_OUT * PK_H1, f) != PK_OUT * PK_H1) { fclose(f); return 0; }
    if (fread(pk_b2, sizeof(float), PK_OUT,          f) != PK_OUT)        { fclose(f); return 0; }

    fclose(f);
    pknet_loaded = 1;
    fprintf(stderr, "[PKNet] Loaded %s  scale=%.0f\n", path, pk_scale);
    return 1;
}

int pknet_eval(const S_BOARD *pos) {

    float h[PK_H1];
    for (int i = 0; i < PK_H1; i++)
        h[i] = pk_b1[i];

    /* Piece-type bitboards give us exactly the sparse set of pieces the
       network cares about, mirroring Ethereal's king/pawn extraction. */
    U64 wp = pos->bitboards[wP];
    U64 bp = pos->bitboards[bP];
    U64 wk = pos->bitboards[wK];
    U64 bk = pos->bitboards[bK];

    if (wk) { int sq = poplsb(&wk); int idx = pkIndex(WHITE, KING, sq);
              for (int i = 0; i < PK_H1; i++) h[i] += pk_w1[idx][i]; }
    if (bk) { int sq = poplsb(&bk); int idx = pkIndex(BLACK, KING, sq);
              for (int i = 0; i < PK_H1; i++) h[i] += pk_w1[idx][i]; }

    while (wp) { int sq = poplsb(&wp); int idx = pkIndex(WHITE, PAWN, sq);
                 for (int i = 0; i < PK_H1; i++) h[i] += pk_w1[idx][i]; }
    while (bp) { int sq = poplsb(&bp); int idx = pkIndex(BLACK, PAWN, sq);
                 for (int i = 0; i < PK_H1; i++) h[i] += pk_w1[idx][i]; }

    float out[PK_OUT];
    for (int o = 0; o < PK_OUT; o++) {
        float acc = pk_b2[o];
        for (int j = 0; j < PK_H1; j++)
            if (h[j] >= 0.0f)
                acc += h[j] * pk_w2[o][j];
        out[o] = acc * pk_scale;
    }

    return MakeScore((int) out[0], (int) out[1]);
}
