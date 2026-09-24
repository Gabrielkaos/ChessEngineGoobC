"""
Sanity checks for a quantised.bin produced by export_weights.py.

    python check_net.py --net ../checkpoints/quantised.bin
    python check_net.py --net ../checkpoints/quantised.bin --checkpoint ../checkpoints/nnue.pt
    python check_net.py --net ../checkpoints/quantised.bin --pipeline

What it checks
  1. File size / layout.
  2. Integer-overflow limits of the engine's inference:
       - output weights must satisfy |w| <= 128 for the fast AVX2/AVX-512
         SCReLU kernel (it multiplies w * v in int16, v <= 255);
       - a conservative bound on the int16 feature-transformer accumulator.
  3. Evaluates test positions with exactly the integer arithmetic of
     nnue_loader.h. Compare the printed numbers with your engine's nnue_eval()
     on the same FENs: they should match to the centipawn. If they don't, the
     engine's square numbering or piece-type order differs from what this
     script assumes (a1 = 0 ... h8 = 63; P, N, B, R, Q, K = 0..5; white = 0).
  4. --checkpoint: float model vs quantized eval (quantization error).
  5. --pipeline: pushes the same FENs through fen_utils.pack_record and
     nnue_dataset, and checks that the training features, buckets and
     targets agree with the engine's conventions.

Only numpy is required for 1-3; --checkpoint and --pipeline also need torch.
"""

import argparse
import math
import os
import sys
import tempfile

import numpy as np

INPUTS, HIDDEN, BUCKETS = 768, 1024, 8
QA, QB, SCALE = 255, 64, 400
FAST_OUT_W_LIMIT = 128
N_SHORTS = INPUTS * HIDDEN + HIDDEN + BUCKETS * 2 * HIDDEN + BUCKETS
PADDING = 48
PIECE_CHARS = "PNBRQK"

DEFAULT_FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -",
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq -",
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq -",
    "2rq1rk1/pp1bnp1p/3p4/3Ppp2/8/6P1/PPNQPPBP/R4RK1 w - -",
    "7r/1p3k2/p1bPR3/5p2/2B2P1p/8/PP4P1/3K4 b - -",
    "8/4r3/2R2pk1/6pp/3P4/6P1/5K1P/8 b - -",
    "8/8/8/p4k1p/P6P/5PK1/8/8 w - -",
    "6k1/6p1/8/4K3/4NN2/8/8/8 w - -",
    "4k3/8/8/8/8/8/8/4K2Q w - -",
]


# ── Board helpers (engine conventions) ─────────────────────────────────────

def parse_fen(fen):
    """Returns (pieces, stm) with pieces = [(color, ptype, sq)], a1 = 0."""
    parts = fen.split()
    board, stm = parts[0], parts[1]
    pieces = []
    rank, file = 7, 0
    for ch in board:
        if ch == "/":
            rank -= 1
            file = 0
        elif ch.isdigit():
            file += int(ch)
        else:
            color = 0 if ch.isupper() else 1
            pieces.append((color, PIECE_CHARS.index(ch.upper()), rank * 8 + file))
            file += 1
    return pieces, (0 if stm == "w" else 1)


def mirror_fen(fen):
    """Swap colors and flip the board vertically. For this architecture the
    side-to-move eval of a position and of its mirror must be identical."""
    parts = fen.split()
    ranks = parts[0].split("/")[::-1]
    board = "/".join("".join(c.swapcase() for c in r) for r in ranks)
    stm = "b" if parts[1] == "w" else "w"
    castle = parts[2] if len(parts) > 2 else "-"
    castle = "".join(sorted(castle.swapcase(), key="KQkq".find)) if castle != "-" else "-"
    ep = parts[3] if len(parts) > 3 else "-"
    if ep != "-":
        ep = ep[0] + ("6" if ep[1] == "3" else "3")
    return f"{board} {stm} {castle} {ep}"


def feature_indices(pieces, perspective):
    if perspective == 0:
        return [c * 384 + pt * 64 + sq for c, pt, sq in pieces]
    return [(c ^ 1) * 384 + pt * 64 + (sq ^ 56) for c, pt, sq in pieces]


