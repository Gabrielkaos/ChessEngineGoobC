import numpy as np
from variables import no_sq,WKCA,WQCA,BKCA,BQCA
from Structs import Pieces

piece=Pieces()
set120To64=np.zeros((120),dtype=int)
set64To120 = np.zeros((64), dtype=int)


def get_game_phase(gs):
    game_phase = 24

    game_phase -= gs.pieceNum[piece.wN]
    game_phase -= gs.pieceNum[piece.bN]
    game_phase -= gs.pieceNum[piece.wB]
    game_phase -= gs.pieceNum[piece.bB]

    game_phase -= gs.pieceNum[piece.wR] * 2
    game_phase -= gs.pieceNum[piece.bR] * 2
    game_phase -= gs.pieceNum[piece.wQ] * 4
    game_phase -= gs.pieceNum[piece.bQ] * 4

    return (game_phase * 256 + 12) / 24

def FRTOSQ120(f, r):
    return ((21 + (f)) + ((r) * 10))

def get_file(sq,r):
    return -(r*8)+sq
def get_row(sq,f):
    return int((sq-f)/8)
def initSetSquares():
    sq64=0
    for i in range(64):
        set64To120[i]=120

    for i in range(120):
        set120To64[i]=65

    for r in range(8):
        for f in range(8):
            sq=FRTOSQ120(f,r)
            set64To120[sq64]=sq
            set120To64[sq]=sq64
            sq64+=1
initSetSquares()

def rand64():
    return np.random.randint(0,2**64,dtype=np.uint64)

Mirror64 = np.array([
56	,	57	,	58	,	59	,	60	,	61	,	62	,	63	,
48	,	49	,	50	,	51	,	52	,	53	,	54	,	55	,
40	,	41	,	42	,	43	,	44	,	45	,	46	,	47	,
32	,	33	,	34	,	35	,	36	,	37	,	38	,	39	,
24	,	25	,	26	,	27	,	28	,	29	,	30	,	31	,
16	,	17	,	18	,	19	,	20	,	21	,	22	,	23	,
8	,	9	,	10	,	11	,	12	,	13	,	14	,	15	,
0	,	1	,	2	,	3	,	4	,	5	,	6	,	7
])
def mirror_board(gs):
    temp_enpas=no_sq
    temp_side= gs.side ^ 1
    temp_castleRights=0
    temp_halfmove=gs.halfMove

    board=gs.board.reshape(12,10)
    board_flipped=np.flipud(board)

    #flip board
    temp_board=board_flipped.reshape(120)

    #flip enPas
    if(gs.enPas != no_sq):
        temp_enpas=Mirror64[gs.enPas]

    #flip castle rights
    if (gs.castleRights & WKCA):temp_castleRights |= BKCA
    if (gs.castleRights & WQCA):temp_castleRights |= BQCA
    if (gs.castleRights & BKCA):temp_castleRights |= WKCA
    if (gs.castleRights & BQCA):temp_castleRights |= WQCA


    gs.reset_board()

    gs.board=temp_board
    gs.castleRights=temp_castleRights
    gs.enPas=temp_enpas
    gs.side=temp_side
    gs.posKey=gs.generate_poskey()
    gs.halfMove=temp_halfmove

    gs.update_things()

def MOVE(f,t,cap,prom,flag):
    return ((f) | (t<<7) |(cap<<14) | (prom <<20) | (flag))
def FROMSQ(m):return ((m) & 0x7F)
def TOSQ(m):return (((m)>>7) & 0x7F)
def CAPTURED(m):return (((m)>>14) & 0xF)
def PROMOTED(m):return (((m)>>20) & 0xF)
