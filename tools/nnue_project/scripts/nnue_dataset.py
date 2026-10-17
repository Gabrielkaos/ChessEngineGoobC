"""
PyTorch Dataset over the binary record cache (train.bin, val.bin).
Compatible with 68-byte records:
  bytes[0:64]  : board piece codes (0=empty, 1..6=white P..K, 7..12=black p..k)
  byte[64]     : side to move (0=white, 1=black)
  bytes[65:67] : int16 little-endian centipawn eval (White-relative)
  byte[67]     : flags (bit 0 = mate flag, bits 1..2 = game result: 1=W, 2=D, 3=B)

Feature encoding: Schoenemann 768 dual-perspective features (with optional king mirroring):
  White: color * 384 + piece_type * 64 + (sq ^ flip_w)
  Black: (color ^ 1) * 384 + piece_type * 64 + ((sq ^ 56) ^ flip_b)
  where flip_{w,b} = 7 if mirror=True and that perspective's own king is on files e..h (0 otherwise).
Bucket encoding:
  bucket = clamp((pieces - 2) // 4, 0, 7)
Target: sigmoid(cp_stm / CP_SCALE) blended with WDL result when present.

Speed notes:
  - __getitems__ lets the DataLoader fetch a whole batch with one fancy-index
    read of the memmap instead of one Python __getitem__ call (plus a tensor
    allocation) per position. PyTorch >= 2.0 uses it automatically; older
    versions fall back to __getitem__, which still works.
  - nnue_collate maps (piece code, square) straight to both feature indices
    through two small lookup tables and derives the per-sample offsets from the
    piece counts it already needs for the buckets.
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


def nnue_collate(
    batch,
    wdl_lambda: float = 0.75,
    mirror: bool = False,
    flip_augment: bool = False,
    rng: np.random.Generator = None,
):
    """Vectorized batch collate for 768 Schoenemann NNUE features.

    Accepts either a (B, 68) uint8 array (from __getitems__) or a list of
    per-sample (68,) tensors (from __getitem__).

    Args:
      mirror       : If True, horizontally flip each perspective's squares
                     (sq ^ 7) when that perspective's own king is on files e..h.
      flip_augment : If True (and mirror is False), randomly flip both
                     perspectives' squares horizontally (sq ^ 7) with p=0.5 per
                     sample as a symmetry regularization control.

    Returns ((white_idx, black_idx, offsets, stm, buckets), target):
      white_idx, black_idx : 1-D long tensors of active feature indices
      offsets              : 1-D long tensor of sample offsets
      stm                  : (B,) long tensor, 0=white, 1=black
      buckets              : (B,) long tensor in [0, 7]
      target               : (B,) float tensor in [0, 1] (win probability)
    """
    records = _as_records(batch)
    B = records.shape[0]
    b2d = np.ascontiguousarray(records[:, :64])
    boards = b2d.reshape(-1)  # (B*64,)

    # Active squares in row-major order, so features come out grouped by sample.
    # (nonzero on a bool mask is several times faster than on the uint8 bytes.)
    nz = np.nonzero(boards != 0)[0]
    key = boards[nz].astype(np.int64)
    key <<= 6
    key |= nz & 63                      # key = code * 64 + square
    white_idx = _WHITE_FEAT[key]
    black_idx = _BLACK_FEAT[key]

    sample = nz >> 6
    if mirror:
        wk = np.argmax(b2d == 6, axis=1)
        bk = np.argmax(b2d == 12, axis=1)
        w_mirror = (wk & 7) >= 4
        b_mirror = (bk & 7) >= 4
        white_idx = white_idx ^ np.where(w_mirror[sample], 7, 0)
        black_idx = black_idx ^ np.where(b_mirror[sample], 7, 0)
    elif flip_augment:
        g = rng if rng is not None else np.random.default_rng()
        do_flip = g.integers(0, 2, size=B, dtype=np.int64) * 7
        f = do_flip[sample]
        white_idx = white_idx ^ f
        black_idx = black_idx ^ f

    counts = np.bincount(sample, minlength=B)
    offsets = np.zeros(B, dtype=np.int64)
    np.cumsum(counts[:-1], out=offsets[1:])
    buckets = _BUCKET_OF_COUNT[counts]

    stm = records[:, 64].astype(np.int64)
    cp_white = np.ascontiguousarray(records[:, 65:67]).view("<i2").reshape(B).astype(np.float32)
    cp_stm = np.where(stm == 0, cp_white, -cp_white)
    eval_target = 1.0 / (1.0 + np.exp(-cp_stm / np.float32(CP_SCALE)))

    flags = records[:, 67].astype(np.int64)
    res_code = (flags >> 1) & 0x03

    if np.any(res_code > 0):
        # res_code: 1 = White win, 2 = Draw, 3 = Black win
        score_table = np.array([0.0, 1.0, 0.5, 0.0], dtype=np.float32)
        white_result = score_table[res_code]
        wdl_stm = np.where(stm == 0, white_result, 1.0 - white_result)
        lmb = np.float32(wdl_lambda)
        blended = lmb * eval_target + (np.float32(1.0) - lmb) * wdl_stm
        target = np.where(res_code > 0, blended, eval_target)
    else:
        target = eval_target

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