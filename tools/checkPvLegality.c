#include "../src/defs.h"
#include "../src/board.h"
#include "../src/makemove.h"
#include <stdio.h>
#include <stdlib.h>

void checkPvLegality(S_BOARD *pos, int *pvArray, int len) {
    for (int i = 0; i < len; i++) {
        if (!makeMove(pos, pvArray[i])) {
            printf("ILLEGAL PV MOVE FOUND! depth=%d move=%x ply=%d\n", len, pvArray[i], pos->ply);
            abort();
        }
    }
    for (int i = 0; i < len; i++) {
        takeMove(pos);
    }
}
