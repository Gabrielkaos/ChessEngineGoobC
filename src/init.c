

#include "stdlib.h"
#include "stdio.h"
#include "search.h"
#include "init.h"
#include "attacks.h"

//got this from ethereals
U64 RAND_64() {

    // http://vigna.di.unimi.it/ftp/papers/xorshift.pdf

    static U64 seed = 1070372ull;

    seed ^= seed >> 12;
    seed ^= seed << 25;
    seed ^= seed >> 27;

    return seed * 2685821657736338717ull;
}



U64 pieceKeys[16][BOARD_NUMS_SQ];
U64 sideKey;
U64 castleKeys[16];
U64 cuckoo[8192];
int cuckooMove[8192];

S_OPTIONS EngineOptions[1];

static inline int cuckooH1(U64 h) { return (int)(h & 0x1fff); }
static inline int cuckooH2(U64 h) { return (int)((h >> 16) & 0x1fff); }

static void InitCuckoo() {
    static const int cuckooPieces[10] = { wN, wB, wR, wQ, wK, bN, bB, bR, bQ, bK };
    memset(cuckoo, 0, sizeof(cuckoo));
    memset(cuckooMove, 0, sizeof(cuckooMove));
    int count = 0;

    for (int p = 0; p < 10; ++p) {
        int pc = cuckooPieces[p];
        int pt = TYPE_OF(pc);
        for (int s1 = A1; s1 <= H8; ++s1) {
            U64 attacks = (pt == KNIGHT) ? knight_attacks[s1]
                        : (pt == BISHOP) ? bishop_pseudo_attacks[s1]
                        : (pt == ROOK)   ? rook_pseudo_attacks[s1]
                        : (pt == QUEEN)  ? (bishop_pseudo_attacks[s1] | rook_pseudo_attacks[s1])
                                         : king_attacks[s1];
            for (int s2 = s1 + 1; s2 <= H8; ++s2) {
                if (!(attacks & (1ULL << s2))) continue;

                int move = MOVE(s1, s2, EMPTY, EMPTY, 0);
                U64 key = pieceKeys[pc][s1] ^ pieceKeys[pc][s2] ^ sideKey;
                int i = cuckooH1(key);
                while (TRUE) {
                    U64 tmpKey = cuckoo[i];
                    cuckoo[i] = key;
                    key = tmpKey;

                    int tmpMove = cuckooMove[i];
                    cuckooMove[i] = move;
                    move = tmpMove;

                    if (move == NOMOVE) break;
                    i = (i == cuckooH1(key)) ? cuckooH2(key) : cuckooH1(key);
                }
                count++;
            }
        }
    }
    if (count != 3668) {
        fprintf(stderr, "Failed to initialize cuckoo tables (count=%d)\n", count);
        exit(1);
    }
}


void InitHashKeys(){
    int index=0;
    int index2=0;
    for (index=0;index<16;++index){
        for(index2=0;index2<BOARD_NUMS_SQ;++index2){
            pieceKeys[index][index2]=RAND_64();
        }
    }
    sideKey=RAND_64();

    for(index=0;index<16;++index){
        castleKeys[index]=RAND_64();
    }

}

void AllInit(){
    InitHashKeys();
    EngineOptions->analysisMode   = FALSE;
    EngineOptions->limitStrength  = FALSE;
    EngineOptions->uciElo         = defaultElo;
    EngineOptions->skillLevel     = defaultSkillLevel;
    EngineOptions->personality    = PERSONALITY_DEFAULT;
    EngineOptions->contempt       = 0;
    EngineOptions->aggressiveness = 100;
    EngineOptions->tradeAversion  = 0;
    //InitMvvLva();
    //InitPolyBook("Performance.bin");
    InitAttacks();
    InitCuckoo();
    initLMRTable();
}
