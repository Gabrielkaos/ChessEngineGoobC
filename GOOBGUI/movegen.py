from Structs import Pieces
import numpy as np
from variables import black,white,rank7,rank2
from helper_functions import MOVE
from variables import MVFLAGCA,MVFLAGEP,MVFLAGPS
from variables import pieceCol,WKCA,WQCA,BKCA,BQCA,no_sq
from attacks import sqAttacked
from squares import e8,d8,c8,e1,d1,c1,f1,g1,f8,g8,b1,b8

# Variables
piece=Pieces()
LoopSlidePiece=np.array([
    piece.wB,piece.wR,piece.wQ,0,piece.bB,piece.bR,piece.bQ,0
])
LoopNonSlidePiece=np.array([piece.wN,piece.wK,0,piece.bN,piece.bK,0])
LoopSlideIndex=np.array([0,4])
LoopNonSlideIndex=np.array([0,3])
pieceDir=np.array([[ 0, 0, 0, 0, 0, 0, 0, 0],
                   [0, 0, 0, 0, 0, 0, 0,0],
                   [-1, -10,1, 10, 0, 0, 0, 0],
                   [-8, -19,-21, -12, 8, 19, 21, 12],
                   [-9, -11, 11, 9, 0, 0, 0, 0 ],
                   [ -1, -10,1, 10, -9, -11, 11, 9 ],
                   [-1, -10,1, 10, -9, -11, 11, 9],

                   [0, 0, 0, 0, 0, 0, 0, 0],
                   [-1, -10,1, 10, 0, 0, 0, 0],
                   [-8, -19,-21, -12, 8, 19, 21, 12 ],
                   [-9, -11, 11, 9, 0, 0, 0, 0],
                   [-1, -10,1, 10, -9, -11, 11, 9],
                   [ -1, -10,1, 10, -9, -11, 11, 9 ]])
numDir=np.array([0,0,4,8,4,8,8,0,4,8,4,8,8])

def isSqOffboard(gs, sq):
    if gs.board[sq] == piece.offboard:
        return True
    return False
def encodeMove(list, move):
    list.moves[list.count].move = move
    list.count += 1
def addWhitePawnCaptures(list,fromSq,toSq,cap):

    if fromSq in rank7:
        encodeMove(list,MOVE(fromSq,toSq,cap,piece.wQ,0))
        encodeMove(list, MOVE(fromSq, toSq, cap, piece.wR, 0))
        encodeMove(list, MOVE(fromSq, toSq, cap, piece.wB, 0))
        encodeMove(list, MOVE(fromSq, toSq, cap, piece.wN, 0))
    else:
        encodeMove(list,MOVE(fromSq,toSq,cap,piece.empty,0))
def addWhitePawnMoves(list,fromSq,toSq):

    if fromSq in rank7:
        encodeMove(list,MOVE(fromSq,toSq,piece.empty,piece.wQ,0))
        encodeMove(list, MOVE(fromSq, toSq, piece.empty, piece.wB, 0))
        encodeMove(list, MOVE(fromSq, toSq, piece.empty, piece.wR, 0))
        encodeMove(list, MOVE(fromSq, toSq, piece.empty, piece.wN, 0))
    else:
        encodeMove(list, MOVE(fromSq, toSq, piece.empty, piece.empty, 0))
def addBlackPawnCaptures(list,fromSq,toSq,cap):

    if fromSq in rank2:
        encodeMove(list,MOVE(fromSq,toSq,cap,piece.bQ,0))
        encodeMove(list, MOVE(fromSq, toSq, cap, piece.bR, 0))
        encodeMove(list, MOVE(fromSq, toSq, cap, piece.bB, 0))
        encodeMove(list, MOVE(fromSq, toSq, cap, piece.bN, 0))
    else:
        encodeMove(list,MOVE(fromSq,toSq,cap,piece.empty,0))
def addBlackPawnMoves(list,fromSq,toSq):

    if fromSq in rank2:
        encodeMove(list,MOVE(fromSq,toSq,piece.empty,piece.bQ,0))
        encodeMove(list, MOVE(fromSq, toSq, piece.empty, piece.bB, 0))
        encodeMove(list, MOVE(fromSq, toSq, piece.empty, piece.bR, 0))
        encodeMove(list, MOVE(fromSq, toSq, piece.empty, piece.bN, 0))
    else:
        encodeMove(list, MOVE(fromSq, toSq, piece.empty, piece.empty, 0))

