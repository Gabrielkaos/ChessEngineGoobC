import numpy as np

class Undo:
    def __init__(self):
        self.move=0
        self.fiftyMove=0
        self.enPas=0
        self.castleRights=0
        self.poskey=np.uint64(0)
class move:
    def __init__(self):
        # self.score=0
        self.move=0
class movelist:
    def __init__(self):
        self.moves=np.array([move() for _ in range(300)])
        self.count=0
class Pieces:
    def __init__(self):
        self.offboard=13
        self.empty=0

        self.wP=1
        self.wR=2
        self.wN=3
        self.wB=4
        self.wQ=5
        self.wK=6

        self.bP = 7
        self.bR = 8
        self.bN = 9
        self.bB = 10
        self.bQ = 11
        self.bK = 12
