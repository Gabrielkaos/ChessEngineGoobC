import random
from os import environ
from GameState import Game_State
from subprocess import Popen, PIPE, STDOUT
import pygame
import numpy as np
from helper_functions import FRTOSQ120,set64To120,set120To64,FROMSQ,TOSQ,Mirror64,CAPTURED
from variables import moveCharRe,piecePawn,white,black,rank2,rank7,moveChar,MVFLAGCA,MVFLAGEP
from Structs import Pieces,movelist
from I_O import parseMove
from makemove import makeMove,undoMove,isZeroMoves,isLegalMove
from movegen import generateAllMoves
from attacks import sqAttacked
import pyperclip
from multiprocessing import Queue,Process


"""

                                    What is a 'Goob'?
    
            Goob is an engine written in C, can reach up to 40 million nps in perft mode
        and can search up to depth 12 in 1.5 seconds. It obeys the complete rules of Chess,
        it can only play in Standard variation. It has transposition tables, Quiescence
        Search, Iterative deepening, etc. Evaluation function is just handcrafted. It's design
        is almost the same as Vice (because I watched Bluefever Software's Youtube series on how to make
        a chess engine in c) but the difference is the board representation and move generator. He's much
        stronger in classical than bullets and blitz.
        

"""
"""

    TODOS

"""
# make GOOB run in a different thread -> can't use transposition tables
# arrows for analysis
# fix the play function to prune mate
# todo make the eval depth higher

pygame.init()
pygame.mixer.init()
environ['PYGAME_HIDE_SUPPORT_PROMPT'] = '1'

PANEL_WIDTH=600
WIDTH=512
HEIGHT=512
DIM=8
pieces=Pieces()
SQ_SIZE=HEIGHT//DIM
MAX_FPS=15
font = pygame.font.Font('freesansbold.ttf', 25)
fen_font = pygame.font.Font('freesansbold.ttf', 13)
eval_font = pygame.font.Font('freesansbold.ttf', 20)
rat_font=pygame.font.Font('freesansbold.ttf', 15)
IMAGES={}
dicts = {1: "wp", 2: "wR", 3: "wN",
          4: "wB", 5: "wQ", 6: "wK",
          7: "bp", 8: "bR", 9: "bN",
          10: "bB", 11: "bQ", 12: "bK"
          }
sound_move="sounds/moves_sound.mp3"
sound_capture="sounds/captured_sound.mp3"
sound_castle="sounds/castle_sound.mp3"
sound_start="sounds/game_start_chess.mp3.mp3"
move_SFX = pygame.mixer.Sound(sound_move)
castle_SFX = pygame.mixer.Sound(sound_castle)
capture_SFX = pygame.mixer.Sound(sound_capture)
start_SFX = pygame.mixer.Sound(sound_start)
colors=np.array([pygame.Color((160,190,160)),pygame.Color((0,100,0))])
whites=np.array([1,2,3,4,5,6])
blacks=np.array([7,8,9,10,11,12])

"""

    GOOB initialization

"""
def command(p, commands):
    p.stdin.write(f'{commands}\n')
def playGOOB(userinput, returnQueue):
    with_move = None
    # scores=[]
    GOOB = "engines/CE_QUIET.exe"
    # StockFish = "C:/Users/LENOVO/Desktop/stockfish/stockfish_14.1.exe"
    engine = Popen([GOOB], stdout=PIPE, stdin=PIPE, stderr=STDOUT, bufsize=0, text=True)

    command(engine, 'uci')
    for elines in iter(engine.stdout.readline, ''):
        eline = elines.strip()
        if 'uciok' in eline:
            break

    # position startpos moves e2e4
    line=userinput[0].rstrip()
    command(engine, line)

    # go movetime 1000
    line = userinput[1].rstrip()
    command(engine, line)
    for elines in iter(engine.stdout.readline, ''):
        eline = elines.strip()
        print(eline)
        if 'bestmove' in eline:
            with_move=eline
            break

    command(engine,"quit")
    move_str=with_move.split(" ")[1]
    returnQueue.put(move_str)
