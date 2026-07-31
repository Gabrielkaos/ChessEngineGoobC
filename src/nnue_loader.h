/*
 * nnue_loader.h / nnue_loader.c
 * ==============================
 * NNUE inference for GOOB — Stockfish-style architecture:
 *
 *   - King-relative "HalfKA" input features (own king square selects
 *     which 768-wide feature block is active; features never change
 *     meaning when OTHER pieces move, only when the corresponding
 *     king moves), instead of the old single absolute/White-fixed
 *     768-input encoding.
 *   - TWO accumulators per position, one per perspective (the side to
 *     move's own view of the board, and the other side's view),
 *     instead of one. This is what "dual perspective" means: the same
 *     physical position is encoded twice, once from each king's point
 *     of view, and both encodings feed the network every eval.
 *   - A single SHARED feature-transformer weight table is used for
 *     BOTH perspectives (same weights, different feature indices per
 *     perspective) — this is how Stockfish does it too, and it's why
 *     the net generalizes across both colors from one weight set.
 *   - Fully quantized integer forward pass (int16 feature transformer,
 *     int8 L2/L3/output), one float multiply at the very end — same
 *     quantization discipline as before, just applied to the new
 *     architecture.
 *   - Incremental accumulator maintenance for everything except king
 *     moves; a king move forces a full rebuild of THAT king's own
 *     perspective (see "WHY king moves are special" below), but the
 *     OTHER perspective's accumulator is still updated incrementally
 *     (from the other side's point of view, an enemy king moving is
 *     just an ordinary piece moving).
 *
 * ── Feature encoding ───────────────────────────────────────────────
 * For perspective `us` (WHITE or BLACK) looking at a piece `pce` on
 * square `sq`, with `us`'s king on `kingSq`:
 *
 *   relKing  = (us == WHITE) ? kingSq : MIRROR64(kingSq)
 *   relSq    = (us == WHITE) ? sq     : MIRROR64(sq)
 *   ptype    = pieceType(pce) - 1                    // 0..5 (P..K)
 *   relColor = (pieceCol(pce) == us) ? 0 : 1          // own=0, enemy=1
 *   ptc      = relColor * 6 + ptype                   // 0..11
 *   feature  = (relKing * 12 + ptc) * 64 + relSq       // 0..49151
 *
 * MIRROR64 (already used elsewhere in this engine for PSQT/eval
 * mirroring) flips a square top-to-bottom (a1<->a8), which is exactly
 * the standard "view the board as if you were Black" transform. This
 * makes the feature space perspective-agnostic: "my king is on e1,
 * my pawn is on e2" and "my king is on e8, my pawn is on e7" produce
 * IDENTICAL feature indices from their respective owner's point of
 * view, so one set of weights learns both colors.
 *
 * Total input size = 64 king squares * 12 piece-types(inc. king) * 64
 * squares = 49152 per perspective (this is "HalfKA": unlike the older
 * "HalfKP" scheme, kings themselves ARE one of the 12 piece types, so
 * both your own king and the enemy king get feature planes too).
 *
 * A further standard optimization — horizontal mirroring to fold the
 * 64 king squares down to 32 shared buckets ("HalfKAv2_hm") — is not
 * implemented here to keep this a manageable, correct first cut; it's
 * a drop-in follow-up (halve NUM_KING_BUCKETS, mirror files d-h onto
 * a-d in nnue_feature_index) once this net is trained and working.
 *
 * ── WHY king moves are special ─────────────────────────────────────
 * Every one of a perspective's 49152 features is implicitly indexed
 * by "where is MY king", so when that king moves, relKing changes for
 * EVERY currently-active feature in that perspective at once — there
 * is no small incremental delta, unlike an ordinary piece move (which
 * only ever touches 1-2 feature columns). So on a king move:
 *   - the MOVING side's own-perspective accumulator is fully rebuilt
 *     from the current board (O(32) piece additions, not O(1), but
 *     this only happens on king moves, not every move)
 *   - the OTHER side's perspective accumulator is updated the normal
 *     incremental way, because from the other side's point of view
 *     their own king hasn't moved — the enemy king is just another
 *     piece changing squares.
 * This mirrors exactly how Stockfish's NNUE handles king moves
 * (accumulator refresh on own-king move, incremental otherwise).
 *
 * ── Network ────────────────────────────────────────────────────────
 *   FeatureTransformer (shared weights, per-perspective accumulator):
 *     acc[us][j] = bias[j] + sum over active features f of W[f][j]
 *   Both accumulators are clipped-ReLU'd and requantized to int8
 *   [0,127], then CONCATENATED as [stm_acc | other_acc] (side-to-move
 *   first) into a single 2*L1-wide int8 vector. Putting the mover's
 *   own view first is what makes the network's output naturally
 *   side-to-move-relative — no separate sign flip needed at the end,
 *   unlike the old absolute-feature version.
 *   L2: QUANTIZED (int8 weights, int32 dot), clipped ReLU -> int8
 *   L3: QUANTIZED (int8 weights, int32 dot), clipped ReLU -> int8
 *   Output: QUANTIZED (int8 weights, int32 dot) raw logit, converted
 *   to centipawns with ONE float multiply at the very end. Already
 *   side-to-move-relative (see above), matching how EvalPosition()
 *   in evaluate.c uses the result.
 *
 * ── Performance notes ──────────────────────────────────────────────
 * fc/feature-transformer weight table is stored feature-major on disk
 * AND in memory ([feature][l1_size], contiguous per feature) — this
 * is a NEW file format (v5) designed around the update hot path from
 * day one, so unlike the old v4 loader there's no disk->memory
 * transpose step needed at load time.
 *
 * The int8 x int8 -> int32 dot product shared by L2/L3/output has a
 * runtime-dispatched AVX2 kernel (32 lanes/iter via
 * _mm256_maddubs_epi16 + _mm256_madd_epi16), falling back to a
 * portable scalar loop on non-AVX2 CPUs — unchanged from before,
 * still the highest-value SIMD target since these 3 matmuls run on
 * every single nnue_eval() call (the feature transformer only touches
 * a couple of rows per move, so it doesn't need the same treatment).
 *
 * ── Weight file layout v5 (all little-endian) ──────────────────────
 *   char[4]  magic = "NNUE"
 *   int32    version = 5
 *   int32    king_squares      (MUST be 64 — see header comment above
 *                                about the HalfKAv2_hm follow-up)
 *   int32    l1_size           (MUST equal NNUE_ACC_SIZE in board.h)
 *   int32    l2_size
 *   int32    l3_size
 *   float32  cp_scale
 *   int32    qa_scale                (feature-transformer scale)
 *   int32    qb_scale                (L2 scale)
 *   int32    qc_scale                (L3 scale)
 *   int32    qd_scale                (output scale)
 *   int16[king_squares*12*64 * l1_size]   ft.weight  (feature-major,
 *                                                      quantized)
 *   int32[l1_size]                        ft.bias    (quantized)
 *   int8 [l2_size * (2*l1_size)]          l2.weight  (quantized)
 *   int32[l2_size]                        l2.bias    (scale=127*qb_scale)
 *   int8 [l3_size * l2_size]              l3.weight  (quantized)
 *   int32[l3_size]                        l3.bias    (scale=127*qc_scale)
 *   int8 [1 * l3_size]                    out.weight (quantized)
 *   int32[1]                              out.bias   (scale=127*qd_scale)
 *
 * v4 (absolute single-perspective) files are no longer accepted — you
 * need a new training/export pipeline that emits the v5 layout above;
 * this loader only implements the C-side inference/update code.
 */

