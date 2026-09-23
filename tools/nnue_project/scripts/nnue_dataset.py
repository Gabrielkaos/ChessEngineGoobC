"""
PyTorch Dataset over the binary record cache (train1.bin, val1.bin).
Compatible with 68-byte records:
  bytes[0:64]  : board piece codes (0=empty, 1..6=white P..K, 7..12=black p..k)
  byte[64]     : side to move (0=white, 1=black)
  bytes[65:67] : int16 little-endian centipawn eval (White-relative)
  byte[67]     : flags (mate flag)

Feature encoding: Schoenemann 768 dual-perspective features:
  White: color * 384 + piece_type * 64 + sq
  Black: (color ^ 1) * 384 + piece_type * 64 + (sq ^ 56)
Bucket encoding:
  bucket = clamp((pieces - 2) // 4, 0, 7)
"""

import numpy as np
import torch
from torch.utils.data import Dataset

from fen_utils import RECORD_SIZE
from model import CP_SCALE


class NNUEDataset(Dataset):
    def __init__(self, bin_path: str):
        self.path = bin_path
        with open(bin_path, "rb") as f:
            f.seek(0, 2)
            size = f.tell()
        assert size % RECORD_SIZE == 0, "corrupt cache file"
        self.n = size // RECORD_SIZE
        self._mmap = None

    def _ensure_mmap(self):
        if self._mmap is None:
            self._mmap = np.memmap(
                self.path, dtype=np.uint8, mode="r", shape=(self.n, RECORD_SIZE)
            )

    def __len__(self):
        return self.n

    def __getitem__(self, idx):
        self._ensure_mmap()
        return torch.from_numpy(np.array(self._mmap[idx]))


def nnue_collate(batch):
    """Vectorized batch collate for 768 Schoenemann NNUE features.

    Returns ((white_idx, black_idx, offsets, stm, buckets), target):
      white_idx, black_idx : 1-D long tensors of active feature indices
      offsets              : 1-D long tensor of sample offsets
      stm                  : (B,) long tensor, 0=white, 1=black
      buckets              : (B,) long tensor in [0, 7]
      target               : (B,) float tensor in [0, 1] (win probability)
    """
    records = torch.stack(batch).numpy()  # (B, 68) uint8
    boards = records[:, :64]              # (B, 64) piece codes
    stm = records[:, 64].astype(np.int64)

    eval_lo = records[:, 65].astype(np.int32)
    eval_hi = records[:, 66].astype(np.int32)
    eval_raw = eval_lo | (eval_hi << 8)
    eval_raw = np.where(eval_raw >= 32768, eval_raw - 65536, eval_raw)
    cp_white = eval_raw.astype(np.float32)
    cp_stm = np.where(stm == 0, cp_white, -cp_white)
    target = 1.0 / (1.0 + np.exp(-cp_stm / CP_SCALE))

    B = records.shape[0]

    # Material buckets based on piece count: (pieces - 2) // 4 in [0, 7]
    pieces_cnt = np.count_nonzero(boards, axis=1)
    buckets = np.clip((pieces_cnt - 2) // 4, 0, 7).astype(np.int64)

    # Active pieces across batch
    rows, cols = np.nonzero(boards)
    pieces = boards[rows, cols].astype(np.int64)
    piece_color = np.where(pieces <= 6, 0, 1)  # 0=white, 1=black
    piece_type0 = (pieces - 1) % 6             # 0..5 (P..K)

    # White perspective:
    feat_w = piece_color * 384 + piece_type0 * 64 + cols

    # Black perspective (flip color, mirror rank via XOR 56):
    feat_b = (piece_color ^ 1) * 384 + piece_type0 * 64 + (cols ^ 56)

    counts = np.bincount(rows, minlength=B)
    offsets = np.zeros(B, dtype=np.int64)
    offsets[1:] = np.cumsum(counts)[:-1]

    white_idx = torch.from_numpy(feat_w.astype(np.int64))
    black_idx = torch.from_numpy(feat_b.astype(np.int64))
    offsets_t = torch.from_numpy(offsets)
    stm_t = torch.from_numpy(stm)
    buckets_t = torch.from_numpy(buckets)

    return (
        (white_idx, black_idx, offsets_t, stm_t, buckets_t),
        torch.from_numpy(target.astype(np.float32)),
    )