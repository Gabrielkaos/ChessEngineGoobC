import time
import pandas as pd
from GameState import Game_State
from variables import black,white
from perft_test import perftTest
from I_O import prettyPrint,parseMove
from makemove import makeMove,undoMove
from evaluate import eval

"""

    Engine uses 120 element 1d array and piece list with zobrist hashing

"""

def perft_suite_epd(LIMIT_NODES=1000000):
    gs = Game_State()

    starttime=time.time()
    with open("perft_files/Perft.txt", "r") as f:
        suite=f.readlines()


    for i,pos in enumerate(suite):
        print(f"Position in line {i+1}")
        fen=pos.split(";")[0]
        gs.parseFEN(fen)

        MAX_DEPTH=6
        try:
            MAX_NODES=int((pos.split(";")[MAX_DEPTH]).split("D"+str(MAX_DEPTH)+"")[1])
        except IndexError:
            MAX_DEPTH-=1
            MAX_NODES = int((pos.split(";")[MAX_DEPTH]).split("D" + str(MAX_DEPTH) + "")[1])
        if MAX_NODES>LIMIT_NODES:
            MAX_DEPTH -=1
            MAX_NODES = int((pos.split(";")[MAX_DEPTH]).split("D"+str(MAX_DEPTH)+"")[1])
            if MAX_NODES > LIMIT_NODES:
                MAX_DEPTH -=1
                MAX_NODES = int((pos.split(";")[MAX_DEPTH]).split("D"+str(MAX_DEPTH)+"")[1])
                if MAX_NODES > LIMIT_NODES:
                    MAX_DEPTH -= 1
                    #MAX_NODES = int((pos.split(";")[MAX_DEPTH]).split("D" + str(MAX_DEPTH) + "")[1])

        start=1

        while start<=MAX_DEPTH:
            MAX_NODES = int((pos.split(";")[start]).split("D"+str(start)+"")[1])
            result_nodes=perftTest(gs,start)
            if result_nodes != MAX_NODES:
                print(f"###########################\n\n\nFAILED TEST at line {i+1}, at depth {start}\nExpected: {MAX_NODES}, instead: {result_nodes}\n\n###########################")
                minute = (time.time() - starttime) / 60.0
                print(f"Finished perft debugging, time taken {minute:.2f}mins")
                quit()
            start+=1

        print()

    minute=(time.time()-starttime)/60.0
    print(f"Finished perft debugging, time taken {minute:.2f}mins")
# last stop at 2228th line at node limit 50000
def perft_debug_csv(file="",LIMIT_NODES=1000000):
    gs = Game_State()
    suite=None
    if file != "":
        suite=pd.read_csv(file)
    else:
        print("No file detected")
        quit()

    depths=["depth 1","depth 2","depth 3","depth 4","depth 5","depth 6"]

    for i in range(len(suite)):
        if i <= 2228:
            continue
        fen=str(suite["fen"][i])
        if fen=="rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -":
            continue
        gs.parseFEN(fen)
        print(f"Position at line {i+1}")

        index_depth = 5
        max_depth_csv=depths[index_depth]

        if suite[max_depth_csv][i] >=LIMIT_NODES:
            index_depth-=1
            max_depth_csv=depths[index_depth]

            if suite[max_depth_csv][i] >= LIMIT_NODES:
                index_depth -= 1
                max_depth_csv = depths[index_depth]

                if suite[max_depth_csv][i] >= LIMIT_NODES:
                    index_depth -= 1
                    #max_depth_csv = depths[index_depth]

        start=1
        max_depth=index_depth+1
        while start<=max_depth:
            nodes=perftTest(gs,start)
            if nodes != int(suite[depths[start-1]][i]):
                print(
                    f"###########################\n\n\nFAILED TEST at line {i + 1}, at depth {start}\n\n\n###########################")
                quit()
            start+=1

        print()
def simple_perft_to_depth(depth,orig=False):
    gs=Game_State()

    perftTest(gs,depth,orig=orig)
def perft_mode():

    orig=False
    gs=Game_State()
    running=True

    print("Type help\n")

    while running:

        optionss=input("--> ")
        print()
        if optionss.lower()=="help":
            print("\nperft x - simple perft debug in current position to depth x")
            print("type algebraic notation of moves to make a move")
            print("undo - undo the move")
            print("print - print info about board")
            print("eval - evaluate current position")
            print("fen x - set board to x fen")
            print("not orig - use perft adjusted move generator")
            print("orig - use the main move generator\n")
            continue

        if len(optionss.split(" "))>1:
            if optionss.split(" ")[0].lower()=="perft":
                try:
                    prettyPrint(gs)

                    start=1
                    maxdepth=int(optionss.split(" ")[1])

                    while start<=maxdepth:
                        perftTest(gs,start,orig)
                        start+=1
                except ValueError:
                    print("-->Invalid")
                    continue
                continue

            elif optionss.split(" ")[0].lower()=="fen":
                try:
                    gs.parseFEN(optionss.split(" ")[1])
                    prettyPrint(gs)
                except ValueError:
                    print("-->Invalid")
                    continue
                continue
            else:
                continue

        if optionss.lower()=="not orig":
            orig=False
            continue
        elif optionss.lower()=="orig":
            orig=True
            continue
        elif optionss.lower() == "eval":
            print("Eval: ",eval(gs))
            continue
        elif optionss.lower()=="undo":
            if gs.hisPly != 0:
                if gs.side==black:
                    if gs.halfMove != 1:
                        gs.halfMove-=1
                undoMove(gs)
                gs.ply=0
            continue
        elif optionss.lower()=="print":
            prettyPrint(gs)
            continue
        elif optionss.lower()=="quit":
            running=False
            print("Bye\n")
            continue

        # if options not one of the above, must be a move
        try:
            move=parseMove(optionss,gs)
        except KeyError:
            print("-->Invalid")
            continue

        try:
            makeMove(gs,move)
        except IndexError:
            print("Invalid")
            continue

        # if black just moved increment half move
        if gs.side==white:
            gs.halfMove+=1
        gs.ply=0

if __name__ == "__main__":
    # com=input("p.) perft mode\ng.)gui mode\nEnter: ")
    # if com.lower() =="p":
    #     perft_mode()
    # elif com.lower() == "g":
    #     RunGui.guiMode()
    # else:
    #     print("\nJust a simple instruction")
    #     quit()
    perft_mode()