#ifndef NNUE_LOADER_H
#define NNUE_LOADER_H

#include "defs.h"
#include "board.h"

/* ── Public API (unchanged signatures — callers in makemove.c/board.c/
 * uci.c/evaluate.c need no changes) ───────────────────────────────── */

/* Load weights from binary file. Returns 1 on success, 0 on failure.
 * Does NOT touch any position's accumulators — call
 * nnue_refresh_accumulator() afterward for whatever position is
 * currently set up (uci.c already does this). */
int nnue_init(const char *path);

/* Full rebuild of pos->nnue_acc[WHITE] and pos->nnue_acc[BLACK] from
 * pos->pieces, from scratch. Call whenever a position is set up from
 * raw FEN/mirrored data rather than via incremental makeMove/takeMove
 * (board.c already does this from updateListMaterial), or right after
 * loading new weights. */
void nnue_refresh_accumulator(S_BOARD *pos);

/* Incremental accumulator maintenance — called from makemove.c's
 * ClearPiece/AddPiece/MovePiece. No-ops if !nnue_loaded or piece==EMPTY.
 * Internally touches BOTH perspectives (every piece is visible from
 * both sides' feature sets); nnue_update_move additionally triggers a
 * full own-perspective refresh when `piece` is a king (see header
 * comment "WHY king moves are special"). */
