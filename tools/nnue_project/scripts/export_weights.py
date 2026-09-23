"""
Export a trained Schoenemann NNUE checkpoint to a binary weight file (quantised.bin).
Compatible with Schoenemann and GOOB engines.

Format:
  int16[768 * 1024]   feature transformer weights (quantized with QA=255)
  int16[1024]         feature transformer biases  (quantized with QA=255)
  int16[8 * 2048]     output layer weights        (quantized with QB=64)
  int16[8]            output layer biases         (quantized with QA*QB=16320)
  bytes[48]           padding (optional, bullet format compatibility)

Total: 803,848 int16 values (1,607,696 bytes) + 48 bytes padding = 1,607,744 bytes (~1.53 MB).
"""

import argparse
import numpy as np
import torch

from model import NNUE, QA, QB


def quantize(val, scale, name):
    v = np.round(val.astype(np.float64) * scale)
    limit = 32767
    max_val = np.max(np.abs(v))
    if max_val > limit:
        print(f"Warning: {name} maximum value {max_val:.1f} exceeds int16 range (+/-{limit}). Clamping.")
        v = np.clip(v, -32768, 32767)
    return v.astype("<i2")


def export_checkpoint(checkpoint_path: str, out_path: str):
    print(f"Loading checkpoint: {checkpoint_path}")
    checkpoint = torch.load(checkpoint_path, map_location="cpu")
    if "model_state_dict" in checkpoint:
        sd = checkpoint["model_state_dict"]
    else:
        sd = checkpoint

    ft_w = sd["ft.weight"].cpu().numpy()            # (768, 1024)
    ft_b = sd["ft_bias"].cpu().numpy()              # (1024,)
    out_w = sd["output_weights"].cpu().numpy()      # (8, 2048)
    out_b = sd["output_biases"].cpu().numpy()       # (8,)

    print(f"Feature weights: {ft_w.shape}, range [{ft_w.min():.4f}, {ft_w.max():.4f}]")
    print(f"Feature bias:    {ft_b.shape}, range [{ft_b.min():.4f}, {ft_b.max():.4f}]")
    print(f"Output weights:  {out_w.shape}, range [{out_w.min():.4f}, {out_w.max():.4f}]")
    print(f"Output bias:     {out_b.shape}, range [{out_b.min():.4f}, {out_b.max():.4f}]")

    q_ft_w = quantize(ft_w, QA, "ft.weight")
    q_ft_b = quantize(ft_b, QA, "ft.bias")
    q_out_w = quantize(out_w, QB, "output_weights")
    q_out_b = quantize(out_b, QA * QB, "output_biases")

    with open(out_path, "wb") as f:
        f.write(q_ft_w.tobytes())
        f.write(q_ft_b.tobytes())
        f.write(q_out_w.tobytes())
        f.write(q_out_b.tobytes())
        # Optional 48 bytes trailing padding matching bullet
        f.write(b"bullet" * 8)

    file_size = len(q_ft_w.tobytes()) + len(q_ft_b.tobytes()) + len(q_out_w.tobytes()) + len(q_out_b.tobytes()) + 48
    print(f"Exported {file_size} bytes to {out_path} successfully.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--checkpoint", required=True, help="Path to PyTorch checkpoint (.pt)")
    ap.add_argument("--out", default="../checkpoints/quantised.bin", help="Path to output binary (.bin)")
    args = ap.parse_args()

    export_checkpoint(args.checkpoint, args.out)


if __name__ == "__main__":
    main()