def generateAllMoves(gs, list):
    list.count = 0

    if gs.side == white:
        # for white pawns
        for pieceNum in range(gs.pieceNum[piece.wP]):
            sq = gs.pieceList[piece.wP][pieceNum]
            # for pawns just moving forward
            if gs.board[sq + 10] == piece.empty:
                addWhitePawnMoves(list, sq, sq + 10)
                if sq in rank2 and gs.board[sq + 20] == piece.empty:
                    encodeMove(list, MOVE(sq, sq + 20, piece.empty, piece.empty, MVFLAGPS))

            # captures pawn
            if not isSqOffboard(gs,sq + 9) and pieceCol[gs.board[sq + 9]] == black:
                addWhitePawnCaptures(list, sq, sq + 9, gs.board[sq + 9])
            if not isSqOffboard(gs,sq + 11) and pieceCol[gs.board[sq + 11]] == black:
                addWhitePawnCaptures(list, sq, sq + 11, gs.board[sq + 11])

            # enpas
            if gs.enPas != no_sq:
                if (sq + 9) == gs.enPas:
                    encodeMove(list, MOVE(sq, sq + 9, piece.empty, piece.empty, MVFLAGEP))
                if (sq + 11) == gs.enPas:
                    encodeMove(list, MOVE(sq, sq + 11, piece.empty, piece.empty, MVFLAGEP))
        # castle
        if gs.castleRights & WKCA:
            if gs.board[f1] == piece.empty and gs.board[g1] == piece.empty:
                if not sqAttacked(gs,f1, black) and not sqAttacked(gs,e1, black):
                    encodeMove(list, MOVE(e1, g1, piece.empty, piece.empty, MVFLAGCA))
        if gs.castleRights & WQCA:
            if gs.board[d1] == piece.empty and gs.board[c1] == piece.empty and gs.board[b1] == piece.empty:
                if not sqAttacked(gs,d1, black) and not sqAttacked(gs,e1, black):
                    encodeMove(list, MOVE(e1, c1, piece.empty, piece.empty, MVFLAGCA))

    elif gs.side == black:
        # for black pawns
        for pieceNum in range(gs.pieceNum[piece.bP]):
            sq = gs.pieceList[piece.bP][pieceNum]

            # for pawns just moving forward
            if gs.board[sq - 10] == piece.empty:
                addBlackPawnMoves(list, sq, sq - 10)
                if sq in rank7 and gs.board[sq - 20] == piece.empty:
                    encodeMove(list, MOVE(sq, sq - 20, piece.empty, piece.empty, MVFLAGPS))

            # captures pawn
            if not isSqOffboard(gs,sq - 9) and pieceCol[gs.board[sq - 9]] == white:
                addBlackPawnCaptures(list, sq, sq - 9, gs.board[sq - 9])
            if not isSqOffboard(gs,sq - 11) and pieceCol[gs.board[sq - 11]] == white:
                addBlackPawnCaptures(list, sq, sq - 11, gs.board[sq - 11])

            # enpas
            if gs.enPas != no_sq:
                if (sq - 9) == gs.enPas:
                    encodeMove(list, MOVE(sq, sq - 9, piece.empty, piece.empty, MVFLAGEP))
                if (sq - 11) == gs.enPas:
                    encodeMove(list, MOVE(sq, sq - 11, piece.empty, piece.empty, MVFLAGEP))
        # castle
        if gs.castleRights & BKCA:
            if gs.board[f8] == piece.empty and gs.board[g8] == piece.empty:
                if not sqAttacked(gs,f8, white) and not sqAttacked(gs,e8, white):
                    encodeMove(list, MOVE(e8, g8, piece.empty, piece.empty, MVFLAGCA))
        if gs.castleRights & BQCA:
            if gs.board[d8] == piece.empty and gs.board[c8] == piece.empty and gs.board[b8] == piece.empty:
                if not sqAttacked(gs,d8, white) and not sqAttacked(gs,e8, white):
                    encodeMove(list, MOVE(e8, c8, piece.empty, piece.empty, MVFLAGCA))

    # non slider pieces moves
    pce_index = LoopNonSlideIndex[gs.side]
    pce = LoopNonSlidePiece[pce_index]
    pce_index += 1

    while pce != piece.empty:
        for pieceNum in range(gs.pieceNum[pce]):
            sq = gs.pieceList[pce][pieceNum]

            for index in range(numDir[pce]):
                dir = pieceDir[pce][index]
                t_sq = sq + dir

                if isSqOffboard(gs,t_sq):
                    continue

                if gs.board[t_sq] != piece.empty:
                    if (pieceCol[gs.board[t_sq]] == gs.side ^ 1):
                        encodeMove(list, MOVE(sq, t_sq, gs.board[t_sq], piece.empty, 0))
                        # capture move
                    continue
                # not capture move
                encodeMove(list, MOVE(sq, t_sq, piece.empty, piece.empty, 0))
        pce = LoopNonSlidePiece[pce_index]
        pce_index += 1

    #slider pieces
    pce_index = LoopSlideIndex[gs.side]
    pce = LoopSlidePiece[pce_index]
    pce_index += 1
    while pce != piece.empty:
        for pieceNum in range(gs.pieceNum[pce]):
            sq = gs.pieceList[pce][pieceNum]

            for index in range(numDir[pce]):
                dir = pieceDir[pce][index]
                t_sq = sq + dir

                while not isSqOffboard(gs,t_sq):
                    if gs.board[t_sq] != piece.empty:
                        if pieceCol[gs.board[t_sq]] == gs.side ^ 1:
                            encodeMove(list, MOVE(sq, t_sq, gs.board[t_sq], piece.empty, 0))
                            # capture
                        break
                    # quiet move
                    encodeMove(list, MOVE(sq, t_sq, piece.empty, piece.empty, 0))
                    t_sq += dir

        pce = LoopSlidePiece[pce_index]
        pce_index += 1
