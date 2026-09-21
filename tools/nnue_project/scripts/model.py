"""
NNUE architecture: dual-perspective HalfKA feature transformer + 3 small
linear layers.

Feature transformer: 49152 (=64 king squares x 12 piece types x 64
squares) sparse inputs per perspective, implemented as an EmbeddingBag
(mode='sum') instead of a dense Linear -- a HalfKA sample only has as
many active features as there are pieces on the board (<=32), so a
dense 49152-wide matmul would be almost entirely wasted work compared
to summing the <=32 active rows directly.

Both perspectives (the board as seen from White's king, and the board
as seen from Black's king) share the SAME feature-transformer weights
-- one nn.EmbeddingBag, called twice with two different index sets
built in nnue_dataset.py's nnue_collate(). This mirrors the C-side
loader, where a single ft_w[feature][l1] table is shared by both
accumulators.

Forward pass:
  acc_w = clip(ft(white_features) + ft_bias, 0, 1)
  acc_b = clip(ft(black_features) + ft_bias, 0, 1)
  x = concat([own_acc, other_acc])   -- own perspective FIRST, chosen
      per-sample by stm, which is what makes the network's raw output
      naturally side-to-move-relative (see nnue_dataset.py) instead of
      always White-relative like the old single-perspective net.
  x = clip(fc2(x)); x = clip(fc3(x)); out = fc4(x)

Returns a raw scalar in roughly [-8, 8] logit space; apply sigmoid to
get the same [0, 1] "win probability-like" scale used for training
targets. Multiply logit by CP_SCALE (see nnue_dataset.py) and you get
back something in centipawn units if you want a human-readable number.
"""

import torch
import torch.nn as nn

NUM_KING_SQUARES = 64
NUM_PIECE_TYPES = 6          # P, N, B, R, Q, K
NUM_PTC = 12                  # own/enemy x 6 piece types
NUM_FEATURES = NUM_KING_SQUARES * NUM_PTC * 64   # 49152


class ClippedReLU(nn.Module):
    def forward(self, x):
        return torch.clamp(x, 0.0, 1.0)


class NNUE(nn.Module):
    def __init__(self, num_features=NUM_FEATURES, l1=1024, l2=128, l3=64):
        super().__init__()
        self.num_features = num_features
        self.l1_size = l1
        self.l2_size = l2
        self.l3_size = l3

        # Shared feature transformer -- one weight table, used for both
        # perspectives. EmbeddingBag(mode='sum') has no built-in bias,
        # so it's a separate learnable parameter added in forward().
        self.ft = nn.EmbeddingBag(num_features, l1, mode="sum")
        self.ft_bias = nn.Parameter(torch.zeros(l1))

        self.fc2 = nn.Linear(2 * l1, l2)
        self.fc3 = nn.Linear(l2, l3)
        self.fc4 = nn.Linear(l3, 1)
        self.act = ClippedReLU()

    def forward(self, white_idx, white_off, black_idx, black_off, stm):
        """
        white_idx/white_off, black_idx/black_off: EmbeddingBag-style
            (values, offsets) pairs -- the active HalfKA feature indices
            for White's perspective and Black's perspective respectively,
            for every sample in the batch (see nnue_collate). In practice
            white_off and black_off are the same tensor (same piece count
            per sample regardless of perspective) -- kept as separate
            parameters for clarity at the call site.
        stm: (B,) long tensor, 0 = white to move, 1 = black to move.
        """
        acc_w = self.act(self.ft(white_idx, white_off) + self.ft_bias)
        acc_b = self.act(self.ft(black_idx, black_off) + self.ft_bias)

        # Per-sample: side-to-move's own accumulator first, other side's
        # second. stm==0 (white to move) -> [acc_w, acc_b]; stm==1 -> [acc_b, acc_w].
        is_black = stm.bool().unsqueeze(1)              # (B, 1)
        own = torch.where(is_black, acc_b, acc_w)
        other = torch.where(is_black, acc_w, acc_b)
        x = torch.cat([own, other], dim=1)               # (B, 2*l1)

        x = self.act(self.fc2(x))
        x = self.act(self.fc3(x))
        x = self.fc4(x)
        return x.squeeze(-1)