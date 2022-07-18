import time
from makemove import undoMove,makeMove
from Structs import movelist
from movegen import generateAllMoves
from variables import MVFLAGCA,MVFLAGEP,MVFLAGPS
from helper_functions import FROMSQ,TOSQ,CAPTURED,PROMOTED
from variables import white,black,piecePawn,pieceKing,pieceCol,pieceVal,no_sq
from squares import d1,c1,d8,c8,f1,g1,f8,g8,a1,a8,h1,h8
from Structs import Pieces
import numpy as np
from attacks import sqAttacked

piece=Pieces()
castlePerm=np.array([
    15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,
    15,13,15,15,15,12,15,15,14,15,
    15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,
    15, 7,15,15,15, 3,15,15,11,15,
    15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15

])

def makeMoves(gs, move):
    fromSq = FROMSQ(move)
    toSq = TOSQ(move)
    side = gs.side

    #gs.history[gs.hisPly].poskey = gs.posKey

    # if enpassant
    if (move & MVFLAGEP):
        if side == white:
            clearPieces(gs,toSq - 10)
        elif side == black:
            clearPieces(gs,toSq + 10)

    # if castle
    if (move & MVFLAGCA):
        if toSq == c1:
            movePieces(gs,a1, d1)
        elif toSq == g1:
            movePieces(gs,h1, f1)

        elif toSq == c8:
            movePieces(gs,a8, d8)
        elif toSq == g8:
            movePieces(gs,h8, f8)

    gs.history[gs.hisPly].move = move
    #gs.history[gs.hisPly].fiftyMove = gs.fiftyMove
    gs.history[gs.hisPly].enPas = gs.enPas
    gs.history[gs.hisPly].castleRights = gs.castleRights

    # update castle rights
    gs.castleRights &= castlePerm[fromSq]
    gs.castleRights &= castlePerm[toSq]

    # enpas
    gs.enPas = no_sq

    captured = CAPTURED(move)
    if captured != piece.empty:
        clearPieces(gs,toSq)

    #gs.ply += 1
    gs.hisPly += 1

    if piecePawn[gs.board[fromSq]]:
        if (move & MVFLAGPS):
            if side == white:
                gs.enPas = fromSq + 10
            elif side == black:
                gs.enPas = fromSq - 10

    movePieces(gs,fromSq, toSq)

    promoted_piece = PROMOTED(move)
    if promoted_piece != piece.empty:
        clearPieces(gs,toSq)
        addPieces(gs,toSq, promoted_piece)

    if pieceKing[gs.board[toSq]]:
        gs.kingSq[gs.side] = toSq

    gs.side ^= 1

    if sqAttacked(gs, gs.kingSq[side], gs.side):
        undoMove(gs)
        return False
    return True
def undoMoves(gs):
    #gs.ply-=1
    gs.hisPly-=1

    move=gs.history[gs.hisPly].move

    fromSq=FROMSQ(move)
    toSq=TOSQ(move)

    gs.castleRights=gs.history[gs.hisPly].castleRights
    gs.enPas = gs.history[gs.hisPly].enPas
    #gs.fiftyMove = gs.history[gs.hisPly].fiftyMove

    gs.side^=1

    if (move & MVFLAGEP):
        if gs.side==white:
            addPieces(gs,toSq - 10, piece.bP)
        elif gs.side==black:
            addPieces(gs,toSq + 10, piece.wP)

    if (move & MVFLAGCA):
        if toSq==c1:
            movePieces(gs,d1, a1)
        elif toSq==g1:
            movePieces(gs,f1, h1)

        elif toSq==c8:
            movePieces(gs,d8, a8)
        elif toSq==g8:
            movePieces(gs,f8, h8)

    movePieces(gs,toSq, fromSq)

    if pieceKing[gs.board[fromSq]]:
        gs.kingSq[gs.side]=fromSq

    captured=CAPTURED(move)
    if captured != piece.empty:
        addPieces(gs,toSq, captured)

    promoted=PROMOTED(move)
    if promoted != piece.empty:
        clearPieces(gs,fromSq)
        addPieces(gs,fromSq, (piece.wP if pieceCol[promoted] == white else piece.bP))

def clearPieces(gs, sq):
    pce=gs.board[sq]
    # col=pieceCol[pce]
    t_piece=-1

    gs.board[sq]=piece.empty

    #gs.material[col]-=pieceVal[pce]

    #remove from piecelist
    for index in range(gs.pieceNum[pce]):
        if(gs.pieceList[pce][index]==sq):
            t_piece=index
            break

    gs.pieceNum[pce]-=1
    gs.pieceList[pce][t_piece]=gs.pieceList[pce][gs.pieceNum[pce]]
def movePieces(gs, fromSq, toSq):
    pce=gs.board[fromSq]

    gs.board[fromSq]=piece.empty
    gs.board[toSq]=pce

    for index in range(gs.pieceNum[pce]):
        if(gs.pieceList[pce][index]==fromSq):
            gs.pieceList[pce][index]=toSq
            break
def addPieces(gs, sq, pce):
    # col=pieceCol[pce]

    gs.board[sq]=pce

    # gs.material[col]+=pieceVal[pce]

    gs.pieceList[pce][gs.pieceNum[pce]]=sq
    gs.pieceNum[pce]+=1

def MoveGenerationTest(gs,depth):
    global leafnodes
    if depth==0:
        leafnodes+=1
        return
    lists=movelist()
    generateAllMoves(gs,lists)
    for moveNum in range(lists.count):
        if not (makeMove(gs,lists.moves[moveNum].move)):
            continue
        MoveGenerationTest(gs,depth-1)
        undoMove(gs)

        # print("\tBranch: ",end="")
        # printMove(move,gs)
        # print()

    return
def MoveGenerationTests(gs,depth):
    global leafnodes
    if depth==0:
        leafnodes+=1
        return
    lists=movelist()
    generateAllMoves(gs,lists)
    for moveNum in range(lists.count):
        if not (makeMoves(gs,lists.moves[moveNum].move)):
            continue
        MoveGenerationTests(gs,depth-1)
        undoMoves(gs)

        # print("\tBranch: ",end="")
        # printMove(move,gs)
        # print()

    return
def perftTest(gs,depth,orig=False):
    global leafnodes

    leafnodes=0
    lists=movelist()
    generateAllMoves(gs,lists)
    start=time.time()
    no_of_moves=0

    for moveNum in range(lists.count):
        move=lists.moves[moveNum].move
        if not orig:
            if not (makeMoves(gs, move)):
                continue
        else:
            if not (makeMove(gs,move)):
                continue

        no_of_moves+=1

        # cumnodes=leafnodes
        if not orig:
            MoveGenerationTests(gs,depth-1)
        else:
            MoveGenerationTest(gs,depth-1)

        if not orig:
            undoMoves(gs)
        else:
            undoMove(gs)

        # oldnodes=leafnodes-cumnodes
        # print(f"{no_of_moves} | ",end="")
        # printMove(move, gs)
        # print(f" | {oldnodes}")

    print(f"Depth: {depth} | Nodes : {leafnodes} | Total Time: {(time.time()-start):.4f}s")

    return leafnodes