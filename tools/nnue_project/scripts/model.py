"""
NNUE Architecture: Schoenemann-0.5.0 / Bullet style + Horizontal King Mirroring + Factorized King Buckets.
Topology: (768xK -> 1024)x2 -> 1x8  (K = 1 or 4 king buckets)

Features:
  - 768 sparse perspective-aware base inputs (2 colors x 6 piece types x 64 squares).
  - Perspective horizontal king mirroring (when mirror=True): if a perspective's
    own king is on files e..h ((king_sq & 7) >= 4), all piece squares for that
    perspective are horizontally flipped (sq ^= 7), folding king files into a..d.
  - Perspective king buckets (when king_buckets=4): 4 buckets indexed by
    KING_BUCKET[rel_ksq] (0=castled corner, 1=back-rank center, 2=2nd rank, 3=ranks 3..8).
    Trained with factorized weights (ft_shared[768, 1024] + ft_buckets[4, 768, 1024])
    that fold into a single (4*768, 1024) table at export time.
  - Activation: SCReLU (clamp(x, 0, 1) ** 2).
  - Output: 8 material buckets based on remaining piece count:
      bucket = clamp((pieces - 2) // 4, 0, 7)
    Direct dot-product of concatenated [us_act, them_act] (2048) into bucket weights.
"""

import torch
import torch.nn as nn
import torch.nn.functional as F

NUM_FEATURES = 768
L1_SIZE = 1024
NUM_BUCKETS = 8
QA = 255.0
QB = 64.0
CP_SCALE = 400.0

# Indexed by perspective-relative king square (ksq for White, ksq ^ 56 for Black).
# Symmetric across the d/e file line:
#   Bucket 0: back-rank corner / castled (a1, b1, g1, h1)
#   Bucket 1: back-rank center / uncastled (c1, d1, e1, f1)
#   Bucket 2: 2nd rank (a2..h2)
#   Bucket 3: ranks 3..8 (a3..h8)
KING_BUCKET = [
    0, 0, 1, 1, 1, 1, 0, 0,
    2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3,
]

DEFAULT_ARCH = {"mirror": False, "king_buckets": 1}


class NNUE(nn.Module):
    def __init__(
        self,
        num_features=NUM_FEATURES,
        l1=L1_SIZE,
        num_buckets=NUM_BUCKETS,
        mirror=False,
        king_buckets=1,
        factorized=None,
    ):
        super().__init__()
        self.num_features = num_features
        self.l1_size = l1
        self.num_buckets = num_buckets
        self.king_buckets = int(king_buckets)
        if factorized is None:
            factorized = self.king_buckets > 1
        self.factorized = bool(factorized and self.king_buckets > 1)
        self.arch = {
            "mirror": bool(mirror),
            "king_buckets": self.king_buckets,
            **({"factorized": True} if self.factorized else {}),
        }

        if self.factorized:
            self.ft_buckets = nn.Parameter(torch.zeros(self.king_buckets, num_features, l1))
            self.ft_shared = nn.Parameter(torch.empty(num_features, l1))
        else:
            self.ft = nn.EmbeddingBag(self.king_buckets * num_features, l1, mode="sum")
        self.ft_bias = nn.Parameter(torch.zeros(l1))
        self.output_weights = nn.Parameter(torch.empty(num_buckets, 2 * l1))
        self.output_biases = nn.Parameter(torch.zeros(num_buckets))
        self.reset_parameters()

    def reset_parameters(self):
        if self.factorized:
            nn.init.zeros_(self.ft_buckets)
            nn.init.kaiming_uniform_(self.ft_shared, a=1.0)
        else:
            nn.init.kaiming_uniform_(self.ft.weight, a=1.0)
        nn.init.zeros_(self.ft_bias)
        nn.init.kaiming_uniform_(self.output_weights, a=1.0)
        nn.init.zeros_(self.output_biases)

    def ft_weight(self):
        """Effective feature transformer weight table of shape (king_buckets * num_features, l1)."""
        if self.factorized:
            return (self.ft_buckets + self.ft_shared.unsqueeze(0)).view(
                self.king_buckets * self.num_features, self.l1_size
            )
        return self.ft.weight

    def forward(self, white_idx, black_idx, offsets, stm, buckets):
        """
        white_idx, black_idx: 1-D tensors of active feature indices.
        offsets: 1-D tensor of sample start boundaries (same for white & black).
        stm: (B,) long tensor (0 = white to move, 1 = black to move).
        buckets: (B,) long tensor in [0, 7].
        """
        if self.factorized:
            w = self.ft_weight()
            acc_w = F.embedding_bag(white_idx, w, offsets, mode="sum") + self.ft_bias
            acc_b = F.embedding_bag(black_idx, w, offsets, mode="sum") + self.ft_bias
        else:
            acc_w = self.ft(white_idx, offsets) + self.ft_bias
            acc_b = self.ft(black_idx, offsets) + self.ft_bias

        # Place side-to-move's perspective first
        is_black = stm.bool().unsqueeze(1)
        own = torch.where(is_black, acc_b, acc_w)
        other = torch.where(is_black, acc_w, acc_b)

        # SCReLU activation: clamp to [0, 1] then square
        own_act = torch.clamp(own, 0.0, 1.0) ** 2
        other_act = torch.clamp(other, 0.0, 1.0) ** 2

        x = torch.cat([own_act, other_act], dim=1)  # (B, 2048)

        # Bucket selection: evaluate all 8 buckets as one small matmul, then pick
        # each sample's bucket. Same result as gathering per-sample weight rows,
        # but it avoids materialising a (B, 2048) copy of the weights and the
        # scatter-add that indexing needs in the backward pass.
        all_buckets = x @ self.output_weights.t()                          # (B, 8)
        out = all_buckets.gather(1, buckets.unsqueeze(1)).squeeze(1)       # (B,)
        return out + self.output_biases[buckets]