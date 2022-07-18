import chess.engine
import time

engine=chess.engine.SimpleEngine.popen_uci(r"C:/Users/LENOVO/Desktop/someEngines/ChessEngine.exe")

board=chess.Board()

result=engine.analyse(board,chess.engine.Limit(depth=4))

print(result["bestmove"])

engine.quit()