def output_bucket(pieces):
    return min(max((len(pieces) - 2) // 4, 0), 7)


# ── Net loading ────────────────────────────────────────────────────────────

class QuantNet:
    def __init__(self, path):
        raw = np.fromfile(path, dtype="<i2")
        size = os.path.getsize(path)
        self.size_ok = size in (2 * N_SHORTS, 2 * N_SHORTS + PADDING)
        self.size = size
        if raw.size < N_SHORTS:
            raise SystemExit(f"{path}: {size} bytes is too small (need {2 * N_SHORTS}).")
        o = 0
        self.ft_w = raw[o:o + INPUTS * HIDDEN].reshape(INPUTS, HIDDEN).astype(np.int64); o += INPUTS * HIDDEN
        self.ft_b = raw[o:o + HIDDEN].astype(np.int64); o += HIDDEN
        self.out_w = raw[o:o + BUCKETS * 2 * HIDDEN].reshape(BUCKETS, 2 * HIDDEN).astype(np.int64); o += BUCKETS * 2 * HIDDEN
        self.out_b = raw[o:o + BUCKETS].astype(np.int64)


def wrap_i16(x):
    return ((x + 32768) % 65536) - 32768


def c_div(a, b):
    """C integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def accumulator(net, feats):
    return wrap_i16(net.ft_b + net.ft_w[feats].sum(axis=0))


def quantized_eval(net, fen, emulate_fast_kernel_overflow=False):
    """Side-to-move centipawns, bit-exact with nnue_loader.h.
    emulate_fast_kernel_overflow=True reproduces the original loader's AVX2
    kernel, which silently wraps w * v to int16."""
    pieces, stm = parse_fen(fen)
    acc = [accumulator(net, feature_indices(pieces, p)) for p in (0, 1)]
    bucket = output_bucket(pieces)
    x = np.concatenate([acc[stm], acc[stm ^ 1]])
    c = np.clip(x, 0, QA)
    w = net.out_w[bucket]
    if emulate_fast_kernel_overflow:
        total = int((wrap_i16(w * c) * c).sum())
    else:
        total = int((c * c * w).sum())
    overflow = not (-(2 ** 31) <= total < 2 ** 31)
    ev = c_div(total, QA) + int(net.out_b[bucket])
    ev = c_div(ev * SCALE, QA * QB)
    return ev, bucket, overflow


def accumulator_bound(ft_w, ft_b):
    """Conservative worst-case accumulator range over legal-ish material
    (per side: exactly one king, at most 15 other pieces, no pawns on the
    first/last rank). ft_w: (768, 1024), ft_b: (1024,) quantized.
    Returns (max_upper, min_lower) in int16 units."""
    ft_w = np.asarray(ft_w, dtype=np.int64)
    upper = np.asarray(ft_b, dtype=np.float64).copy()
    lower = upper.copy()
    for block in (0, 1):
        base = block * 384
        king = ft_w[base + 5 * 64: base + 6 * 64]
        others = []
        for pt in range(5):
            for sq in range(64):
                if pt == 0 and (sq < 8 or sq >= 56):
                    continue
                others.append(base + pt * 64 + sq)
        o = np.sort(ft_w[others], axis=0)          # (n, HIDDEN), ascending
        top = np.clip(o[-15:], 0, None).sum(axis=0)
        bot = np.clip(o[:15], None, 0).sum(axis=0)
        upper += king.max(axis=0) + top
        lower += king.min(axis=0) + bot
    return upper.max(), lower.min()


# ── Float model (for quantization error) ───────────────────────────────────

def load_float_params(path):
    import torch
    try:
        ckpt = torch.load(path, map_location="cpu", weights_only=True)
    except TypeError:   # torch < 1.13
        ckpt = torch.load(path, map_location="cpu")
    sd = ckpt.get("model_state_dict", ckpt)
    return {k: sd[k].float().numpy() for k in ("ft.weight", "ft_bias", "output_weights", "output_biases")}


def float_eval(params, fen):
    pieces, stm = parse_fen(fen)
    acc = [params["ft_bias"] + params["ft.weight"][feature_indices(pieces, p)].sum(axis=0) for p in (0, 1)]
    act = [np.clip(a, 0.0, 1.0) ** 2 for a in acc]
    b = output_bucket(pieces)
    w = params["output_weights"][b]
    out = act[stm] @ w[:HIDDEN] + act[stm ^ 1] @ w[HIDDEN:] + params["output_biases"][b]
    return float(out) * SCALE


# ── Training-pipeline consistency ──────────────────────────────────────────

def check_pipeline(fens, cp_white=100):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        from fen_utils import pack_record
        from nnue_dataset import NNUEDataset, nnue_collate
    except ImportError as e:
        print(f"  skipped: cannot import training modules ({e})")
        return
    tmpdir = tempfile.mkdtemp()
    path = os.path.join(tmpdir, "check.bin")
    with open(path, "wb") as f:
        for fen in fens:
            f.write(pack_record(fen, cp_white, None))
    ds = NNUEDataset(path)
    feats, y = nnue_collate([ds[i] for i in range(len(ds))])
    white_idx, black_idx, offsets, stm, buckets = (t.cpu().numpy() for t in feats)
    y = y.cpu().numpy().reshape(-1)
    ends = list(offsets[1:]) + [len(white_idx)]

    problems = 0
    print(f"  Each FEN was packed with cp = {cp_white:+d} (white's point of view, as in the dataset).")
    print(f"  {'#':>2}  stm  feats  bucket  target  implied cp (stm POV)")
    for i, fen in enumerate(fens):
        pieces, ref_stm = parse_fen(fen)
        s, e = int(offsets[i]), int(ends[i])
        ok_w = sorted(white_idx[s:e].tolist()) == sorted(feature_indices(pieces, 0))
        ok_b = sorted(black_idx[s:e].tolist()) == sorted(feature_indices(pieces, 1))
        ok_stm = int(stm[i]) == ref_stm
        ok_bucket = int(buckets[i]) == output_bucket(pieces)
        yi = float(np.clip(y[i], 1e-9, 1 - 1e-9))
        implied = SCALE * math.log(yi / (1 - yi))
        expected = cp_white if ref_stm == 0 else -cp_white
        ok_y = abs(implied - expected) < 2.0
        flags = "".join([
            "" if ok_w and ok_b else " FEATURES",
            "" if ok_stm else " STM",
            "" if ok_bucket else " BUCKET",
            "" if ok_y else " TARGET",
        ])
        problems += bool(flags)
        print(f"  {i:>2}  {'wb'[ref_stm]}    {'ok' if ok_w and ok_b else 'BAD':>5}  {int(buckets[i]):>6}  "
              f"{yi:.4f}  {implied:+8.1f} (expected {expected:+d}){'  <--' + flags if flags else ''}")

    if problems == 0:
        print("  Training pipeline matches the engine's conventions.")
        return
    print(f"  {problems} position(s) disagree. Hints:")
    print("   - FEATURES: square numbering, piece order or the black-perspective flip differ from nnue_loader.h.")
    print("   - TARGET with implied cp = +100 on black-to-move rows: targets are in white's point of view,")
    print("     but the model/engine are side-to-move. Negate cp (and mate) when black is to move.")
    print("   - TARGET with a different magnitude: the sigmoid scale is not 400 (model.CP_SCALE), or the")
    print("     target blends in game results. Make it match NNUE_SCALE in the engine.")


# ── Main ───────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--net", required=True, help="quantised.bin")
    ap.add_argument("--checkpoint", help="PyTorch checkpoint the net was exported from (optional)")
    ap.add_argument("--pipeline", action="store_true", help="check fen_utils/nnue_dataset against the engine")
    ap.add_argument("--fen", action="append", help="position to evaluate (repeatable); default: built-in set")
    args = ap.parse_args()

    net = QuantNet(args.net)
    fens = args.fen or DEFAULT_FENS

    print("== File")
    print(f"  {net.size:,} bytes: {'ok' if net.size_ok else 'UNEXPECTED SIZE (different architecture?)'}")

    print("== Integer limits")
    max_out = int(np.abs(net.out_w).max())
    fast_ok = max_out <= FAST_OUT_W_LIMIT
    print(f"  max |output weight| = {max_out} (fast SIMD kernel needs <= {FAST_OUT_W_LIMIT}): "
          f"{'ok' if fast_ok else 'EXCEEDED'}")
    if not fast_ok:
        n_bad = int((np.abs(net.out_w) > FAST_OUT_W_LIMIT).sum())
        print(f"    {n_bad} weights exceed it. The original loader's AVX2 path gives wrong evals with this net;")
        print("    the patched loader detects it and uses an exact (slower) kernel. Retrain/fine-tune with --clip.")
    hi, lo = accumulator_bound(net.ft_w, net.ft_b)
    acc_ok = hi <= 32767 and lo >= -32768
    print(f"  accumulator worst case [{lo:,.0f}, {hi:,.0f}] (int16 is [-32768, 32767]): "
          f"{'ok' if acc_ok else 'MAY OVERFLOW in extreme positions'}")
    print(f"  output biases span [{int(net.out_b.min())}, {int(net.out_b.max())}] "
          f"(= [{net.out_b.min() / (QA * QB) * SCALE:+.0f}, {net.out_b.max() / (QA * QB) * SCALE:+.0f}] cp)")

    params = load_float_params(args.checkpoint) if args.checkpoint else None

    print("== Evaluations (side-to-move centipawns, bit-exact with nnue_loader.h)")
    header = f"  {'bucket':>6}  {'engine':>7}"
    if params is not None:
        header += f"  {'float':>8}  {'diff':>5}"
    if not fast_ok:
        header += f"  {'old AVX2':>8}"
    print(header + "  fen")
    asym = 0
    for fen in fens:
        ev, bucket, ovf = quantized_eval(net, fen)
        ev_m, _, _ = quantized_eval(net, mirror_fen(fen))
        asym += ev != ev_m
        line = f"  {bucket:>6}  {ev:>7}"
        if params is not None:
            fe = float_eval(params, fen)
            line += f"  {fe:>8.1f}  {ev - fe:>+5.1f}"
        if not fast_ok:
            old, _, _ = quantized_eval(net, fen, emulate_fast_kernel_overflow=True)
            line += f"  {old:>8}"
        if ovf:
            line += "  [int32 sum overflow]"
        print(line + f"  {fen}")
    print(f"  color-mirror symmetry: {'ok' if asym == 0 else f'{asym} mismatches (reference bug?)'}")
    print("  Your engine should print these exact numbers for the same FENs, and the same number")
    print("  for each FEN's color-flipped mirror. A mismatch means its feature indexing differs.")

    if args.pipeline:
        print("== Training pipeline")
        check_pipeline(fens)


if __name__ == "__main__":
    main()