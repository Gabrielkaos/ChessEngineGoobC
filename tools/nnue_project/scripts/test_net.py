import torch
from model import NNUE

ckpt = torch.load("../checkpoints/nnue.pt", map_location="cpu")
model = NNUE()
model.load_state_dict(ckpt["model_state_dict"])
model.eval()

def feat(king_sq, us_king, board):  # board: dict sq->piece_code (1..12), king_sq per perspective
    idx = []
    for sq, pce in board.items():
        us = 0  # perspective always "us" here
        # reuse the exact same formula as nnue_collate for a single sample
        ...

# Simplest: just reuse nnue_collate() on a 1-row fake batch.
from fen_utils import pack_record
from nnue_dataset import nnue_collate
import torch as T

fen_white_up_queen_white_to_move = "r2qkb1r/pppb1ppp/8/3Pp3/8/5N2/PPPP1PPP/RNBQ1RK1 w kq - 0 7"
fen_white_up_queen_black_to_move = "r2qkb1r/pppb1ppp/8/3Pp3/8/5N2/PPPP1PPP/RNBQ1RK1 b kq - 0 7"

for fen in (fen_white_up_queen_white_to_move, fen_white_up_queen_black_to_move):
    rec = pack_record(fen, 900, None)  # cp value here doesn't matter, only features/stm do
    batch = [T.from_numpy(__import__("numpy").frombuffer(rec, dtype="uint8").copy())]
    (white_idx, black_idx, offsets, stm), _ = nnue_collate(batch)
    with T.no_grad():
        out = model(white_idx, offsets, black_idx, offsets, stm)
    print(fen, "raw logit:", out.item(), "cp approx:", out.item() * 410.0)