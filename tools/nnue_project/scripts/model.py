"""
NNUE Architecture: Schoenemann-0.5.0 / Bullet style.
Topology: (768 -> 1024)x2 -> 1x8

Features:
  - 768 sparse perspective-aware inputs (2 colors x 6 piece types x 64 squares).
  - No king square in the feature index: king moves are ordinary O(1) piece moves.
  - Shared feature transformer: 768 -> 1024.
  - Activation: SCReLU (clamp(x, 0, 1) ** 2).
  - Output: 8 material buckets based on remaining piece count:
      bucket = clamp((pieces - 2) // 4, 0, 7)
    Direct dot-product of concatenated [us_act, them_act] (2048) into bucket weights.
"""

import torch
import torch.nn as nn

NUM_FEATURES = 768
L1_SIZE = 1024
NUM_BUCKETS = 8
QA = 255.0
QB = 64.0
CP_SCALE = 400.0


class NNUE(nn.Module):
    def __init__(self, num_features=NUM_FEATURES, l1=L1_SIZE, num_buckets=NUM_BUCKETS):
        super().__init__()
        self.num_features = num_features
        self.l1_size = l1
        self.num_buckets = num_buckets

        self.ft = nn.EmbeddingBag(num_features, l1, mode="sum")
        self.ft_bias = nn.Parameter(torch.zeros(l1))
        self.output_weights = nn.Parameter(torch.empty(num_buckets, 2 * l1))
        self.output_biases = nn.Parameter(torch.zeros(num_buckets))
        self.reset_parameters()

    def reset_parameters(self):
        nn.init.kaiming_uniform_(self.ft.weight, a=1.0)
        nn.init.zeros_(self.ft_bias)
        nn.init.kaiming_uniform_(self.output_weights, a=1.0)
        nn.init.zeros_(self.output_biases)

    def forward(self, white_idx, black_idx, offsets, stm, buckets):
        """
        white_idx, black_idx: 1-D tensors of active feature indices.
        offsets: 1-D tensor of sample start boundaries (same for white & black).
        stm: (B,) long tensor (0 = white to move, 1 = black to move).
        buckets: (B,) long tensor in [0, 7].
        """
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

        # Bucket selection
        selected_w = self.output_weights[buckets]   # (B, 2048)
        selected_b = self.output_biases[buckets]    # (B,)

        out = torch.sum(x * selected_w, dim=1) + selected_b
        return out