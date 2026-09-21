"""
Export a trained NNUE checkpoint to a binary weight file the C engine
can load directly with fread().

Format v5 (dual-perspective HalfKA -- see nnue_loader.h on the C side
for the full architecture writeup). Unlike v4 (single absolute
perspective), this format stores ONE shared feature-transformer weight
table used for BOTH perspectives, plus a doubled-width fc2 since its
input is the concatenation of both perspectives' accumulators:

    char[4]   magic         = b"NNUE"
    int32     version       = 5
    int32     king_squares  = 64
    int32     l1_size
    int32     l2_size
    int32     l3_size
    float32   cp_scale              (see nnue_dataset.py, default 410.0)
    int32     qa_scale              (feature-transformer fixed-point scale)
    int32     qb_scale              (fc2 fixed-point scale)
    int32     qc_scale              (fc3 fixed-point scale)
    int32     qd_scale              (output fixed-point scale)
    int16[king_squares*12*64 * l1_size]   ft.weight, quantized: round(w * qa_scale)
    int32[l1_size]                        ft.bias,   quantized: round(b * qa_scale)
    int8 [l2_size * (2*l1_size)]          fc2.weight, quantized: round(w * qb_scale)
    int32[l2_size]                        fc2.bias,   quantized: round(b * 127 * qb_scale)
    int8 [l3_size * l2_size]              fc3.weight, quantized: round(w * qc_scale)
    int32[l3_size]                        fc3.bias,   quantized: round(b * 127 * qc_scale)
    int8 [1 * l3_size]                    fc4.weight, quantized: round(w * qd_scale)
    int32[1]                              fc4.bias,   quantized: round(b * 127 * qd_scale)

model.ft.weight is already [num_features, l1_size] -- exactly the
feature-major layout the C loader wants (one row per feature,
contiguous across l1_size) -- so unlike the old v4 exporter there's no
transpose step here.

fc2/fc3/fc4 quantization convention is unchanged from v4: each layer's
dequantized, clipped [0,1] activation is requantized to int8 [0,127]
before the next layer's dot product, so every matmul after the feature
transformer runs as plain int8 x int8 -> int32. fc4 has no activation
after it (raw logit, not a [0,1] activation), so its int32 accumulator
is converted to a centipawn value with a single multiply on the C
side -- the only float op in the whole forward pass that isn't done
once per neuron, it's done once per eval call.

v4 files (single absolute perspective) are no longer accepted by the
loader -- this is a from-scratch retrain, not a re-export of an old
checkpoint.

Usage:
    python export_weights.py --checkpoint ../checkpoints/nnue.pt \
        --out ../checkpoints/nnue.bin
"""

import argparse
import struct

import numpy as np
import torch

from model import NNUE, NUM_KING_SQUARES
from nnue_dataset import CP_SCALE


def quantize(w, scale, bits, name):
    """Round-and-clip w*scale to a signed integer of the given bit width."""
    w_q = np.round(w.astype(np.float64) * scale)
    limit = 2 ** (bits - 1) - 1
    max_w = np.max(np.abs(w_q))
    if max_w > limit:
        raise ValueError(
            f"{name} overflow at scale={scale}: max |w*scale|={max_w:.0f} exceeds "
            f"int{bits} range (+/-{limit}). Lower the scale, or retrain with a bit "
            f"of weight decay to keep weight magnitudes down."
        )
    return w_q


def quantize_int8_layer(w, b, scale, name):
    """Quantize a Linear layer's weight to int8 and bias to int32, using
    the (weight*scale, bias*127*scale) convention shared by fc2/fc3/fc4:
    the int8 x int8 dot product lands in units of 127*scale (since the
    incoming activation is itself an int8 in [0,127], i.e. a [0,1] value
    scaled by 127), so the bias must be pre-scaled by that same 127*scale
    to be addable to that dot product directly."""
    w_q = quantize(w, scale, 8, f"{name}.weight")
    b_q = np.round(b.astype(np.float64) * 127 * scale)
    return w_q.astype("<i1"), b_q.astype("<i4")