def bestMoveFinder(userinput, returnQueue):
    with_move = None
    # score_now=score_earlier=score_earlier_earlier=None
    # scores=[]
    s_f = "engines/stockfish_15.exe"
    # StockFish = "C:/Users/LENOVO/Desktop/stockfish/stockfish_14.1.exe"
    engine = Popen([s_f], stdout=PIPE, stdin=PIPE, stderr=STDOUT, bufsize=0, text=True)

    command(engine, 'uci')
    for elines in iter(engine.stdout.readline, ''):
        eline = elines.strip()
        if 'uciok' in eline:
            break

    # command(engine,'setoption name EvalFile value C:/Users/LENOVO/Desktop/stockfish_15_win_x64_avx2/nn-d0b74ce1e5eb.nnue')

    # position startpos moves e2e4
    line=userinput[0].rstrip()
    command(engine, line)

    # go movetime 1000
    line = userinput[1].rstrip()
    command(engine, line)
    for elines in iter(engine.stdout.readline, ''):
        eline = elines.strip()
        print(eline)
        if 'bestmove' in eline:
            with_move=eline
            break

    command(engine,"quit")
    move_str=with_move.split(" ")[1]
    returnQueue.put(move_str)
def evalByStockFish(inputs, max_think):
    evaluate=None

    s_f_eval = "engines/stockfish_15.exe"
    engineEval = Popen([s_f_eval], stdout=PIPE, stdin=PIPE, stderr=STDOUT, bufsize=0, text=True)
    command(engineEval, 'uci')
    # command(engineEval,'setoption name EvalFile value C:/Users/LENOVO/Desktop/stockfish_15_win_x64_avx2/nn-d0b74ce1e5eb.nnue')
    command(engineEval,inputs)
    mateFound=False
    with_move=None
    command(engineEval,f"go depth {str(max_think)}")
    for elines in iter(engineEval.stdout.readline, ''):
        eline = elines.strip()
        if 'cp' in eline:
            a=eline.split(" ")
            index=a.index("cp")+1
            evaluate=int(a[index])
        elif 'mate' in eline:
            mateFound=True
            a=eline.split(" ")
            index=a.index("mate")+1
            evaluate=int(a[index])
        if 'bestmove' in eline:
            with_move=eline.split(" ")[1]
            break
    command(engineEval,"quit")
    return evaluate,with_move,mateFound
def playVice(userinput, returnQueue):
    with_move = None
    Vice = "engines/vice.exe"
    # StockFish = "C:/Users/LENOVO/Desktop/stockfish/stockfish_14.1.exe"
    engine = Popen([Vice], stdout=PIPE, stdin=PIPE, stderr=STDOUT, bufsize=0, text=True)

    command(engine, 'uci')
    for elines in iter(engine.stdout.readline, ''):
        eline = elines.strip()
        if 'uciok' in eline:
            break

    # position startpos moves e2e4
    line=userinput[0].rstrip()
    command(engine, line)

    # go movetime 1000
    line = userinput[1].rstrip()
    command(engine, line)
    for elines in iter(engine.stdout.readline, ''):
        eline = elines.strip()
        print(eline)
        if 'bestmove' in eline:
            with_move=eline
            break

    command(engine,"quit")
    move_str=with_move.split(" ")[1]
    returnQueue.put((move_str))
def playBBC(userinput, returnQueue):
    with_move = None
    # score_now=score_earlier=score_earlier_earlier=None
    BBC = "engines/bbc_1.2_64bit_windows.exe"
    # StockFish = "C:/Users/LENOVO/Desktop/stockfish/stockfish_14.1.exe"
    engine = Popen([BBC], stdout=PIPE, stdin=PIPE, stderr=STDOUT, bufsize=0, text=True)

    command(engine, 'uci')
    for elines in iter(engine.stdout.readline, ''):
        eline = elines.strip()
        if 'uciok' in eline:
            break

    # position startpos moves e2e4
    line=userinput[0].rstrip()
    command(engine, line)

    # go movetime 1000
    line = userinput[1].rstrip()
    command(engine, line)
    for elines in iter(engine.stdout.readline, ''):
        eline = elines.strip()
        print(eline)
        if 'bestmove' in eline:
            with_move=eline
            break

    command(engine,"quit")
    move_str=with_move.split(" ")[1]
    returnQueue.put((move_str))

board2d=[]
def init2d():
    r=0
    for i in range(DIM):
        f = 0
        for j in range(DIM):
            board2d.append((r,f))
            f+=1
        r+=1
def load_images():
    pieces=np.array(["wp", "wR", "wN", "wB", "wQ", "wK","bp", "bR", "bN", "bB", "bQ", "bK"])
    for piece in pieces:
        IMAGES[piece]=pygame.transform.scale(pygame.image.load("images/"+piece+".png"),(SQ_SIZE,SQ_SIZE))
