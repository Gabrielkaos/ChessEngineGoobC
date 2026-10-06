"""
Quantization headroom of a checkpoint: the largest QA / QB that would still
fit the engine's integer inference, next to the QA / QB actually used.

    python scale_test.py --checkpoint ../checkpoints/nnue.pt

Feature weights and biases are stored as int16 (x QA); output weights must
stay within +/-128 after x QB for the fast SCReLU kernel. check_net.py does
the full check (including accumulator sums) on an exported net.
"""

import argparse

import torch

from model import QA, QB


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--checkpoint", default="../checkpoints/nnue.pt")
    args = ap.parse_args()

    ckpt = torch.load(args.checkpoint, map_location="cpu")
    sd = ckpt.get("model_state_dict", ckpt)

    for name, scale, limit in (("ft.weight", QA, 32767),
                               ("ft_bias", QA, 32767),
                               ("output_weights", QB, 128)):
        max_abs = sd[name].abs().max().item()
        safe = limit / max_abs if max_abs > 0 else float("inf")
        print(f"{name:15s} max |w| = {max_abs:.4f}  x{scale:.0f} = {max_abs * scale:8.1f}"
              f"  (limit {limit})  largest safe scale {safe:.1f}")


if __name__ == "__main__":
    main()