def write_nnue_v5(f, weights, king_squares, l1_size, l2_size, l3_size,
                   cp_scale, qa_scale, qb_scale, qc_scale, qd_scale):
    """Writes a complete v5 weight file to the open binary file handle f.

    `weights` is a dict of plain numpy arrays:
        ft.weight  [king_squares*12*64, l1_size]   ft.bias  [l1_size]
        fc2.weight [l2_size, 2*l1_size]             fc2.bias [l2_size]
        fc3.weight [l3_size, l2_size]                fc3.bias [l3_size]
        fc4.weight [1, l3_size]                      fc4.bias [1]
    """
    f.write(b"NNUE")
    f.write(struct.pack("<i", 5))
    f.write(struct.pack("<i", king_squares))
    f.write(struct.pack("<i", l1_size))
    f.write(struct.pack("<i", l2_size))
    f.write(struct.pack("<i", l3_size))
    f.write(struct.pack("<f", cp_scale))
    f.write(struct.pack("<i", qa_scale))
    f.write(struct.pack("<i", qb_scale))
    f.write(struct.pack("<i", qc_scale))
    f.write(struct.pack("<i", qd_scale))

    # Feature transformer: quantized int16 weight / int32 bias, already
    # feature-major -- no transpose needed (see module docstring).
    ft_w_q = quantize(weights["ft.weight"], qa_scale, 16, "ft.weight")
    ft_b_q = np.round(weights["ft.bias"].astype(np.float64) * qa_scale)
    f.write(ft_w_q.astype("<i2").tobytes())
    f.write(ft_b_q.astype("<i4").tobytes())

    # fc2/fc3/fc4: all quantized int8 weight / int32 bias, same convention as v4.
    for name, scale in (("fc2", qb_scale), ("fc3", qc_scale), ("fc4", qd_scale)):
        w_q, b_q = quantize_int8_layer(
            weights[f"{name}.weight"], weights[f"{name}.bias"], scale, name)
        f.write(w_q.tobytes())
        f.write(b_q.tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--checkpoint", default="../checkpoints/nnue.pt")
    ap.add_argument("--out", default="../checkpoints/nnue.bin")
    ap.add_argument("--qa-scale", type=int, default=5478,
                     help="Feature-transformer fixed-point scale (weights go "
                          "into int16, so keep max|weight|*scale under 32767).")
    ap.add_argument("--qb-scale", type=int, default=684,
                     help="fc2 fixed-point scale (weights go into int8, so "
                          "keep max|weight|*scale under 127 -- expect to "
                          "need a smaller value here than qa, e.g. 16-64).")
    ap.add_argument("--qc-scale", type=int, default=126,
                     help="fc3 fixed-point scale (weights go into int8, "
                          "same headroom caveat as qb-scale).")
    ap.add_argument("--qd-scale", type=int, default=82,
                     help="fc4/output fixed-point scale (weights go into "
                          "int8, same headroom caveat as qb-scale).")
    args = ap.parse_args()

    ckpt = torch.load(args.checkpoint, map_location="cpu")
    model = NNUE()
    model.load_state_dict(ckpt["model_state_dict"])
    model.eval()

    weights = {
        "ft.weight": model.ft.weight.detach().numpy(),
        "ft.bias": model.ft_bias.detach().numpy(),
    }
    for name, layer in (("fc2", model.fc2), ("fc3", model.fc3), ("fc4", model.fc4)):
        weights[f"{name}.weight"] = layer.weight.detach().numpy()
        weights[f"{name}.bias"] = layer.bias.detach().numpy()

    with open(args.out, "wb") as f:
        write_nnue_v5(f, weights, NUM_KING_SQUARES, model.l1_size,
                      model.l2_size, model.l3_size, CP_SCALE,
                      args.qa_scale, args.qb_scale, args.qc_scale, args.qd_scale)

    print(f"Exported v5 weights to {args.out} "
          f"(qa={args.qa_scale} qb={args.qb_scale} "
          f"qc={args.qc_scale} qd={args.qd_scale})")


if __name__ == "__main__":
    main()