void nnue_update_add(S_BOARD *pos, int piece, int sq);
void nnue_update_remove(S_BOARD *pos, int piece, int sq);
void nnue_update_move(S_BOARD *pos, int piece, int from, int to);

/* Evaluate board position using the CURRENT accumulators (must already
 * be in sync — see above). Returns centipawns from the SIDE-TO-MOVE's
 * POV (matches how EvalPosition() in evaluate.c uses the result).
 * Requires nnue_init() to have been called first. */
int nnue_eval(const S_BOARD *pos);

/* True after successful nnue_init */
extern int nnue_loaded;

#endif /* NNUE_LOADER_H */


/* ═══════════════════════════════════════════════════════════════════════════
 * IMPLEMENTATION  –  define NNUE_IMPLEMENTATION in exactly one .c file
 *                    (nnue_loader.c already does this).
 * ═══════════════════════════════════════════════════════════════════════════ */
#ifdef NNUE_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── SIMD platform detection for the int8 dot product (unchanged from
 * before — see header comment) ────────────────────────────────────── */
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define NNUE_ARCH_X86 1
#endif

#if defined(NNUE_ARCH_X86) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define NNUE_HAVE_AVX2_DISPATCH 1
#endif

/* ── Feature-space constants ───────────────────────────────────────── */
#define NNUE_KING_SQUARES   64
#define NNUE_PIECE_TYPES    6   /* P,N,B,R,Q,K */
#define NNUE_PTC_COUNT      12  /* own/enemy x 6 piece types */
#define NNUE_FEATURES_PER_KING (NNUE_PTC_COUNT * 64)              /* 768 */
#define NNUE_NUM_FEATURES  ((size_t)NNUE_KING_SQUARES * NNUE_FEATURES_PER_KING) /* 49152 */

/* fc2/fc3 fixed buffer caps (same convention as before) */
#define NNUE_L2L3_MAX 64

typedef struct {
    int king_squares;   /* must be NNUE_KING_SQUARES (64) */
    int l1_size;         /* per-perspective accumulator width */
    int l2_in_size;       /* = 2 * l1_size (concatenated perspectives) */
    int l2_size;
    int l3_size;
    float cp_scale;
    int qa_scale;
    int qb_scale;
    int qc_scale;
    int qd_scale;

    int16_t *ft_w; /* [NNUE_NUM_FEATURES][l1_size], feature-major, quantized.
                       Shared by both perspectives — see header comment.
                       Already contiguous-per-feature on disk in v5, no
                       transpose needed (unlike the old v4 format). */
    int32_t *ft_b; /* [l1_size], quantized */
    int8_t  *l2_w; /* [l2_size][2*l1_size], quantized */
    int32_t *l2_b; /* [l2_size], quantized (scale = 127*qb_scale) */
    int8_t  *l3_w; /* [l3_size][l2_size], quantized */
    int32_t *l3_b; /* [l3_size], quantized (scale = 127*qc_scale) */
    int8_t  *out_w; /* [1][l3_size], quantized */
    int32_t  out_b;  /* scalar, quantized (scale = 127*qd_scale) */

    /* Precomputed once at load time: cp_scale / (127 * qd_scale). The
     * output layer's raw int32 accumulator times this single float
     * gives centipawns directly — the only float op in the whole
     * eval, same discipline as before. */
    float out_combined_scale;
} NNUE_WEIGHTS;

/* ── Globals ────────────────────────────────────────────────────────────── */
static NNUE_WEIGHTS *g_weights = NULL;
int nnue_loaded = 0;

/* ── Feature indexing ──────────────────────────────────────────────────── */

/* See the big header comment for the derivation. `us` is the
 * perspective (WHITE or BLACK), `kingSq` is THAT perspective's own
 * king square (caller-supplied — see the king-move special case in
 * nnue_update_move for why this can't always just be read straight
 * off pos->bitboards). */
