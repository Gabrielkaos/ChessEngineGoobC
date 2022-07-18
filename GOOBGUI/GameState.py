from variables import black,white,both
from variables import WKCA,WQCA,BKCA,BQCA,moveChar,moveCharRe
from variables import pieceVal,pieceCol,no_sq,maxGameMoves,pieceMaj,pieceBig
import numpy as np
from helper_functions import set64To120,rand64,get_game_phase
from Structs import Undo,Pieces
import io

piece=Pieces()

class Game_State:
    def __init__(self):
        self.side = None
        self.fiftyMove = 0
        self.halfMove=1
        self.castleRights=0
        self.posKey=np.uint64(0)
        self.enPas=no_sq

        self.gamePhase=0
        self.kingSq = np.zeros((2),dtype=int)
        self.pieceList = np.zeros((13,10),dtype=int)
        self.pieceNum=np.zeros((13),dtype=int)
        self.material=np.zeros((2),dtype=int)
        self.board=np.zeros((120),dtype=int)
        self.minPiece=np.zeros((2),dtype=int)
        self.majPiece = np.zeros((2), dtype=int)
        self.bigPiece = np.zeros((2), dtype=int)

        self.ply=0
        self.hisPly=0

        self.sideKey=np.uint64(0)
        self.pieceKeys = np.zeros((13, 120), dtype=np.uint64)
        self.castleKeys=np.zeros((16),dtype=np.uint64)

        self.history=np.array([Undo() for _ in range(maxGameMoves)])
        self.initHashKeys()
        self.parseFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")
    def parseBoardToFen(self):
        # Use StringIO to build string more efficiently than concatenating
        pieceChar={1:"P",2:"R",3:"N",4:"B",5:"Q",6:"K",7:"p",8:"r",9:"n",10:"b",11:"q",12:"k"}
        board = self.board.reshape(12, 10)
        board = np.flipud(board)
        with io.StringIO() as s:
            for row in board:
                empty = 0
                for cell in row:
                    c = cell
                    if c == piece.offboard:
                        continue
                    if c in [1,2,3,4,5,6,7,8,9,10,11,12]:
                        if empty > 0:
                            s.write(str(empty))
                            empty = 0
                        s.write(pieceChar[c])
                    else:
                        empty += 1
                if empty > 0:
                    s.write(str(empty))
                s.write('/')
            # Move one position back to overwrite last '/'
            s.seek(s.tell() - 3)
            # s.seek(s.tell() - 2)
            # If you do not have the additional information choose what to put
            sidechar = {0: "w", 1: "b"}
            s.write(" "+sidechar[self.side])

            if self.castleRights & WKCA:
                a = "K"
            else:
                a = ""
            if self.castleRights & WQCA:
                b = "Q"
            else:
                b = ""
            if self.castleRights & BKCA:
                c = "k"
            else:
                c = ""
            if self.castleRights & BQCA:
                d = "q"
            else:
                d = ""
            if a =="" and b =="" and c =="" and d =="":
                s.write(" -")
            else:
                s.write(" " + (a + b + c + d))

            if self.enPas == no_sq:
                a = "-"
            else:
                a = moveCharRe[self.enPas]
            s.write(" " + a)
            s.write(" "+str(self.fiftyMove))
            result=s.getvalue()
            result=result[2:]
            result+=" "+str(self.halfMove)
            return result
    def parseFEN(self,fen):

        self.reset_board()

        board=list('         \n' * 2 + ' ' + ''.join([
            '.' * int(c) if c.isdigit() else c
            for c in fen.split()[0].replace('/', '\n ')
        ]) + '\n' + '         \n' * 2)

        board=np.array(board)
        board = board.reshape(12, 10)
        board = np.flipud(board)
        board=board.reshape(120)


        for i,pce in enumerate(board):
            if pce==' ' or pce=='\n':
                self.board[i]=piece.offboard
            if pce==".":
                self.board[i]=piece.empty
            if pce=="p":
                self.board[i]=piece.bP
            if pce=="P":
                self.board[i]=piece.wP
            if pce=="Q":
                self.board[i]=piece.wQ
            if pce=="q":
                self.board[i]=piece.bQ

            if pce=="r":
                self.board[i]=piece.bR
            if pce=="R":
                self.board[i]=piece.wR

            if pce=="n":
                self.board[i]=piece.bN
            if pce=="N":
                self.board[i]=piece.wN

            if pce=="b":
                self.board[i]=piece.bB
            if pce=="B":
                self.board[i]=piece.wB

            if pce=="k":
                self.board[i]=piece.bK
            if pce=="K":
                self.board[i]=piece.wK


        if fen.split()[1]=="w":
            self.side=white
        else:
            self.side=black


        if len(fen.split()[2])==0 and fen.split()[2]=="-":
            pass
        else:
            for i in fen.split()[2]:
                if i=="K":
                    self.castleRights |= WKCA
                if i== "Q":
                    self.castleRights |= WQCA
                if i=="k":
                    self.castleRights |= BKCA
                if i=="q":
                    self.castleRights |= BQCA

        s=fen.split()[3]
        if s != "-":
            self.enPas=moveChar[s]
        else:
            self.enPas=no_sq

        try:
            s=fen.split()[4]
            if s != "-":
                self.fiftyMove=int(s)

            s=fen.split()[5]
            self.halfMove=int(s)
        except IndexError:
            self.fiftyMove=0
            self.halfMove=1

        self.posKey=self.generate_poskey()
        self.update_things()
    def reset_board(self):
        ################################
        #RESET THE BOARD
        for i in range(120):
            self.board[i]=piece.offboard

        for i in range(64):
            self.board[set64To120[i]]=piece.empty

        for i in range(2):
            self.material[i]=0
            self.minPiece[i]=0
            self.majPiece[i]=0
            self.bigPiece[i]=0

        for i in range(13):
            self.pieceNum[i]=0

        self.kingSq[white]=self.kingSq[black]= no_sq

        self.side=both
        self.enPas=no_sq
        self.fiftyMove=0
        self.castleRights=0

        self.ply=0
        self.hisPly=0
        self.halfMove = 1

        self.posKey=np.uint64(0)
    def generate_poskey(self):
        poskey=np.uint64(0)
        for i in range(13):
            for j in range(self.pieceNum[i]):
                poskey ^=self.pieceKeys[i][self.pieceList[i][j]]

        poskey ^=self.castleKeys[self.castleRights]

        if self.side==white:
            poskey ^= self.sideKey

        #enpassant key
        if self.enPas != no_sq:
            poskey ^= self.pieceKeys[piece.empty][self.enPas]

        return poskey
    def initHashKeys(self):
        for i in range(13):
            for j in range(120):
                self.pieceKeys[i][j]=rand64()

        self.sideKey=rand64()

        for i in range(16):
            self.castleKeys[i]=rand64()
    def update_things(self):
        for sq in range(len(self.board)):
            pce=self.board[sq]
            if pce != piece.offboard and pce != piece.empty:
                color=pieceCol[pce]

                if pieceBig[pce]:
                    self.bigPiece[color]+=1
                    if pieceMaj[pce]:
                        self.majPiece[color]+=1
                    else:
                        self.minPiece[color]+=1

                # update material
                self.material[color] += pieceVal[pce]

                # pieceList
                self.pieceList[pce][self.pieceNum[pce]] = sq
                # pieceNum
                self.pieceNum[pce] += 1

                #update the kings location
                if pce==piece.wK:
                    self.kingSq[white]=sq
                elif pce==piece.bK:
                    self.kingSq[black]=sq
        self.gamePhase=get_game_phase(self)
