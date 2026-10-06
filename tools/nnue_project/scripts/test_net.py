"""
Evaluate positions with a saved PyTorch checkpoint (float model).

    python test_net.py --checkpoint ../checkpoints/nnue.pt
    python test_net.py --checkpoint ../checkpoints/nnue.pt --fen "<fen>" --fen "<fen>"

Prints the raw output and the side-to-move centipawns it stands for
(output * CP_SCALE, the same scaling the engine applies). check_net.py
compares this float model with an exported quantised.bin.
"""

import argparse

import numpy as np
import torch

from fen_utils import pack_record
from model import NNUE, CP_SCALE
from nnue_dataset import nnue_collate

DEFAULT_FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    # White is a queen up, with each side to move
    "r2qkb1r/pppb1ppp/8/3Pp3/8/5N2/PPPP1PPP/RNBQ1RK1 w kq - 0 7",
    "r2qkb1r/pppb1ppp/8/3Pp3/8/5N2/PPPP1PPP/RNBQ1RK1 b kq - 0 7",
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--checkpoint", default="../checkpoints/nnue.pt")
    ap.add_argument("--fen", action="append", help="position to evaluate (repeatable)")
    args = ap.parse_args()

    ckpt = torch.load(args.checkpoint, map_location="cpu")
    model = NNUE()
    model.load_state_dict(ckpt.get("model_state_dict", ckpt))
    model.eval()

    for fen in args.fen or DEFAULT_FENS:
        rec = np.frombuffer(pack_record(fen, 0, None), dtype=np.uint8).reshape(1, -1)
        features, _ = nnue_collate(rec)   # the cp in the record only affects the target
        with torch.no_grad():
            out = model(*features).item()
        print(f"{out:+.4f} raw  {out * CP_SCALE:+8.1f} cp (side to move)  {fen}")


if __name__ == "__main__":
    main()
