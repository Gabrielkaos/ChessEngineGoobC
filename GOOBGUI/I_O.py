from helper_functions import FROMSQ,TOSQ,PROMOTED
from variables import moveChar,moveCharRe,white,black
from Structs import movelist,Pieces
import numpy as np
from movegen import generateAllMoves

piece=Pieces()
corner_left_squares=np.array([0,10,20,30,40,50,60,70,80,90,100,110])

def printMove(move,gs):
    fromSq=FROMSQ(move)
    toSq=TOSQ(move)
    promote=""

    promoteTo=PROMOTED(move)
    if gs.side==white:
        if promoteTo==piece.wQ:
            promote="q"
        elif promoteTo==piece.wB:
            promote="b"
        elif promoteTo==piece.wN:
            promote="n"
        elif promoteTo==piece.wR:
            promote="r"
        else:
            promote=""

    elif gs.side==black:
        if promoteTo==piece.bQ:
            promote="q"
        elif promoteTo==piece.bB:
            promote="b"
        elif promoteTo==piece.bN:
            promote="n"
        elif promoteTo==piece.bR:
            promote="r"
        else:
            promote=""

    print(moveCharRe[fromSq]+moveCharRe[toSq]+promote,end="")
def StrMove(move,gs):
    fromSq=FROMSQ(move)
    toSq=TOSQ(move)
    promote=""

    promoteTo=PROMOTED(move)
    if gs.side==white:
        if promoteTo==piece.wQ:
            promote="q"
        elif promoteTo==piece.wB:
            promote="b"
        elif promoteTo==piece.wN:
            promote="n"
        elif promoteTo==piece.wR:
            promote="r"
        else:
            promote=""

    elif gs.side==black:
        if promoteTo==piece.bQ:
            promote="q"
        elif promoteTo==piece.bB:
            promote="b"
        elif promoteTo==piece.bN:
            promote="n"
        elif promoteTo==piece.bR:
            promote="r"
        else:
            promote=""

    return moveCharRe[fromSq]+moveCharRe[toSq]+promote
def prettyPrint(gs):
    # board = board.reshape(12, 10)
    # board = np.flipud(board)
    #
    # sideChar={white:"w",black:"b"}
    #
    # for j in range(len(board)):
    #     for i, sq in enumerate(board[j]):
    #         if i in corner_left_squares:
    #             print("\n", end="")
    #         elif sq == 0:
    #             print("· ", end="")
    #         elif sq == 1:
    #             print("♟ ", end="")
    #         elif sq == 2:
    #             print("♜ ", end="")
    #         elif sq == 3:
    #             print("♞ ", end="")
    #         elif sq == 4:
    #             print("♝ ", end="")
    #         elif sq == 5:
    #             print("♛ ", end="")
    #         elif sq == 6:
    #             print("♚ ", end="")
    #         elif sq == 7:
    #             print("♙ ", end="")
    #         elif sq == 8:
    #             print("♖ ", end="")
    #         elif sq == 9:
    #             print("♘ ", end="")
    #         elif sq == 10:
    #             print("♗ ", end="")
    #         elif sq == 11:
    #             print("♕ ", end="")
    #         elif sq == 12:
    #             print("♔ ", end="")
    #         if sq == 13:
    #             print("  ", end="")
    # print("")
    # print("Side: "+sideChar[gs.side])
    # print("CastleRights: ",end="")
    #
    # if gs.castleRights & WKCA:a="K"
    # else:a="-"
    # if gs.castleRights & WQCA:b="Q"
    # else:b="-"
    # if gs.castleRights & BKCA:c="k"
    # else:c="-"
    # if gs.castleRights & BQCA:d="q"
    # else:d="-"
    #
    # print(a+b+c+d)
    # print("EnPassant Sq:",moveCharRe[gs.enPas])
    print(f"Position key: {hex(gs.posKey)}")
    print("FEN:",gs.parseBoardToFen())
    print("")
def parseMove(move,gs):

    lists=movelist()
    generateAllMoves(gs,lists)

    blackProChar={"q":piece.bQ,"n":piece.bN,"b":piece.bB,"r":piece.bR}
    whiteProChar = {"q": piece.wQ, "n": piece.wN, "b": piece.wB, "r": piece.wR}

    if len(move)==4:
        FrSq = moveChar[move[0] + move[1]]
        TSq = moveChar[move[2] + move[3]]

        for moveNum in range(lists.count):
            moves=lists.moves[moveNum].move
            # if(moves & MVFLAGPROM) != 0 and FROMSQ(moves)==FrSq and TOSQ(moves)==TSq:
            #     if PROMOTED(moves)==auto_promote:
            #         return moves
            if FROMSQ(moves)==FrSq and TOSQ(moves)==TSq:
                return moves

    #promotion
    if len(move)==5:
        FrSq = moveChar[move[0] + move[1]]
        TSq = moveChar[move[2] + move[3]]

        proChar=move[4]
        promoted=None
        if gs.side==white:
            promoted=whiteProChar[proChar]
        elif gs.side==black:
            promoted=blackProChar[proChar]

        for moveNum in range(lists.count):
            moves=lists.moves[moveNum].move
            if FROMSQ(moves)==FrSq and TOSQ(moves)==TSq:
                promo=PROMOTED(moves)
                if promo==promoted:
                    return moves

    return 0