static inline size_t nnue_feature_index(int us, int kingSq, int pce, int sq) {
    int relKing  = (us == WHITE) ? kingSq : MIRROR64(kingSq);
    int relSq    = (us == WHITE) ? sq     : MIRROR64(sq);
    int ptype    = pieceType[pce] - 1;                    /* 0..5 */
    int relColor = (pieceCol[pce] == us) ? 0 : 1;          /* own=0 enemy=1 */
    int ptc      = relColor * NNUE_PIECE_TYPES + ptype;    /* 0..11 */
    return ((size_t)relKing * NNUE_PTC_COUNT + ptc) * 64 + relSq;
}

/* ── Helpers ────────────────────────────────────────────────────────────── */

/* Undo a layer's (127*q_scale) combined fixed-point scale and clip to
 * [0,127] in one step (same convention as before, unchanged). */
static inline int8_t requantize_clipped_i8(int32_t acc, int q_scale) {
    float v = (float)acc / (float)q_scale;
    if (v < 0.0f) v = 0.0f;
    if (v > 127.0f) v = 127.0f;
    return (int8_t)(v + 0.5f);
}

static void *read_bytes(FILE *f, size_t nbytes, int *ok) {
    void *buf = malloc(nbytes);
    if (!buf) { *ok = 0; return NULL; }
    if (fread(buf, 1, nbytes, f) != nbytes) {
        *ok = 0;
        free(buf);
        return NULL;
    }
    return buf;
}

static void nnue_free_weights(void) {
    if (!g_weights) return;
    free(g_weights->ft_w); free(g_weights->ft_b);
    free(g_weights->l2_w); free(g_weights->l2_b);
    free(g_weights->l3_w); free(g_weights->l3_b);
    free(g_weights->out_w);
    free(g_weights);
    g_weights = NULL;
}

/* ── Init ───────────────────────────────────────────────────────────────── */
int nnue_init(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[NNUE] Cannot open weight file: %s\n", path);
        return 0;
    }

    char magic[4];
    int32_t version, king_squares, l1, l2, l3, qa_scale, qb_scale, qc_scale, qd_scale;
    float cp_scale;

    int ok = 1;
    ok &= fread(magic, 1, 4, f) == 4 && memcmp(magic, "NNUE", 4) == 0;
    ok &= fread(&version, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&king_squares, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&l1, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&l2, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&l3, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&cp_scale, sizeof(float), 1, f) == 1;
    ok &= fread(&qa_scale, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&qb_scale, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&qc_scale, sizeof(int32_t), 1, f) == 1;
    ok &= fread(&qd_scale, sizeof(int32_t), 1, f) == 1;

    if (!ok || version != 5) {
        fprintf(stderr, "[NNUE] Bad header or unsupported version in: %s "
                         "(expected version 5 — a dual-perspective HalfKA "
                         "net; v4 absolute single-perspective files are no "
                         "longer accepted)\n", path);
        fclose(f);
        return 0;
    }

    if (king_squares != NNUE_KING_SQUARES) {
        fprintf(stderr, "[NNUE] king_squares (%d) != %d — this loader only "
                         "supports the full 64-king-square HalfKA layout "
                         "(no horizontal-mirror king buckets yet).\n",
                king_squares, NNUE_KING_SQUARES);
        fclose(f);
        return 0;
    }

    if (l1 != NNUE_ACC_SIZE) {
        fprintf(stderr, "[NNUE] l1_size (%d) does not match NNUE_ACC_SIZE (%d) "
                         "in board.h — update board.h and rebuild.\n",
                l1, NNUE_ACC_SIZE);
        fclose(f);
        return 0;
    }

    if (l2 > NNUE_L2L3_MAX || l3 > NNUE_L2L3_MAX) {
        fprintf(stderr, "[NNUE] l2_size/l3_size (%d/%d) exceed the fixed "
                         "eval buffer cap (%d) — raise NNUE_L2L3_MAX.\n",
                l2, l3, NNUE_L2L3_MAX);
        fclose(f);
        return 0;
    }

    nnue_free_weights(); /* drop any previously loaded net */
    g_weights = (NNUE_WEIGHTS *)calloc(1, sizeof(NNUE_WEIGHTS));
    if (!g_weights) { fclose(f); return 0; }

    g_weights->king_squares = king_squares;
    g_weights->l1_size = l1;
    g_weights->l2_in_size = 2 * l1;
    g_weights->l2_size = l2;
    g_weights->l3_size = l3;
    g_weights->cp_scale = cp_scale;
    g_weights->qa_scale = qa_scale;
    g_weights->qb_scale = qb_scale;
    g_weights->qc_scale = qc_scale;
    g_weights->qd_scale = qd_scale;

    g_weights->ft_w = (int16_t *)read_bytes(f, sizeof(int16_t) * NNUE_NUM_FEATURES * (size_t)l1, &ok);
    g_weights->ft_b = (int32_t *)read_bytes(f, sizeof(int32_t) * (size_t)l1, &ok);
    g_weights->l2_w = (int8_t *)read_bytes(f, sizeof(int8_t) * (size_t)l2 * (size_t)(2 * l1), &ok);
    g_weights->l2_b = (int32_t *)read_bytes(f, sizeof(int32_t) * (size_t)l2, &ok);
    g_weights->l3_w = (int8_t *)read_bytes(f, sizeof(int8_t) * (size_t)l3 * l2, &ok);
    g_weights->l3_b = (int32_t *)read_bytes(f, sizeof(int32_t) * (size_t)l3, &ok);
    g_weights->out_w = (int8_t *)read_bytes(f, sizeof(int8_t) * (size_t)l3, &ok);
    ok &= fread(&g_weights->out_b, sizeof(int32_t), 1, f) == 1;

    fclose(f);

    if (!ok) {
        fprintf(stderr, "[NNUE] Weight file truncated or corrupt: %s\n", path);
        nnue_free_weights();
        nnue_loaded = 0;
        return 0;
    }

    g_weights->out_combined_scale = cp_scale / (127.0f * (float)qd_scale);

    nnue_loaded = 1;
    printf("[NNUE] v5 (dual-perspective HalfKA) weights loaded from %s "
           "(features=%zu l1=%d l2=%d l3=%d scale=%.1f qa=%d qb=%d qc=%d qd=%d)\n",
           path, NNUE_NUM_FEATURES, l1, l2, l3, cp_scale, qa_scale, qb_scale, qc_scale, qd_scale);
    return 1;
}