def guiMode():
    # sound
    pygame.mixer.Sound.play(start_SFX)
    screen=pygame.display.set_mode((WIDTH+PANEL_WIDTH,HEIGHT))
    logos=pygame.image.load("images/bK.png")
    pygame.display.set_icon(logos)
    pygame.display.set_caption("ChessGUI")
    clock=pygame.time.Clock()
    load_images()
    init2d()

    gs=Game_State()
    sq_Selected = None
    playerClicks = []
    sq_tuple=()

    last_move=0

    running = True
    moveMade = False
    moveUndone=False

    whiteAi=False
    blackAi=False
    """
        Engines:
            ->vice
            ->goob
            ->bbc
    """
    AiWhite="goob"
    AiBlack="bbc"
    AnalyzeMode=False

    # test position 2 race of kings
    #gs.parseFEN("8/k7/3p4/p2P1p2/P2P1P2/8/8/K7 w - - 0 1")

    # mate in 1 by white
    #gs.parseFEN("rnbqkbnr/pppp1ppp/8/4p3/2B1P3/5Q2/PPPP1PPP/RNB1K1NR w KQkq - 4 4")

    #mate in 1 by black
    #gs.parseFEN("rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2")

    # gs.parseFEN("r1bqk1nr/pp1pppbp/2n3p1/8/3NP3/2N5/PPP2PPP/R1BQKB1R w KQkq - 2 6")


    lists = movelist()
    generateAllMoves(gs, lists)
    fen=gs.parseBoardToFen()
    x=fen
    initialized_fen=x
    just_copied_fen=False
    just_copied_msg = fen_font.render("**Fen COPIED to clipboard**", True, (0, 0, 0))

    """
    
        GOOB
    
    """
    time_limit = 5
    depth_limit=63
    goob_msg = fen_font.render("Evaluator: stockfish 15", True, (0, 0, 0))
    analyzed_bm=None
    # display flag for rat
    d_flag = "simple"
    move_str=None
    moves_so_far=[]
    MAX_EVAL_THINKING_DEPTH=12

    evaluation,analyzed_bm2nd,mateFound = evalByStockFish(f"position fen {initialized_fen}", MAX_EVAL_THINKING_DEPTH)
    # print(evaluation)

    drawYellowArrow=False
    """
    
        Threading
    
    """
    AiThinking=False
    moveFinderProcess=None
    returnQueue=None

    # threading for analysis
    AAiThinking = False
    AmoveFinderProcess = None
    AreturnQueue = None

    while running:
        screen.fill((125,125,125))

        humanTurn=(True if gs.side==white and not whiteAi else False) or (True if gs.side==black and not blackAi else False)
        isGameOver, DrawFifty, DrawRep, DrawMaterial=updateGameOverThings(gs,lists)
        inCheck=sqAttacked(gs,gs.kingSq[gs.side],gs.side^1)
        in_check_msg = fen_font.render("InCheck:"+str(inCheck), True, (0, 0, 0))

##########################################################################################################
        # EVAL BAR   ****Based on Stockfish's eval****
        # eval bar is only updated after a move
##########################################################################################################
        if not isGameOver and not DrawFifty and not DrawRep and not DrawMaterial:
            draw_eval_bar(gs,screen,evaluation,mateFound)
