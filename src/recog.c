
#include "defs.h"
#include "recog.h"
#include "stdio.h"
#include "bitboards.h"

int drawRepetition(const S_BOARD *pos) {
    return is_repetition(pos);
}

int drawByRepetitionEthereals(const S_BOARD *pos) {
    return is_repetition(pos);
}