/* ── Incremental accumulator maintenance ───────────────────────────────── */

/* Rebuilds pos->nnue_acc[us] from scratch using pos->pieces[], with an
 * EXPLICITLY supplied king square for `us` rather than reading it off
 * pos->bitboards. This matters in exactly one caller: the king-move
 * branch of nnue_update_move, where pos->pieces[] already reflects the
 * king's NEW square but pos->bitboards[wK/bK] has not been updated yet
 * (MovePiece updates the board array before the bitboard — see
 * makemove.c). Everywhere else (nnue_refresh_accumulator, called only
 * when the whole board — bitboards included — is already fully and
 * consistently set up) it's safe to pass the bitboard-derived king
 * square, and nnue_refresh_accumulator does exactly that. */
static void nnue_refresh_perspective_ks(S_BOARD *pos, int us, int kingSq) {
    int l1 = g_weights->l1_size;
    int32_t *acc = pos->nnue_acc[us];
    for (int i = 0; i < l1; i++) acc[i] = g_weights->ft_b[i];
    for (int sq = 0; sq < 64; sq++) {
        int p = pos->pieces[sq];
        if (p == EMPTY) continue;
        size_t idx = nnue_feature_index(us, kingSq, p, sq);
        const int16_t *row = g_weights->ft_w + idx * (size_t)l1;
        for (int i = 0; i < l1; i++) acc[i] += (int32_t)row[i];
    }
}

static inline int nnue_king_sq(const S_BOARD *pos, int us) {
    return LSBINDEX(pos->bitboards[us == WHITE ? wK : bK]);
}

void nnue_refresh_accumulator(S_BOARD *pos) {
    if (!nnue_loaded) return;
    nnue_refresh_perspective_ks(pos, WHITE, nnue_king_sq(pos, WHITE));
    nnue_refresh_perspective_ks(pos, BLACK, nnue_king_sq(pos, BLACK));
}

/* Single-perspective incremental add/remove/move, shared by the public
 * nnue_update_* functions below (each of which applies these to BOTH
 * perspectives — every piece is visible from both sides' feature
 * sets). `viewer`'s own king square is read from pos->bitboards, which
 * is always valid here: kings themselves are never passed to these
 * per-perspective helpers (add/remove never touch a king — see
 * nnue_update_add/remove's own-king note below — and the "other
 * perspective" call from nnue_update_move is for a piece that is NOT
 * `viewer`'s own king by construction). */