##########################################################################################################

        #ANALYZE MODE
        if AnalyzeMode and not isGameOver and not DrawFifty and not DrawRep and not DrawMaterial and\
            (not whiteAi and not blackAi) and not analyzed_bm:

            moves_compiled=""
            for i,word in enumerate(moves_so_far):
                if i==0:
                    moves_compiled+=word
                else:
                    moves_compiled+=" "+word

            user_inputs = [f"position fen {initialized_fen} moves {moves_compiled}", f"go movetime {str(time_limit * 1000)} depth {str(depth_limit)}"]
            if not AAiThinking:
                AAiThinking=True
                AreturnQueue=Queue()
                AmoveFinderProcess=Process(target=bestMoveFinder, args=(user_inputs, AreturnQueue))
                AmoveFinderProcess.start()
            if not AmoveFinderProcess.is_alive():
                analyzed_bm = AreturnQueue.get()
                AAiThinking=False

        # AI Handling
        if not isGameOver and not DrawFifty and not DrawRep and not humanTurn and not DrawMaterial and not AnalyzeMode:
            """
            
                GOOB
            
            """
            moves_compiled = ""
            for i, word in enumerate(moves_so_far):
                if i == 0:
                    moves_compiled += word
                else:
                    moves_compiled += " " + word
            user_inputs=[f"position fen {initialized_fen} moves {moves_compiled}",f"go movetime {str(time_limit*1000)} depth {str(depth_limit)}"]
            if not AiThinking:
                AiThinking=True
                returnQueue=Queue()
                if gs.side==white:
                    if AiWhite.lower()=="bbc":
                        moveFinderProcess=Process(target=playBBC, args=(user_inputs, returnQueue))
                    elif AiWhite.lower()=="vice":
                        moveFinderProcess=Process(target=playVice, args=(user_inputs, returnQueue))
                    else:
                        moveFinderProcess=Process(target=playGOOB, args=(user_inputs, returnQueue))
                else:
                    if AiBlack.lower()=="vice":
                        moveFinderProcess=Process(target=playVice, args=(user_inputs, returnQueue))
                    elif AiBlack.lower()=="bbc":
                        moveFinderProcess=Process(target=playBBC, args=(user_inputs, returnQueue))
                    else:
                        moveFinderProcess=Process(target=playGOOB, args=(user_inputs, returnQueue))
                moveFinderProcess.start()
            if not moveFinderProcess.is_alive():
                move_str = returnQueue.get()
                movesf=parseMove(move_str,gs)
                if makeMove(gs, movesf):
                    moves_so_far.append(move_str)
                    if gs.side == white:
                        gs.halfMove += 1
                    gs.ply = 0
                    sq_Selected = None
                    sq_tuple = ()
                    playerClicks = []
                    moveMade = True
                AiThinking=False

        for event in pygame.event.get():
            if event.type==pygame.QUIT:
                running=False
            elif event.type == pygame.MOUSEBUTTONDOWN:
                if not isGameOver and not DrawFifty and not DrawRep and not DrawMaterial and not AnalyzeMode:
                    location = pygame.mouse.get_pos()
                    col = location[0] // SQ_SIZE
                    row = location[1] // SQ_SIZE

                    sq=FRTOSQ120(col,row)
                    sq=set120To64[sq]

                    if sq_Selected == sq or col >= 8:
                        sq_Selected = None
                        sq_tuple=()
                        playerClicks = []
                    else:
                        sq_Selected = sq
                        sq_tuple=(row,col)
                        playerClicks.append(sq_Selected)

                    if len(playerClicks)==2 and humanTurn:

                        move_str_from=moveCharRe[set64To120[Mirror64[playerClicks[0]]]]
                        move_str_to = moveCharRe[set64To120[Mirror64[playerClicks[1]]]]
                        sw = set64To120[Mirror64[playerClicks[0]]]

                        legalToSq=checkLegalSqTo(gs,lists,playerClicks)

                        promote=None
                        if legalToSq:
                            if piecePawn[gs.board[sw]] and gs.board[sw] in (whites if gs.side==white else blacks):
                                if gs.side==white:
                                    if sw in rank7:
                                        promote=input("Promote to: ")
                                if gs.side==black:
                                    if sw in rank2:
                                        promote = input("Promote to: ")
                        #the_move_str=""
                        if promote == None:
                            move=parseMove(move_str_from+move_str_to,gs)
                            the_move_str=move_str_from+move_str_to
                        else:
                            try:
                                move=parseMove(move_str_from+move_str_to+promote.lower(),gs)
                                the_move_str = move_str_from + move_str_to+promote.lower()
                            except:
                                move = parseMove(move_str_from + move_str_to + "q", gs)
                                the_move_str = move_str_from + move_str_to + "q"
                        move=isLegalMove(move,gs)
                        if move != 0:
                            if makeMove(gs,move):
                                moves_so_far.append(the_move_str)
                                if gs.side==white:
                                    gs.halfMove+=1
                                gs.ply=0
                                sq_Selected = None
                                sq_tuple=()
                                playerClicks = []
                                moveMade=True

                        else:
                            playerClicks = [sq_Selected]
            elif event.type==pygame.KEYDOWN:
                if event.key==pygame.K_q:running=False
                elif event.key==pygame.K_LEFT:
                    if not blackAi and not whiteAi:
                        if gs.hisPly != 0:
                            if gs.side == black:
                                if gs.halfMove != 1:
                                    gs.halfMove -= 1
                            undoMove(gs)
                            gs.ply=0
                            moveMade=True
                            AnalyzeMode=False
                            moveUndone=True
                            isGameOver=False
                            if len(moves_so_far) != 0:
                                moves_so_far.pop()
                elif event.key==pygame.K_r:
                    pygame.mixer.Sound.play(start_SFX)
                    gs=Game_State()
                    fen=gs.parseBoardToFen()
                    x=fen
                    initialized_fen=x
                    isGameOver=False
                    evaluation,analyzed_bm2nd,mateFound = evalByStockFish(f"position startpos", MAX_EVAL_THINKING_DEPTH)
                    just_copied_fen = False
                    moveMade=False
                    move_str=None
                    AnalyzeMode = False
                    moveUndone=False
                    analyzed_bm=None
                    last_move=0
                    lists = movelist()
                    d_flag="simple"
                    generateAllMoves(gs, lists)
                    moves_so_far=[]
                elif event.key == pygame.K_c:
                    pyperclip.copy(str(fen))
                    just_copied_fen = True
                elif event.key == pygame.K_a:
                    if not whiteAi and not blackAi:
                        AnalyzeMode^=1
                        analyzed_bm=None
                elif event.key == pygame.K_k:
                    if time_limit >=120:
                        time_limit+=10
                    else:
                        if time_limit<5:
                            time_limit+=1
                        else:
                            time_limit+=5
                elif event.key == pygame.K_j:
                    if time_limit<=5:
                        time_limit-=1
                    else:
                        time_limit-=5
                    if time_limit <= 0:
                        time_limit=5
                elif event.key == pygame.K_m:
                    depth_limit+=1
                    if depth_limit >=64:
                        depth_limit=63
                elif event.key == pygame.K_n:
                    depth_limit-=1
                    if depth_limit <= 0:
                        depth_limit=63

        if moveMade:
            last_move = gs.history[gs.hisPly - 1].move
            play_sound_SFXs(last_move)
            lists = movelist()
            generateAllMoves(gs, lists)
            fen=gs.parseBoardToFen()
