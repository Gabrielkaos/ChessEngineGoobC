import ctypes
import os

_lib_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "libgoob.so"))
_lib = ctypes.CDLL(_lib_path)

_lib.AllInit()
tuneMode = ctypes.c_int.in_dll(_lib, "tuneMode")
tuneMode.value = 1

def evalFen(fen: str):
    """
    Returns (mg, eg) classical evaluation for the given FEN.
    """
    mg = ctypes.c_int()
    eg = ctypes.c_int()
    fen_bytes = fen.encode('utf-8')
    _lib.eval_fen_c(fen_bytes, ctypes.byref(mg), ctypes.byref(eg))
    return (mg.value, eg.value)

def evalFenPKResidual(fen: str):
    """
    Returns (mg, eg) of the PK residual the engine's PKNet replaces:
    pawn structure eval + king/pawn safety + passed-pawn eval
    (eval_fen_pk_residual_c).

    The engine keeps evalKing/threats/space/psqtmat/closedness/complexity
    classical and adds them on top of the net's output, so these labels --
    NOT the full classical eval -- are what the net must learn.
    """
    mg = ctypes.c_int()
    eg = ctypes.c_int()
    fen_bytes = fen.encode('utf-8')
    _lib.eval_fen_pk_residual_c(fen_bytes, ctypes.byref(mg), ctypes.byref(eg))
    return (mg.value, eg.value)

if __name__ == "__main__":
    print(evalFen("rnbqkbnr/pp1ppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"))

import threading
def test_threads():
    for _ in range(100):
        assert evalFen("rnbqkbnr/pp1ppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1") == (68, 32)

if __name__ == "__main__":
    threads = [threading.Thread(target=test_threads) for _ in range(10)]
    for t in threads: t.start()
    for t in threads: t.join()
    print("Thread test passed")
