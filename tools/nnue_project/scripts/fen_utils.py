"""
FEN <-> compact board encoding.

Board encoding used everywhere in this project (68-byte training record):
  bytes[0:64] : one byte per square, square index = rank*8 + file,
                rank 0 = the '1' rank (a1..h1), file 0 = 'a' file.
                Piece codes:
                  0  = empty
                  1..6  = white P, N, B, R, Q, K
                  7..12 = black P, N, B, R, Q, K
  byte[64]    : side to move, 0 = white, 1 = black
  bytes[65:67]: int16 little-endian centipawn eval, ALWAYS from White's
                point of view (positive = good for White), clipped to
                [-3000, 3000]
  byte[67]    : flags, bit0 = 1 if this position is a "mate" score
                (the int16 eval field then holds +/-3000 as a saturated
                stand-in and should usually be skipped or handled specially
                during training)

This mirrors the fields in the Lichess/chess-position-evaluations dataset:
  fen, line, depth, knodes, cp, mate
We only use fen, cp and mate here.

Perf notes (this file is on the hot path -- called once per row, millions
of times, during data prep):
  - fen_to_board_and_stm() parses the placement field in a single pass
    with no fen.split() list allocation and no isdigit()/int() calls.
  - pack_record() builds the board directly and hands it straight to
    struct.pack without an intermediate 65-byte slice-and-reslice.
"""

import struct

PIECE_CODE = {
    "P": 1, "N": 2, "B": 3, "R": 4, "Q": 5, "K": 6,
    "p": 7, "n": 8, "b": 9, "r": 10, "q": 11, "k": 12,
}

RECORD_STRUCT = struct.Struct("<64sBhB")  # 64 bytes, 1 byte, int16, 1 byte
RECORD_SIZE = RECORD_STRUCT.size  # 68

_pack = RECORD_STRUCT.pack
_unpack = RECORD_STRUCT.unpack


def fen_to_board_and_stm(fen: str):
    """Parse the piece-placement + side-to-move fields of a FEN.

    Returns (board: bytearray[64], stm: int) where stm is 0 for white,
    1 for black. This is the fast core parser; fen_to_board_bytes() below
    is kept only for backwards compatibility with old callers.
    """
    space = fen.find(" ")
    if space == -1:
        placement = fen
        stm = 0
    else:
        placement = fen[:space]
        # side-to-move is the single character right after the first space
        stm = 0 if (len(fen) <= space + 1 or fen[space + 1] == "w") else 1

    board = bytearray(64)
    rank = 7  # FEN starts at rank 8 (index 7)
    file = 0
    for ch in placement:
        if ch == "/":
            rank -= 1
            file = 0
        elif "1" <= ch <= "8":
            file += ord(ch) - 48  # faster than int(ch)
        else:
            board[rank * 8 + file] = PIECE_CODE[ch]
            file += 1

    return board, stm


def fen_to_board_bytes(fen: str) -> bytes:
    """Legacy API: 65-byte blob (64 board bytes + stm byte). Prefer
    fen_to_board_and_stm() in new code -- this just wraps it for anything
    still importing the old signature."""
    board, stm = fen_to_board_and_stm(fen)
    board.append(stm)
    return bytes(board)


def pack_record(fen: str, cp, mate, result: float = None) -> bytes:
    board, stm = fen_to_board_and_stm(fen)

    if mate is not None:
        is_mate = 1
        # mate is White-relative too: positive = White mates.
        eval_cp = 3000 if mate > 0 else -3000
    else:
        is_mate = 0
        eval_cp = int(cp)  # already White-relative, no stm flip needed
        if eval_cp > 3000:
            eval_cp = 3000
        elif eval_cp < -3000:
            eval_cp = -3000

    flags = is_mate & 1
    if result is not None:
        # result is from White's POV: 1.0 = White win, 0.5 = Draw, 0.0 = Black win
        if result >= 0.75:
            flags |= (1 << 1)  # 1 = White win
        elif result <= 0.25:
            flags |= (3 << 1)  # 3 = Black win
        else:
            flags |= (2 << 1)  # 2 = Draw

    return _pack(bytes(board), stm, eval_cp, flags)


def unpack_record(record: bytes, with_result: bool = False):
    board, stm, eval_cp, flags = _unpack(record)
    is_mate = bool(flags & 1)
    if not with_result:
        return board, stm, eval_cp, is_mate

    res_code = (flags >> 1) & 0x03
    result = None
    if res_code == 1:
        result = 1.0
    elif res_code == 2:
        result = 0.5
    elif res_code == 3:
        result = 0.0
    return board, stm, eval_cp, is_mate, result