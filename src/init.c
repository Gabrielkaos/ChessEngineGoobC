#include "defs.h"
#include "stdlib.h"
#include "stdio.h"
#include "search.h"
#include "evaluate.h"


/*#define RAND_64 (   (U64) rand() |\
                    (U64) rand()<<15 |\
                    (U64) rand()<<30 |\
                    (U64) rand()<<45 |\
                    ((U64) rand() & 0xf) <<60    )
*/

/*U64 RAND_64(){
    return (   (U64) rand() |\
                    (U64) rand()<<15 |\
                    (U64) rand()<<30 |\
                    (U64) rand()<<45 |\
                    ((U64) rand() & 0xf) <<60    );
}*/

//got this from ethereals
U64 RAND_64() {

    // http://vigna.di.unimi.it/ftp/papers/xorshift.pdf

    static U64 seed = 1070372ull;

    seed ^= seed >> 12;
    seed ^= seed << 25;
    seed ^= seed >> 27;

    return seed * 2685821657736338717ull;
}



U64 pieceKeys[13][BOARD_NUMS_SQ];
U64 sideKey;
U64 castleKeys[16];

S_OPTIONS EngineOptions[1];


void InitHashKeys(){
    int index=0;
    int index2=0;
    for (index=0;index<13;++index){
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
    //InitMvvLva();
    //InitPolyBook("Performance.bin");
    InitAttacks();
    initLMRTable();
    initDistancesForEval();
    initPQSTMAT();
}
