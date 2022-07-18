from constants import *

def getPosKey(board):

    finalKey=np.uint64(0)

    for i in range(64):
        pce=board.pieces[i]
        if pce!= EMPTY and pce != NO_SQ:
            finalKey^=pieceKeys[pce][i]

    if board.side==white:
        finalKey^=sideKeys

    if board.en_pas != NO_SQ:
        finalKey^=pieceKeys[EMPTY][board.en_pas]

    finalKey^=castleKeys[board.castleRights]

    return finalKey
def getPosKeyFromFen(board,fen):

    board.reset()

    fenboard = list('         \n' * 2 + ' ' + ''.join([
        '.' * int(c) if c.isdigit() else c
        for c in fen.split()[0].replace('/', '\n ')
    ]) + '\n' + '         \n' * 2)

    fenboard = np.array(fenboard)
    fenboard = np.resize(fenboard, (12, 10))
    fenboard = np.flipud(fenboard)
    fenboard = fenboard.reshape(120)

    for i, x in enumerate(fenboard):
        i=sq120_to_sq64[i]
        if x == ' ':continue
        if x == '.':
            board.pieces[i] = EMPTY
        if x == 'p':
            board.pieces[i] = bP
        if x == 'P':
            board.pieces[i] = wP
        if x == 'r':
            board.pieces[i] = bR
        if x == 'R':
            board.pieces[i] = wR
        if x == 'n':
            board.pieces[i] = bN
        if x == 'N':
            board.pieces[i] = wN
        if x == 'b':
            board.pieces[i] = bB
        if x == 'B':
            board.pieces[i] = wB
        if x == 'q':
            board.pieces[i] = bQ
        if x == 'Q':
            board.pieces[i] = wQ
        if x == 'k':
            board.pieces[i] = bK
        if x == 'K':
            board.pieces[i] = wK

    # side
    string_side = fen.split(" ")[1]
    if string_side == "w":
        board.side = white
    else:
        board.side = black

    # castle rights
    string_ca = fen.split(" ")[2]
    if 'K' in string_ca: board.castleRights |= WKCA
    if 'Q' in string_ca: board.castleRights |= WQCA
    if 'k' in string_ca: board.castleRights |= BKCA
    if 'q' in string_ca: board.castleRights |= BQCA

    # enpassant
    string_ep = fen.split(" ")[3]
    board.en_pas = str_to_sq[string_ep]

    return getPosKey(board)

