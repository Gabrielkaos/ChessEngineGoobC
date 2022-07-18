from subprocess import Popen, PIPE, STDOUT
import tqdm

GOOB = "C:/Users/LENOVO/Desktop/someEngines/CE_QUIET.exe"
engine = Popen([GOOB], stdout=PIPE, stdin=PIPE, stderr=STDOUT, bufsize=0, text=True)

def command(p, commands):
    p.stdin.write(f'{commands}\n')

def mate_GOOB(fen, expected_ply_to_found_mate=20,mateIn=3):

    value_of_mate=0
    # result=None
    depth=0
    command(engine, 'uci')
    command(engine,f'position fen {fen}')

    command(engine,f'go mate {3}')
    for elines in iter(engine.stdout.readline, ''):
        eline = elines.strip()
        if "mate" in eline:
            index=eline.split(" ").index("mate")
            value_of_mate=int(eline.split(" ")[index+1])
            index = eline.split(" ").index("depth")
            depth = int(eline.split(" ")[index + 1])
        if value_of_mate==mateIn:
            break

    if value_of_mate==3 and depth<=expected_ply_to_found_mate:
        result=1
    else:
        result=0
    return result,depth,value_of_mate
def mate_test():

    with open("files/mate_in_3.txt",'r') as f:
        fens=f.readlines()
    i=0
    for fen in tqdm.tqdm(fens,desc="Mate in 3 Test",unit=" position"):
        fen=fen.split("acn")[0]
        result,d,m=mate_GOOB(fen)
        if result==0:
            print(f"pos at line {i+1} failed")
            print(f"mate {m}, depth {d}")
            break
        i+=1
def useGOOB():

    # value_of_mate=0
    command(engine, 'uci')
    for elines in iter(engine.stdout.readline, ''):
        eline = elines.strip()
        # print("-->",end="")
        print(eline)
        if 'uciok' in eline:
            break
    print()
    run=True
    while run:
        inputs=input(">")

        if inputs.rstrip()=="uci":
            command(engine, inputs)
            for elines in iter(engine.stdout.readline, ''):
                eline = elines.strip()
                # print("-->",end="")
                print(eline)
                if 'uciok' in eline:
                    break

        if 'position' in inputs:
            command(engine,inputs)
        if 'ucinewgame' in inputs:
            command(engine,inputs)
        if 'setoption' in inputs and 'Hash' in inputs:
            command(engine,inputs)
            for elines in iter(engine.stdout.readline, ''):
                eline = elines.strip()
                print(eline)
                if 'Cleared' in eline:
                    break

        if 'isready' in inputs:
            command(engine,inputs)
            for elines in iter(engine.stdout.readline, ''):
                eline = elines.strip()
                # print("-->",end="")
                print(eline)
                if 'readyok' in eline:
                    break

        if inputs=="d":
            command(engine,'d')
            for elines in iter(engine.stdout.readline, ''):
                eline = elines.strip()
                # print("-->",end="")
                print(eline)
                if 'Key' in eline:
                    break

        if 'go' in inputs:
            command(engine,inputs)
            for elines in iter(engine.stdout.readline, ''):
                eline = elines.strip()
                # print("-->", end="")
                print(eline)
                # if "mate" in eline:
                #     index=eline.split(" ").index("mate")
                #     value_of_mate=int(eline.split(" ")[index+1])
                #     break
                if 'bestmove' in eline:
                    break

        if 'quit' in inputs:
            command(engine,'quit')
            run=False
def uci():
    useGOOB()

if __name__=="__main__":
    mate_test()
    command(engine, 'quit')