#############################################################################################################
            # Updating eval bar
#############################################################################################################
            moves_compiled = ""
            for i, word in enumerate(moves_so_far):
                if i == 0:
                    moves_compiled += word
                else:
                    moves_compiled += " " + word
            evaluation,analyzed_bm2nd,mateFound = evalByStockFish(f"position fen {initialized_fen} moves {moves_compiled}",
                                                        MAX_EVAL_THINKING_DEPTH)
            # if gs.side==black and evaluation != None:
            #     evaluation*=-1
#############################################################################################################
            moveMade=False
            analyzed_bm=None
            d_flag="simple"
            just_copied_fen=False
        if moveUndone:
            that_move = gs.history[gs.hisPly].move
            play_sound_SFXs(that_move)
            d_flag = "simple"
            last_move=gs.history[gs.hisPly-1].move
            just_copied_fen=False
            moveUndone = False
        if just_copied_fen:screen.blit(just_copied_msg, (WIDTH + 40, HEIGHT-15))
        if whiteAi or blackAi:screen.blit(goob_msg, (WIDTH + 40, 120))
        else:
            text = fen_font.render(f"Engine for Analyze Mode Only", True, (0, 0, 0))
            screen.blit(text, (WIDTH + 40, 120))

        d_flag,AnalyzeMode=draw_almost_everything(AnalyzeMode,screen, gs, depth_limit, time_limit, MAX_EVAL_THINKING_DEPTH, sq_Selected, sq_tuple,
                               lists, fen,
                               in_check_msg, last_move, whiteAi, blackAi, analyzed_bm, analyzed_bm2nd, drawYellowArrow,
                               isGameOver, DrawFifty, DrawRep, DrawMaterial, d_flag, AAiThinking, AiThinking, move_str)

        update_gui(clock)
def update_gui(clock):
    clock.tick(MAX_FPS)
    pygame.display.flip()
def draw_almost_everything(AnalyzeMode,screen,gs,depth_limit,time_limit,MAX_EVAL_THINKING_DEPTH,sq_Selected,sq_tuple,lists,fen,
                           in_check_msg,last_move,whiteAi,blackAi,analyzed_bm,analyzed_bm2nd,drawYellowArrow,
                           isGameOver,DrawFifty,DrawRep,DrawMaterial,d_flag,AAiThinking,AiThinking,move_str):
    draw_engine_settings(screen, depth_limit, time_limit, MAX_EVAL_THINKING_DEPTH)
    drawSomeThings(screen, gs, sq_Selected, sq_tuple, lists, fen, in_check_msg, last_move)
    if analyzed_bm and not whiteAi and not blackAi:
        AnalyzeMode = draw_blue_arrow(analyzed_bm, screen)
    isItGameOver(screen, gs, isGameOver, DrawFifty, DrawRep, DrawMaterial)
    if drawYellowArrow:
        if analyzed_bm2nd and analyzed_bm is None:
            draw_yellow_arrow(screen, analyzed_bm2nd)
    isItGameOver(screen, gs, isGameOver, DrawFifty, DrawRep, DrawMaterial)
    d_flag = draw_rat_img_and_txt(screen, d_flag, whiteAi, blackAi, analyzed_bm, AAiThinking, AiThinking, move_str)
    return d_flag,AnalyzeMode
