import numpy as np
from squares import *

MAXDEPTH = 64
maxGameMoves = 2048
no_sq = 99

WKCA = 1
WQCA = 2
BKCA = 4
BQCA = 8

MVFLAGEP = 0x40000
MVFLAGPS = 0x80000
MVFLAGCA = 0x1000000
MVFLAGPROM = 0xF00000

rank7 = np.array([81,82,83,84,85,86,87,88])
rank2 = np.array([31,32,33,34,35,36,37,38])

white = 0
black = 1
both = 2

pieceCol = np.array([both, white, white, white, white, white, white,black, black, black, black, black, black])
pieceVal = np.array([0, 100, 500, 300, 300, 1000, 50000, 100, 500, 300, 300, 1000, 50000])
piecePawn = np.array([False, True, False, False, False, False, False, True, False, False, False, False,False])
pieceKing = np.array([False, False, False, False, False, False, True, False, False, False, False, False,True])
pieceBig=np.array([False,False,True,True,True,True,True,False,True,True,True,True,True])
pieceMaj=np.array([False,False,True,False,False,True,False,False,True,False,False,True,False])
pieceMin=np.array([False,False,False,True,True,False,False,False,False,True,True,False,False])

moveChar = {
    "a1": a1,
    "a2": a2,
    "a3": a3,
    "a4": a4,
    "a5": a5,
    "a6": a6,
    "a7": a7,
    "a8": a8,
    "b1": b1,
    "b2": b2,
    "b3": b3,
    "b4": b4,
    "b5": b5,
    "b6": b6,
    "b7": b7,
    "b8": b8,
    "c1": c1,
    "c2": c2,
    "c3": c3,
    "c4": c4,
    "c5": c5,
    "c6": c6,
    "c7": c7,
    "c8": c8,
    "d1": d1,
    "d2": d2,
    "d3": d3,
    "d4": d4,
    "d5": d5,
    "d6": d6,
    "d7": d7,
    "d8": d8,

    "e1": e1,
    "e2": e2,
    "e3": e3,
    "e4": e4,
    "e5": e5,
    "e6": e6,
    "e7": e7,
    "e8": e8,
    "f1": f1,
    "f2": f2,
    "f3": f3,
    "f4": f4,
    "f5": f5,
    "f6": f6,
    "f7": f7,
    "f8": f8,
    "g1": g1,
    "g2": g2,
    "g3": g3,
    "g4": g4,
    "g5": g5,
    "g6": g6,
    "g7": g7,
    "g8": g8,
    "h1": h1,
    "h2": h2,
    "h3": h3,
    "h4": h4,
    "h5": h5,
    "h6": h6,
    "h7": h7,
    "h8": h8,
    "--": no_sq

}
moveCharRe = {v: k for k,v in moveChar.items()}