static inline void nnue_add_one(S_BOARD *pos, int viewer, int pce, int sq) {
    int l1 = g_weights->l1_size;
    int kingSq = nnue_king_sq(pos, viewer);
    size_t idx = nnue_feature_index(viewer, kingSq, pce, sq);
    const int16_t *row = g_weights->ft_w + idx * (size_t)l1;
    int32_t *acc = pos->nnue_acc[viewer];
    for (int i = 0; i < l1; i++) acc[i] += (int32_t)row[i];
}

static inline void nnue_remove_one(S_BOARD *pos, int viewer, int pce, int sq) {
    int l1 = g_weights->l1_size;
    int kingSq = nnue_king_sq(pos, viewer);
    size_t idx = nnue_feature_index(viewer, kingSq, pce, sq);
    const int16_t *row = g_weights->ft_w + idx * (size_t)l1;
    int32_t *acc = pos->nnue_acc[viewer];
    for (int i = 0; i < l1; i++) acc[i] -= (int32_t)row[i];
}

static inline void nnue_move_one(S_BOARD *pos, int viewer, int pce, int from, int to) {
    int l1 = g_weights->l1_size;
    int kingSq = nnue_king_sq(pos, viewer);
    size_t idx_from = nnue_feature_index(viewer, kingSq, pce, from);
    size_t idx_to   = nnue_feature_index(viewer, kingSq, pce, to);
    const int16_t *row_from = g_weights->ft_w + idx_from * (size_t)l1;
    const int16_t *row_to   = g_weights->ft_w + idx_to   * (size_t)l1;
    int32_t *acc = pos->nnue_acc[viewer];
    for (int i = 0; i < l1; i++) acc[i] += (int32_t)row_to[i] - (int32_t)row_from[i];
}

void nnue_update_add(S_BOARD *pos, int piece, int sq) {
    if (!nnue_loaded || piece == EMPTY) return;
    /* Kings are only ever placed via nnue_refresh_accumulator (board
     * setup) in this engine — see makemove.c, kings always MOVE, never
     * get individually cleared+re-added. Both perspectives' king
     * squares are therefore always safely readable from bitboards
     * here. */
    nnue_add_one(pos, WHITE, piece, sq);
    nnue_add_one(pos, BLACK, piece, sq);
}

void nnue_update_remove(S_BOARD *pos, int piece, int sq) {
    if (!nnue_loaded || piece == EMPTY) return;
    /* Kings are never captured/cleared in legal chess, so `piece` here
     * is never a king — same reasoning as nnue_update_add. */
    nnue_remove_one(pos, WHITE, piece, sq);
    nnue_remove_one(pos, BLACK, piece, sq);
}

void nnue_update_move(S_BOARD *pos, int piece, int from, int to) {
    if (!nnue_loaded || piece == EMPTY) return;
    int us = pieceCol[piece];
    int them = us ^ 1;

    if (pieceType[piece] == KING) {
        /* Own perspective: every feature depends on this king's square,
         * so no incremental delta exists — full rebuild. pos->pieces[]
         * already reflects the king at `to` (MovePiece updates it
         * before calling here), so pass `to` explicitly instead of
         * trusting pos->bitboards (still stale at this point — see
         * nnue_refresh_perspective_ks's comment). */
        nnue_refresh_perspective_ks(pos, us, to);
        /* Other perspective: from their point of view their own king
         * hasn't moved, so the enemy king moving is just an ordinary
         * piece changing squares — incremental update is valid. */
        nnue_move_one(pos, them, piece, from, to);
    } else {
        nnue_move_one(pos, us, piece, from, to);
        nnue_move_one(pos, them, piece, from, to);
    }
}

/* ── int8 dot product kernels, shared by L2/L3/output (unchanged from
 * before — see header comment) ────────────────────────────────────── */

static int32_t nnue_dot_i8_scalar(const int8_t *a, const int8_t *b, int n) {
    int32_t acc = 0;
    for (int j = 0; j < n; j++) {
        acc += (int32_t)(unsigned char)a[j] * (int32_t)b[j];
    }
    return acc;
}

