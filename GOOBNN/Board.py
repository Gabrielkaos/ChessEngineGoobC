import numpy as np
from constants import NO_SQ,EMPTY,both

class Board:
    def __init__(self):
        self.pieces=np.zeros(64,dtype=np.int)
        self.en_pas=NO_SQ
        self.castleRights=0
        self.side=0

    def reset(self):
        for i in range(64):
            self.pieces[i]=EMPTY
        self.en_pas=NO_SQ
        self.side=both
        self.castleRights=0