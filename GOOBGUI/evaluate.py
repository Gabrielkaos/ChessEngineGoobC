from variables import white,black
from Structs import Pieces

piece=Pieces()

def eval(gs):
    score=gs.material[white]-gs.material[black]

    return score

def isItEndGame(gs,my_color):
    if my_color==white:
        if gs.pieceNum[piece.bQ]==0 and gs.pieceNum[piece.bP]<=6 and gs.bigPiece[black]<=4:
            return True
        if gs.minPiece[black]==0 and gs.majPiece[black]<=1:
            return True
        if gs.minPiece[black]<=2 and gs.majPiece[black]==0:
            return True
        if gs.bigPiece[black]<=3:
            return True


    else:
        if gs.pieceNum[piece.wQ]==0 and gs.pieceNum[piece.wP]<=6 and gs.bigPiece[white]<=4:
            return True
        if gs.minPiece[white]==0 and gs.majPiece[white]<=1:
            return True
        if gs.minPiece[white]<=2 and gs.majPiece[white]==0:
            return True
        if gs.bigPiece[white]<=3:
            return True

    return False