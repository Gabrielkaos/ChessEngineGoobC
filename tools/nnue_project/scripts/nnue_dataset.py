"""
PyTorch Dataset over the binary record cache produced by prepare_data.py.

Feature set: dual-perspective HalfKA (see model.py / nnue_loader.h on the
C side) -- 49152 sparse inputs per perspective (64 king squares x 12
piece types x 64 squares), built from the same 68-byte board records as
before. The record format itself (fen_utils.py) hasn't changed: features
are derived from the raw board at collate time, not baked into the cache.

Target scaling: cp values are cached White-relative (see fen_utils.py),
but the new network is SIDE-TO-MOVE-relative (own perspective is
concatenated first -- see model.py), so here we flip sign for Black to
move before the sigmoid: target = sigmoid(cp_stm / CP_SCALE).

PERFORMANCE NOTE: __getitem__ intentionally does almost nothing -- it
just returns the raw 68-byte record. All feature-index construction
happens in nnue_collate(), which builds an ENTIRE BATCH's worth of
sparse HalfKA indices with a handful of vectorized numpy calls instead
of a Python loop per sample (same reasoning as the old dense version:
a per-sample Python loop, however small, dominates wall clock at
100M+ positions regardless of DataLoader worker count).

How the sparse indices are built:
  - np.nonzero(boards) walks the (B, 64) piece-code matrix in row-major
    order, so the returned `rows` array is already grouped/sorted by
    sample -- exactly the grouping an EmbeddingBag `offsets` tensor
    needs, with no extra sort step.
  - White's king square and Black's king square per sample are found
    with np.argmax(boards == code, axis=1) (first/only occurrence).
  - Each active piece's feature index is computed for BOTH perspectives
    at once, following the same relKing/relSq/relColor/ptc formula as
    nnue_feature_index() in nnue_loader.h on the C side. Black's
    perspective mirrors both the king square and the piece square with
    XOR 56 (flips the rank, a1<->a8), matching Mirror64[] in defs.c.
  - Both perspectives share the same per-sample piece counts, so a
    single `offsets` tensor is valid for both white_idx and black_idx.
"""

import numpy as np
import torch
from torch.utils.data import Dataset

from fen_utils import RECORD_SIZE

CP_SCALE = 410.0

_WHITE_KING_CODE = 6
_BLACK_KING_CODE = 12


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
        # Cheap: just copy the raw 68-byte record out of the memmap.
        # All real work happens in nnue_collate, batched.
        return torch.from_numpy(np.array(self._mmap[idx]))


def nnue_collate(batch):
    """Vectorized batch collate: builds the whole batch's sparse HalfKA
    feature indices (both perspectives) with a handful of numpy calls
    instead of a Python loop per sample. Pass this as `collate_fn` to
    your DataLoader.

    Returns ((white_idx, black_idx, offsets, stm), target):
      white_idx, black_idx : 1-D long tensors, EmbeddingBag `input`
      offsets               : 1-D long tensor, EmbeddingBag `offsets`
                               (valid for both white_idx and black_idx)
      stm                   : (B,) long tensor, 0=white to move, 1=black
      target                : (B,) float tensor in [0, 1]
    """
    records = torch.stack(batch).numpy()  # (B, 68) uint8
    boards = records[:, :64]              # (B, 64) piece codes, 0..12
    stm = records[:, 64].astype(np.int64)

    eval_lo = records[:, 65].astype(np.int32)
    eval_hi = records[:, 66].astype(np.int32)
    eval_raw = eval_lo | (eval_hi << 8)
    eval_raw = np.where(eval_raw >= 32768, eval_raw - 65536, eval_raw)
    cp_white = eval_raw.astype(np.float32)
    # Cache is White-relative; the new net is side-to-move-relative.
    cp_stm = np.where(stm == 0, cp_white, -cp_white)
    target = 1.0 / (1.0 + np.exp(-cp_stm / CP_SCALE))

    B = records.shape[0]

    # Each side has exactly one king, so the first (only) match is it.
    wk_sq = np.argmax(boards == _WHITE_KING_CODE, axis=1)  # (B,)
    bk_sq = np.argmax(boards == _BLACK_KING_CODE, axis=1)  # (B,)

    # Every (sample, square) with a piece on it, in row-major (i.e.
    # sample-grouped, ascending) order.
    rows, cols = np.nonzero(boards)
    pieces = boards[rows, cols].astype(np.int64)      # 1..12
    piece_color = np.where(pieces <= 6, 0, 1)          # 0=white, 1=black
    piece_type0 = (pieces - 1) % 6                     # 0..5 (P..K)

    # White perspective (us=0): no mirroring.
    rel_king_w = wk_sq[rows]
    rel_sq_w = cols
    rel_color_w = np.where(piece_color == 0, 0, 1)     # own=0, enemy=1
    ptc_w = rel_color_w * 6 + piece_type0
    feat_w = (rel_king_w * 12 + ptc_w) * 64 + rel_sq_w

    # Black perspective (us=1): mirror king square and piece square
    # (XOR 56 flips the rank -- a1<->a8 -- matching Mirror64[] in defs.c).
    rel_king_b = bk_sq[rows] ^ 56
    rel_sq_b = cols ^ 56
    rel_color_b = np.where(piece_color == 1, 0, 1)     # own=0, enemy=1
    ptc_b = rel_color_b * 6 + piece_type0
    feat_b = (rel_king_b * 12 + ptc_b) * 64 + rel_sq_b

    counts = np.bincount(rows, minlength=B)
    offsets = np.zeros(B, dtype=np.int64)
    offsets[1:] = np.cumsum(counts)[:-1]

    white_idx = torch.from_numpy(feat_w.astype(np.int64))
    black_idx = torch.from_numpy(feat_b.astype(np.int64))
    offsets_t = torch.from_numpy(offsets)
    stm_t = torch.from_numpy(stm)

    return (
        (white_idx, black_idx, offsets_t, stm_t),
        torch.from_numpy(target.astype(np.float32)),
    )