def generateCapMoves(gs,list):
    list.count = 0

    if gs.side == white:
        # for white pawns
        for pieceNum in range(gs.pieceNum[piece.wP]):
            sq = gs.pieceList[piece.wP][pieceNum]

            # captures pawn
            if not isSqOffboard(gs, sq + 9) and pieceCol[gs.board[sq + 9]] == black:
                addWhitePawnCaptures(list, sq, sq + 9, gs.board[sq + 9])
            if not isSqOffboard(gs, sq + 11) and pieceCol[gs.board[sq + 11]] == black:
                addWhitePawnCaptures(list, sq, sq + 11, gs.board[sq + 11])

            # enpas
            if gs.enPas != no_sq:
                if (sq + 9) == gs.enPas:
                    encodeMove(list, MOVE(sq, sq + 9, piece.empty, piece.empty, MVFLAGEP))
                if (sq + 11) == gs.enPas:
                    encodeMove(list, MOVE(sq, sq + 11, piece.empty, piece.empty, MVFLAGEP))

    elif gs.side == black:
        # for black pawns
        for pieceNum in range(gs.pieceNum[piece.bP]):
            sq = gs.pieceList[piece.bP][pieceNum]

            # captures pawn
            if not isSqOffboard(gs, sq - 9) and pieceCol[gs.board[sq - 9]] == white:
                addBlackPawnCaptures(list, sq, sq - 9, gs.board[sq - 9])
            if not isSqOffboard(gs, sq - 11) and pieceCol[gs.board[sq - 11]] == white:
                addBlackPawnCaptures(list, sq, sq - 11, gs.board[sq - 11])

            # enpas
            if gs.enPas != no_sq:
                if (sq - 9) == gs.enPas:
                    encodeMove(list, MOVE(sq, sq - 9, piece.empty, piece.empty, MVFLAGEP))
                if (sq - 11) == gs.enPas:
                    encodeMove(list, MOVE(sq, sq - 11, piece.empty, piece.empty, MVFLAGEP))

    # non slider pieces moves
    pce_index = LoopNonSlideIndex[gs.side]
    pce = LoopNonSlidePiece[pce_index]
    pce_index += 1

    while pce != piece.empty:
        for pieceNum in range(gs.pieceNum[pce]):
            sq = gs.pieceList[pce][pieceNum]

            for index in range(numDir[pce]):
                dir = pieceDir[pce][index]
                t_sq = sq + dir

                if isSqOffboard(gs, t_sq):
                    continue

                if gs.board[t_sq] != piece.empty:
                    if (pieceCol[gs.board[t_sq]] == gs.side ^ 1):
                        encodeMove(list, MOVE(sq, t_sq, gs.board[t_sq], piece.empty, 0))
                        # capture move
                    continue
        pce = LoopNonSlidePiece[pce_index]
        pce_index += 1

    # slider pieces
    pce_index = LoopSlideIndex[gs.side]
    pce = LoopSlidePiece[pce_index]
    pce_index += 1
    while pce != piece.empty:
        for pieceNum in range(gs.pieceNum[pce]):
            sq = gs.pieceList[pce][pieceNum]

            for index in range(numDir[pce]):
                dir = pieceDir[pce][index]
                t_sq = sq + dir

                while not isSqOffboard(gs, t_sq):
                    if gs.board[t_sq] != piece.empty:
                        if pieceCol[gs.board[t_sq]] == gs.side ^ 1:
                            encodeMove(list, MOVE(sq, t_sq, gs.board[t_sq], piece.empty, 0))
                            # capture
                        break
                    t_sq += dir

        pce = LoopSlidePiece[pce_index]
        pce_index += 1