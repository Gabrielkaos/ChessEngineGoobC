#include "../src/defs.h"
#include "../src/board.h"
#include "../src/makemove.h"
#include "../src/movegen.h"
#include <stdio.h>
#include <stdlib.h>

void RunTest() {
    S_BOARD pos[1];
    ParseFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", pos);
    
    S_MOVELIST list[1];
    GenerateAllMoves(pos, list);
    
    for (int i=0; i<list->count; ++i) {
        U64 beforeKey = pos->posKey;
        int move = list->moves[i].move;
        if (!makeMove(pos, move)) continue;
        takeMove(pos);
        if (beforeKey != pos->posKey) {
            printf("Bug found! Move %d corrupted posKey\n", move);
            exit(1);
        }
    }
    printf("PosKey test passed.\n");
}