#ifdef NNUE_HAVE_AVX2_DISPATCH
__attribute__((target("avx2")))
static int32_t nnue_dot_i8_avx2(const int8_t *a, const int8_t *b, int n) {
    __m256i acc = _mm256_setzero_si256();
    const __m256i ones = _mm256_set1_epi16(1);
    int i = 0;
    for (; i + 32 <= n; i += 32) {
        __m256i va = _mm256_loadu_si256((const __m256i *)(a + i));
        __m256i vb = _mm256_loadu_si256((const __m256i *)(b + i));
        __m256i prod16 = _mm256_maddubs_epi16(va, vb);
        __m256i prod32 = _mm256_madd_epi16(prod16, ones);
        acc = _mm256_add_epi32(acc, prod32);
    }
    __m128i lo = _mm256_castsi256_si128(acc);
    __m128i hi = _mm256_extracti128_si256(acc, 1);
    __m128i sum128 = _mm_add_epi32(lo, hi);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    int32_t sum = _mm_cvtsi128_si32(sum128);
    sum += nnue_dot_i8_scalar(a + i, b + i, n - i);
    return sum;
}

static int cpu_supports_avx2(void) {
    static int cached = -1;
    if (cached < 0) {
        __builtin_cpu_init();
        cached = __builtin_cpu_supports("avx2") ? 1 : 0;
    }
    return cached;
}
#endif /* NNUE_HAVE_AVX2_DISPATCH */

static inline int32_t nnue_dot_i8(const int8_t *a, const int8_t *b, int n) {
#ifdef NNUE_HAVE_AVX2_DISPATCH
    if (cpu_supports_avx2()) return nnue_dot_i8_avx2(a, b, n);
#endif
    return nnue_dot_i8_scalar(a, b, n);
}

/* ── Quantized layer forward passes ────────────────────────────────────── */

static void int8_layer_forward(const int8_t *in_i8, int in_size,
                                const int8_t *w, const int32_t *b,
                                int q_scale, int out_size,
                                int8_t *out_i8) {
    for (int i = 0; i < out_size; i++) {
        const int8_t *row = w + (size_t)i * in_size;
        int32_t acc = b[i] + nnue_dot_i8(in_i8, row, in_size);
        out_i8[i] = requantize_clipped_i8(acc, q_scale);
    }
}

static int32_t output_forward(const int8_t *in_i8, int in_size,
                               const int8_t *w, int32_t b) {
    return b + nnue_dot_i8(in_i8, w, in_size);
}

/* ── Eval ───────────────────────────────────────────────────────────────── */
int nnue_eval(const S_BOARD *pos) {
    if (!nnue_loaded) return 0;

    int stm = pos->side;
    int other = stm ^ 1;
    int l1 = g_weights->l1_size;

    /* Concatenate [stm's own view | other side's view], each clipped
     * ReLU'd and requantized to int8 [0,127]. Side-to-move first is
     * what makes the network's raw output already side-to-move-
     * relative — no sign flip needed afterward (contrast with the old
     * absolute-feature version, which had to flip for Black). */
    int8_t in_i8[2 * NNUE_ACC_SIZE];
    for (int i = 0; i < l1; i++) {
        in_i8[i]      = requantize_clipped_i8(pos->nnue_acc[stm][i],   g_weights->qa_scale);
        in_i8[l1 + i] = requantize_clipped_i8(pos->nnue_acc[other][i], g_weights->qa_scale);
    }

    int8_t h2_i8[NNUE_L2L3_MAX], h3_i8[NNUE_L2L3_MAX];
    int8_layer_forward(in_i8, 2 * l1, g_weights->l2_w, g_weights->l2_b,
                        g_weights->qb_scale, g_weights->l2_size, h2_i8);
    int8_layer_forward(h2_i8, g_weights->l2_size, g_weights->l3_w, g_weights->l3_b,
                        g_weights->qc_scale, g_weights->l3_size, h3_i8);
    int32_t acc_out = output_forward(h3_i8, g_weights->l3_size, g_weights->out_w, g_weights->out_b);

    /* acc_out is the output layer's int32 accumulator, in units of
     * (127*qd_scale) — the ONLY float conversion in the whole forward
     * pass happens here, once. Already side-to-move-relative (see
     * above), matching how EvalPosition() uses the result. */
    return (int)((float)acc_out * g_weights->out_combined_scale);
}

#endif /* NNUE_IMPLEMENTATION */