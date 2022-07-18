from variables import MVFLAGCA,MVFLAGEP,MVFLAGPS
from helper_functions import FROMSQ,TOSQ,CAPTURED,PROMOTED,get_game_phase
from variables import white,black,piecePawn,pieceKing,pieceCol,pieceVal,no_sq,pieceMaj,pieceBig
from squares import d1,c1,d8,c8,f1,g1,f8,g8,a1,a8,h1,h8
from Structs import Pieces,movelist
import numpy as np
from movegen import generateAllMoves
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

def hash_ep(gs):
    hash_piece = gs.pieceKeys[piece.empty][gs.enPas]
    gs.posKey ^= hash_piece
def hash_ca(gs):
    hash_piece = gs.castleKeys[gs.castleRights]
    gs.posKey ^= hash_piece
def hash_side(gs):
    hash_piece = gs.sideKey
    gs.posKey ^= hash_piece
def hash_pce(gs,pce,sq):
    hash_piece = gs.pieceKeys[pce][sq]
    gs.posKey ^= hash_piece

def makeMove(gs, move):
    fromSq = FROMSQ(move)
    toSq = TOSQ(move)
    side = gs.side

    gs.history[gs.hisPly].poskey = gs.posKey

    # if enpassant
    if (move & MVFLAGEP):
        if side == white:
            clearPiece(gs,toSq - 10)
        elif side == black:
            clearPiece(gs,toSq + 10)

    # if castle
    if (move & MVFLAGCA):
        if toSq == c1:
            movePiece(gs,a1, d1)
        elif toSq == g1:
            movePiece(gs,h1, f1)

        elif toSq == c8:
            movePiece(gs,a8, d8)
        elif toSq == g8:
            movePiece(gs,h8, f8)

    # hash enpassant
    if gs.enPas != no_sq:
        hash_ep(gs)

    # hash castle
    hash_ca(gs)

    gs.history[gs.hisPly].move = move
    gs.history[gs.hisPly].fiftyMove = gs.fiftyMove
    gs.history[gs.hisPly].enPas = gs.enPas
    gs.history[gs.hisPly].castleRights = gs.castleRights

    # update castle rights
    gs.castleRights &= castlePerm[fromSq]
    gs.castleRights &= castlePerm[toSq]

    # enpas
    gs.enPas = no_sq

    # hash castle
    hash_ca(gs)

    # fiftymove
    gs.fiftyMove += 1

    captured = CAPTURED(move)
    if captured != piece.empty:
        clearPiece(gs,toSq)
        gs.fiftyMove = 0

    gs.ply += 1
    gs.hisPly += 1

    if piecePawn[gs.board[fromSq]]:
        gs.fiftyMove = 0
        if (move & MVFLAGPS):
            if side == white:
                gs.enPas = fromSq + 10
            elif side == black:
                gs.enPas = fromSq - 10
            # hash enpassant
            hash_ep(gs)

    movePiece(gs,fromSq, toSq)

    promoted_piece = PROMOTED(move)
    if promoted_piece != piece.empty:
        clearPiece(gs,toSq)
        addPiece(gs,toSq, promoted_piece)

    if pieceKing[gs.board[toSq]]:
        gs.kingSq[gs.side] = toSq

    gs.side ^= 1

    # hash side
    hash_side(gs)

    if sqAttacked(gs, gs.kingSq[side], gs.side):
        undoMove(gs)
        return False
    return True
def undoMove(gs):
    gs.ply-=1
    gs.hisPly-=1

    move=gs.history[gs.hisPly].move

    fromSq=FROMSQ(move)
    toSq=TOSQ(move)

    # hash enpassant
    if gs.enPas != no_sq:
        hash_ep(gs)

    # hash castle
    hash_ca(gs)

    gs.castleRights=gs.history[gs.hisPly].castleRights
    gs.enPas = gs.history[gs.hisPly].enPas
    gs.fiftyMove = gs.history[gs.hisPly].fiftyMove

    # hash enpassant
    if gs.enPas != no_sq:
        hash_ep(gs)

    # hash castle
    hash_ca(gs)

    gs.side^=1
    # hash side
    hash_side(gs)

    if (move & MVFLAGEP):
        if gs.side==white:
            addPiece(gs,toSq - 10, piece.bP)
        elif gs.side==black:
            addPiece(gs,toSq + 10, piece.wP)

    if (move & MVFLAGCA):
        if toSq==c1:
            movePiece(gs,d1, a1)
        elif toSq==g1:
            movePiece(gs,f1, h1)

        elif toSq==c8:
            movePiece(gs,d8, a8)
        elif toSq==g8:
            movePiece(gs,f8, h8)

    movePiece(gs,toSq, fromSq)

    if pieceKing[gs.board[fromSq]]:
        gs.kingSq[gs.side]=fromSq

    captured=CAPTURED(move)
    if captured != piece.empty:
        addPiece(gs,toSq, captured)

    promoted=PROMOTED(move)
    if promoted != piece.empty:
        clearPiece(gs,fromSq)
        addPiece(gs,fromSq, (piece.wP if pieceCol[promoted] == white else piece.bP))

def isLegalMove(move,gs):
    lists=movelist()
    generateAllMoves(gs,lists)

    for i in range(lists.count):
        moves=lists.moves[i].move

        if not makeMove(gs,moves):
            continue

        undoMove(gs)

        if move==moves:
            return moves

    return 0
def isZeroMoves(gs,list):

    legal=0
    for i in range(list.count):
        move=list.moves[i].move
        if not makeMove(gs,move):
            continue

        legal+=1
        undoMove(gs)
        if legal != 0:
            return False

    return True

def clearPiece(gs, sq):
    pce=gs.board[sq]
    col=pieceCol[pce]
    t_piece=-1

    if pieceBig[pce]:
        gs.bigPiece[col] -= 1
        if pieceMaj[pce]:
            gs.majPiece[col] -= 1
        else:
            gs.minPiece[col] -= 1

    hash_pce(gs,pce,sq)

    gs.board[sq]=piece.empty

    gs.material[col]-=pieceVal[pce]

    #remove from piecelist
    for index in range(gs.pieceNum[pce]):
        if(gs.pieceList[pce][index]==sq):
            t_piece=index
            break

    gs.pieceNum[pce]-=1
    gs.pieceList[pce][t_piece]=gs.pieceList[pce][gs.pieceNum[pce]]

    gs.gamePhase=get_game_phase(gs)
def movePiece(gs, fromSq, toSq):
    pce=gs.board[fromSq]

    hash_pce(gs,pce,fromSq)

    gs.board[fromSq]=piece.empty

    hash_pce(gs,pce,toSq)

    gs.board[toSq]=pce

    for index in range(gs.pieceNum[pce]):
        if(gs.pieceList[pce][index]==fromSq):
            gs.pieceList[pce][index]=toSq
            break
def addPiece(gs, sq, pce):
    col=pieceCol[pce]

    if pieceBig[pce]:
        gs.bigPiece[col] += 1
        if pieceMaj[pce]:
            gs.majPiece[col] += 1
        else:
            gs.minPiece[col] += 1

    hash_pce(gs,pce,sq)

    gs.board[sq]=pce

    gs.material[col]+=pieceVal[pce]

    gs.pieceList[pce][gs.pieceNum[pce]]=sq
    gs.pieceNum[pce]+=1
    gs.gamePhase = get_game_phase(gs)