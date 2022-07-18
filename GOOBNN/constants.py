from squares import *
import numpy as np

# { EMPTY, wP, wN, wB, wR, wQ, wK, bP, bN, bB, bR, bQ, bK }
EMPTY=0
wP=1
wN=2
wB=3
wR=4
wQ=5
wK=6
bP=7
bN=8
bB=9
bR=10
bQ=11
bK=12
NO_SQ=64
white=0
black=1
both=2
OFF_BOARD=13

# castles
WKCA = 1
WQCA = 2
BKCA = 4
BQCA = 8

seed=np.uint64(1070372)

sq64_to_sq120 = np.zeros(64, dtype=np.int)
sq120_to_sq64 = np.zeros(120, dtype=np.int)

sideKeys=np.uint64(6428209151850780823)
castleKeys=np.zeros(16,dtype=np.uint64)
pieceKeys=np.zeros((13,64),dtype=np.uint64)

def rand_64():

    global seed

    seed ^= seed >> np.uint64(12)
    seed ^= seed << np.uint64(25)
    seed ^= seed >> np.uint64(27)

    return seed * np.uint64(2685821657736338717)
def initKeys():
    for i in range(13):
        for j in range(64):
            pieceKeys[i][j]=rand_64()

    _=rand_64()

    for i in range(16):
        castleKeys[i]=rand_64()
initKeys()

def FR_to_SQ(file,rank):
    return (21 + file) + (rank * 10)
def init_sq_converters():
    for index in range(120):
        sq120_to_sq64[index] = OFF_BOARD
    for index in range(64):
        sq64_to_sq120[index] = OFF_BOARD

    sq64_index = 0

    for row in range(8):
        for col in range(8):
            sq = FR_to_SQ(col, row)
            sq64_to_sq120[sq64_index] = sq
            sq120_to_sq64[sq] = sq64_index

            sq64_index += 1
init_sq_converters()

str_to_sq = {
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
    "-": NO_SQ
}