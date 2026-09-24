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
Target: sigmoid(cp_stm / CP_SCALE), where cp_stm is the stored White-relative
eval negated when black is to move.

Speed notes:
  - __getitems__ lets the DataLoader fetch a whole batch with one fancy-index
    read of the memmap instead of one Python __getitem__ call (plus a tensor
    allocation) per position. PyTorch >= 2.0 uses it automatically; older
    versions fall back to __getitem__, which still works.
  - nnue_collate maps (piece code, square) straight to both feature indices
    through two small lookup tables and derives the per-sample offsets from the
    piece counts it already needs for the buckets.
  Outputs are identical to the previous version, element for element.
"""

import numpy as np
import torch
from torch.utils.data import Dataset

from fen_utils import RECORD_SIZE
from model import CP_SCALE


def _build_tables():
    w = np.zeros(13 * 64, dtype=np.int64)
    b = np.zeros(13 * 64, dtype=np.int64)
    for code in range(1, 13):
        color, pt = (code - 1) // 6, (code - 1) % 6
        for sq in range(64):
            w[code * 64 + sq] = color * 384 + pt * 64 + sq
            b[code * 64 + sq] = (color ^ 1) * 384 + pt * 64 + (sq ^ 56)
    return w, b


_WHITE_FEAT, _BLACK_FEAT = _build_tables()
_BUCKET_OF_COUNT = np.clip((np.arange(65) - 2) // 4, 0, 7).astype(np.int64)


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

    def __getstate__(self):
        # Never pickle an open memmap: that would copy the whole file into
        # every DataLoader worker on spawn-based platforms (Windows, macOS).
        state = self.__dict__.copy()
        state["_mmap"] = None
        return state

    def __len__(self):
        return self.n

    def __getitem__(self, idx):
        self._ensure_mmap()
        return torch.from_numpy(np.array(self._mmap[idx]))

    def __getitems__(self, indices):
        """Whole-batch fetch: returns a (B, 68) uint8 array in the given order."""
        self._ensure_mmap()
        return np.asarray(self._mmap[np.asarray(indices, dtype=np.int64)])


def _as_records(batch) -> np.ndarray:
    if isinstance(batch, np.ndarray):
        return batch
    if torch.is_tensor(batch):
        return batch.numpy()
    return torch.stack(list(batch)).numpy()  # list of per-sample tensors


def nnue_collate(batch):
    """Vectorized batch collate for 768 Schoenemann NNUE features.

    Accepts either a (B, 68) uint8 array (from __getitems__) or a list of
    per-sample (68,) tensors (from __getitem__).

    Returns ((white_idx, black_idx, offsets, stm, buckets), target):
      white_idx, black_idx : 1-D long tensors of active feature indices
      offsets              : 1-D long tensor of sample offsets
      stm                  : (B,) long tensor, 0=white, 1=black
      buckets              : (B,) long tensor in [0, 7]
      target               : (B,) float tensor in [0, 1] (win probability)
    """
    records = _as_records(batch)
    B = records.shape[0]
    boards = np.ascontiguousarray(records[:, :64]).reshape(-1)  # (B*64,)

    # Active squares in row-major order, so features come out grouped by sample.
    # (nonzero on a bool mask is several times faster than on the uint8 bytes.)
    nz = np.nonzero(boards != 0)[0]
    key = boards[nz].astype(np.int64)
    key <<= 6
    key |= nz & 63                      # key = code * 64 + square
    white_idx = _WHITE_FEAT[key]
    black_idx = _BLACK_FEAT[key]

    counts = np.bincount(nz >> 6, minlength=B)
    offsets = np.zeros(B, dtype=np.int64)
    np.cumsum(counts[:-1], out=offsets[1:])
    buckets = _BUCKET_OF_COUNT[counts]

    stm = records[:, 64].astype(np.int64)
    cp_white = np.ascontiguousarray(records[:, 65:67]).view("<i2").reshape(B).astype(np.float32)
    cp_stm = np.where(stm == 0, cp_white, -cp_white)
    target = 1.0 / (1.0 + np.exp(-cp_stm / np.float32(CP_SCALE)))

    return (
        (
            torch.from_numpy(white_idx),
            torch.from_numpy(black_idx),
            torch.from_numpy(offsets),
            torch.from_numpy(stm),
            torch.from_numpy(buckets),
        ),
        torch.from_numpy(target.astype(np.float32, copy=False)),
    )