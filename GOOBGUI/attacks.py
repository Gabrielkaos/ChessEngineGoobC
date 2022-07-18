from variables import black,white,pieceCol
from Structs import Pieces
import numpy as np

knightDir=np.array([ -8, -19,-21, -12, 8, 19, 21, 12 ])
bishopDir=np.array([ -9, -11, 11, 9 ])
rookDir=np.array([ -1, -10,	1, 10 ])
kingDir=np.array([ -1, -10,	1, 10, -9, -11, 11, 9 ])

pieceKnight=np.array([False,False,False,True,False,False,False,False,False,True,False,False,False])
pieceRookQueen=np.array([False,False,True,False,False,True,False,False,True,False,False,True,False])
pieceBishopQueen=np.array([False,False,False,False,True,True,False,False,False,False,True,True,False])
pieceKing=np.array([False,False,False,False,False,False,True,False,False,False,False,False,True])

piece=Pieces()

def isKi(pce):
    if pieceKing[pce]:
        return True
    return False
def isKni(pce):
    if pieceKnight[pce]:
        return True
    return False
def isRQ(pce):
    if pieceRookQueen[pce]:
        return True
    return False
def isBQ(pce):
    if pieceBishopQueen[pce]:
        return True
    return False

def sqAttacked(gs, sq, side):

    if side == white:
        # pawns
        if gs.board[sq - 11] == piece.wP or gs.board[sq - 9] == piece.wP:
            return True
    elif side == black:
        # pawns
        if gs.board[sq + 11] == piece.bP or gs.board[sq + 9] == piece.bP:
            return True
    # knights
    for dir in knightDir:
        pce = gs.board[sq + dir]
        if pce != piece.offboard and isKni(pce) and pieceCol[pce] == side:
            return True
    # rooks and queen
    for dir in rookDir:
        t_sq = sq + dir
        pce = gs.board[t_sq]
        while pce != piece.offboard:
            if pce != piece.empty:
                if isRQ(pce) and pieceCol[pce] == side:
                    return True
                break
            t_sq += dir
            pce = gs.board[t_sq]
    # queen and bishop
    for dir in bishopDir:
        t_sq = sq + dir
        pce = gs.board[t_sq]
        while pce != piece.offboard:
            if pce != piece.empty:
                if isBQ(pce) and pieceCol[pce] == side:
                    return True
                break
            t_sq += dir
            pce = gs.board[t_sq]
    # kings
    for i in kingDir:
        pce = gs.board[sq + i]
        if pce != piece.offboard and isKi(pce) and pieceCol[pce] == side:
            return True

    return False