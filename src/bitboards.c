#include "stdio.h"
#include "defs.h"

//CMK
int LSBINDEX(U64 bitboard){

    if(bitboard){

        return COUNTBIT((bitboard & -bitboard)-1);

    }else {
        return -1;

    }

}