def draw_rat_img_and_txt(screen,d_flag,whiteAi,blackAi,analyzed_bm,AAiThinking,AiThinking,move_str):
    if d_flag == "simple":
        image = pygame.image.load("logo/rat2_grey.jpg")
        screen.blit(image, (PANEL_WIDTH + 20, 170))
    elif d_flag == "flex":
        image = pygame.image.load("logo/rat_flex_grey.jpg")
        screen.blit(image, (PANEL_WIDTH + 5, 175))
    elif d_flag == "think":
        image = pygame.image.load("logo/small_grey.jpg")
        screen.blit(image, (PANEL_WIDTH + 5, 175))
    pygame.draw.circle(screen, pygame.Color("black"), (PANEL_WIDTH + 300, 200), SQ_SIZE)
    pygame.draw.circle(screen, pygame.Color("black"), (PANEL_WIDTH + 200, 215), SQ_SIZE // 4)
    if AAiThinking or AiThinking:
        d_flag = "think"
        text11 = rat_font.render(f"Hmmmmm...", True,
                                 (255, 255, 255))
        screen.blit(text11, (PANEL_WIDTH + 255, 200))
    elif analyzed_bm and not AAiThinking and not whiteAi and not blackAi:
        d_flag = "flex"
        text11 = rat_font.render(f"Try {analyzed_bm}", True,
                                 (255, 255, 255))
        screen.blit(text11, (PANEL_WIDTH + 265, 200))
    elif move_str and not AiThinking:
        d_flag = "flex"
        text11 = rat_font.render(f"My Move {move_str}", True,
                                 (255, 255, 255))
        screen.blit(text11, (PANEL_WIDTH + 245, 200))
    if analyzed_bm is None and not AAiThinking and not whiteAi and not blackAi:
        text11 = rat_font.render(f"Need Help?", True,
                                 (255, 255, 255))
        screen.blit(text11, (PANEL_WIDTH + 255, 200))

    return d_flag
def draw_engine_settings(screen,depth_limit,time_limit,MAX_EVAL_THINKING_DEPTH):
    text11 = fen_font.render(f"Engine Settings -> MaxDepth:{depth_limit}, TimeLimit:{time_limit}", True, (0, 0, 0))
    screen.blit(text11, (WIDTH + 40, 100))
    text11 = fen_font.render(f"Evaluation Depth:{MAX_EVAL_THINKING_DEPTH}", True, (0, 0, 0))
    screen.blit(text11, (WIDTH + 40, 140))
def draw_yellow_arrow(screen,analyzed_bm2nd):
    try:
        x = Mirror64[set120To64[moveChar[analyzed_bm2nd[:2]]]]
        y = Mirror64[set120To64[moveChar[analyzed_bm2nd[2:4]]]]
    except:
        return
    x_pos = ((board2d[x][1] * SQ_SIZE) + SQ_SIZE // 2, (board2d[x][0] * SQ_SIZE) + SQ_SIZE // 2)
    y_pos = ((board2d[y][1] * SQ_SIZE) + SQ_SIZE // 2, (board2d[y][0] * SQ_SIZE) + SQ_SIZE // 2)
    pygame.draw.line(screen, (140, 140, 0), x_pos,
                     y_pos, 4)
    pygame.draw.circle(screen, (140, 140, 0), y_pos, SQ_SIZE // 8)
def draw_blue_arrow(analyzed_bm,screen):
    x = Mirror64[set120To64[moveChar[analyzed_bm[:2]]]]
    y = Mirror64[set120To64[moveChar[analyzed_bm[2:4]]]]
    x_pos = ((board2d[x][1] * SQ_SIZE) + SQ_SIZE // 2, (board2d[x][0] * SQ_SIZE) + SQ_SIZE // 2)
    y_pos = ((board2d[y][1] * SQ_SIZE) + SQ_SIZE // 2, (board2d[y][0] * SQ_SIZE) + SQ_SIZE // 2)
    pygame.draw.line(screen, pygame.Color("blue"), x_pos,
                     y_pos, 4)
    pygame.draw.circle(screen, pygame.Color("blue"), y_pos, SQ_SIZE // 8)
    return False
def draw_eval_bar(gs,screen,evaluation,mateFound):
    x1 = WIDTH
    x2 = WIDTH + 20
    x=evaluation
    eval_copy=x
    stronger=None
    if mateFound:
        if gs.side==white:
            if eval_copy>0:
                stronger=white
                evaluation = 20000
            else:
                stronger=black
                evaluation=-20000
        else:
            if eval_copy>0:
                stronger=black
                evaluation = -20000
            else:
                stronger=white
                evaluation=20000
    if gs.side==black and evaluation != None:
        evaluation*=-1
    evaluation/=5
    eval_bar_white = pygame.Rect((x1, 0),
                                 (x2 - x1, ((HEIGHT // 2) + evaluation) - 0))
    eval_bar_black = pygame.Rect((x1, (HEIGHT // 2) + evaluation),
                                 (x2 - x1, HEIGHT - ((HEIGHT // 2) + evaluation)))
    pygame.draw.rect(screen, (10, 10, 10), eval_bar_black)
    pygame.draw.rect(screen, (210, 210, 210), eval_bar_white)
    if not mateFound:
        if evaluation > 0:
            eval_text = eval_font.render(f"+{evaluation:.1f}", True, (0, 0, 0))
        else:
            eval_text = eval_font.render(f"{evaluation:.1f}", True, (0, 0, 0))
    else:
        if gs.side==stronger:
            eval_text = eval_font.render(f"#+{eval_copy}", True, (0, 0, 0))
        else:
            eval_text = eval_font.render(f"#{eval_copy}", True, (0, 0, 0))
    screen.blit(eval_text, (x2 + 10, HEIGHT // 2 - 10))
def play_sound_SFXs(that_move):
    if CAPTURED(that_move) != 0  or (MVFLAGEP & that_move) != 0:
        pygame.mixer.Sound.play(capture_SFX)
    elif (MVFLAGCA & that_move) != 0:
        pygame.mixer.Sound.play(castle_SFX)
    else:
        pygame.mixer.Sound.play(move_SFX)
def drawInCheckAndPhase(gs,in_check_msg, screen):
    screen.blit(in_check_msg, (WIDTH + 40, 50))
    if gs.gamePhase<43:
        message=fen_font.render("Phase: Opening Game "+str(int(gs.gamePhase)),True,(0,0,0))
    elif gs.gamePhase>=43 and gs.gamePhase<171:
        message=fen_font.render("Phase: Middle Game "+str(int(gs.gamePhase)),True,(0,0,0))
    else:
        message = fen_font.render("Phase: End Game "+str(int(gs.gamePhase)), True, (0, 0, 0))
    screen.blit(message,(WIDTH+40,65))
def drawArrowForLastMove(screen,last_move):
    if last_move != 0 and last_move != None:
        x=Mirror64[set120To64[FROMSQ(last_move)]]
        y = Mirror64[set120To64[TOSQ(last_move)]]
        x_pos = ((board2d[x][1] * SQ_SIZE) + SQ_SIZE // 2, (board2d[x][0] * SQ_SIZE) + SQ_SIZE // 2)
        y_pos = ((board2d[y][1] * SQ_SIZE) + SQ_SIZE // 2, (board2d[y][0] * SQ_SIZE) + SQ_SIZE // 2)
        pygame.draw.line(screen, pygame.Color("red"), x_pos,
                         y_pos, 4)
        pygame.draw.circle(screen, pygame.Color("red"), y_pos, SQ_SIZE // 8)
def isItGameOver(screen,gs,isGameOver,DrawFifty,DrawRep,DrawMaterial):
    if isGameOver:
        incheck = sqAttacked(gs, gs.kingSq[gs.side], gs.side ^ 1)
        if incheck:
            if gs.side == black:
                text = font.render("White won", True, (0, 0, 0))
                screen.blit(text, (WIDTH // 3, HEIGHT // 2))
            if gs.side == white:
                text = font.render("Black won", True, (0, 0, 0))
                screen.blit(text, (WIDTH // 3, HEIGHT // 2))
        else:
            text = font.render("Stalemate", True, (0, 0, 0))
            screen.blit(text, (WIDTH // 3, HEIGHT // 2))
    if DrawFifty:
        text = font.render(" Draw - Fifty move rule", True, (0, 0, 0))
        screen.blit(text, (WIDTH // 3 - 50, HEIGHT // 2))
    if DrawRep:
        text = font.render(" Draw - Threefold repetition", True, (0, 0, 0))
        screen.blit(text, (WIDTH // 3 - 50, HEIGHT // 2))
    if DrawMaterial:
        text = font.render(" Draw - Insufficient material", True, (0, 0, 0))
        screen.blit(text, (WIDTH // 3 - 50, HEIGHT // 2))
def updateGameOverThings(gs,lists):
    isGameOver = isZeroMoves(gs, lists)
    DrawFifty = gs.fiftyMove >= 100
    DrawRep = repetition(gs) >= 2
    DrawMaterial = draw_material(gs)

    return isGameOver,DrawFifty,DrawRep,DrawMaterial
def drawFen(fen,screen):
    text = fen_font.render(fen, True, (0, 0, 0))
    screen.blit(text, (WIDTH+40,30))
def highlightSquares(screen,gs,validMoves,sqSelected,sq_tuple):
    pce=gs.board[set64To120[Mirror64[sqSelected]]]
    if sqSelected != None:
        r,c=sq_tuple
        if pce in (whites if gs.side==white else blacks):
            s=pygame.Surface((SQ_SIZE,SQ_SIZE))
            s.set_alpha(110)
            s.fill(pygame.Color((255,255,0)))
            screen.blit(s,(c*SQ_SIZE,r*SQ_SIZE))
            s.fill(pygame.Color((255,0,0)))
            s.set_alpha(80)
            for i in range(validMoves.count):
                move=validMoves.moves[i].move
                sqto64=Mirror64[set120To64[TOSQ(move)]]
                move=isLegalMove(move,gs)
                if move != 0:
                    sq11=set64To120[Mirror64[sqSelected]]
                    if sq11==FROMSQ(move):
                        #pygame.draw.circle(screen,pygame.Color("grey"),(board2d[sqto64][1] * SQ_SIZE, board2d[sqto64][0] * SQ_SIZE),20)
                        screen.blit(s, (board2d[sqto64][1] * SQ_SIZE, board2d[sqto64][0] * SQ_SIZE))
def drawSomeThings(screen, gs, sqSelected, sq_tuple, validMoves, fen, in_check_msg, last_move):
    draw_Board(screen)
    highlightSquares(screen,gs,validMoves,sqSelected,sq_tuple)
    draw_Pieces(screen,gs)
    drawArrowForLastMove(screen,last_move)
    drawFen(fen,screen)
    drawInCheckAndPhase(gs,in_check_msg, screen)
def draw_Board(screen):
    for r in range(DIM):
        for c in range(DIM):
            color=colors[((r+c)%2)]
            pygame.draw.rect(screen,color,pygame.Rect(c*SQ_SIZE,r*SQ_SIZE,SQ_SIZE,SQ_SIZE))
def draw_Pieces(screen,gs):

    temp_board=gs.board
    temp_board=temp_board.reshape(12,10)
    temp_board=np.flipud(temp_board)
    temp_board=temp_board.reshape(120)

    for r in range(DIM):
        for f in range(DIM):
            sq=FRTOSQ120(f,r)

            piece=temp_board[sq]
            if piece != pieces.empty and piece != pieces.offboard:
                screen.blit(IMAGES[dicts[piece]],pygame.Rect(f*SQ_SIZE,r*SQ_SIZE,SQ_SIZE,SQ_SIZE))
def repetition(gs):

    i = 0
    r = 0

    while i<gs.hisPly:
        if gs.posKey==gs.history[i].poskey:
            r+=1
        i+=1
    return r
def draw_material(gs):
    if gs.pieceNum[pieces.wP] != 0 or gs.pieceNum[pieces.bP] != 0:
        return False
    if gs.pieceNum[pieces.wB] > 1 or gs.pieceNum[pieces.bB] > 1:
        return False
    if gs.pieceNum[pieces.wN] > 1 or gs.pieceNum[pieces.bN] > 1:
        return False
    if gs.pieceNum[pieces.wR] != 0 or gs.pieceNum[pieces.bR] != 0:
        return False
    if gs.pieceNum[pieces.wQ] != 0 or gs.pieceNum[pieces.bQ] != 0:
        return False
    if gs.pieceNum[pieces.wB] >= 1 and gs.pieceNum[pieces.wN] >= 1:
        return False
    if gs.pieceNum[pieces.bB] >= 1 and gs.pieceNum[pieces.bN] >= 1:
        return False
    return True
def checkLegalSqTo(gs,lists,playerClicks):
    legalToSq=False
    for i in range(lists.count):
        movess = lists.moves[i].move
        movess = isLegalMove(movess, gs)

        sw1 = set64To120[Mirror64[playerClicks[1]]]
        if movess != 0 and sw1 == TOSQ(movess):
            legalToSq = True
            break

    return legalToSq
def random_adversary(validMoves):
    return validMoves.moves[random.randint(0,validMoves.count-1)].move

if __name__=="__main__":
    guiMode()