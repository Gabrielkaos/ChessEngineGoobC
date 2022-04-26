#include "defs.h"
#include "stdlib.h"
#include "stdio.h"

#define RAND_64 (   (U64) rand() |\
                    (U64) rand()<<15 |\
                    (U64) rand()<<30 |\
                    (U64) rand()<<45 |\
                    ((U64) rand() & 0xf) <<60    )

U64 SetMask[64];
U64 ClearMask[64];

U64 pieceKeys[13][BOARD_NUMS_SQ];
U64 sideKey;
U64 castleKeys[16];

int filesBoard[BOARD_NUMS_SQ];
int ranksBoard[BOARD_NUMS_SQ];


U64 FileBBMask[8];
U64 RankBBMask[8];

U64 BlackPassedMask[64];
U64 WhitePassedMark[64];
U64 IsolatedMask[64];

S_OPTIONS EngineOptions[1];


void InitEvalMasks() {

	int sq, tsq, r, f;

	for(sq = 0; sq < 8; ++sq) {
        FileBBMask[sq] = 0ULL;
		RankBBMask[sq] = 0ULL;
	}

	for(r = RANK_8; r >= RANK_1; r--) {
        for (f = FILE_A; f <= FILE_H; f++) {
            sq = r * 8 + f;
            FileBBMask[f] |= (1ULL << sq);
            RankBBMask[r] |= (1ULL << sq);
        }
	}

	for(sq = 0; sq < 64; ++sq) {
		IsolatedMask[sq] = 0ULL;
		WhitePassedMark[sq] = 0ULL;
		BlackPassedMask[sq] = 0ULL;
    }

	for(sq = 0; sq < 64; ++sq) {
		tsq = sq + 8;

        while(tsq < 64) {
            WhitePassedMark[sq] |= (1ULL << tsq);
            tsq += 8;
        }

        tsq = sq - 8;
        while(tsq >= 0) {
            BlackPassedMask[sq] |= (1ULL << tsq);
            tsq -= 8;
        }
        //filesBoard[SQ120(sq)]
        if(filesBoard[sq] > FILE_A) {
            //IsolatedMask[sq] |= FileBBMask[filesBoard[SQ120(sq)] - 1];
            IsolatedMask[sq] |= FileBBMask[filesBoard[sq] - 1];

            tsq = sq + 7;
            while(tsq < 64) {
                WhitePassedMark[sq] |= (1ULL << tsq);
                tsq += 8;
            }

            tsq = sq - 9;
            while(tsq >= 0) {
                BlackPassedMask[sq] |= (1ULL << tsq);
                tsq -= 8;
            }
        }
        //filesBoard[SQ120(sq)]
        if(filesBoard[sq] < FILE_H) {
            //IsolatedMask[sq] |= FileBBMask[filesBoard[SQ120(sq)] + 1];
            IsolatedMask[sq] |= FileBBMask[filesBoard[sq] + 1];

            tsq = sq + 9;
            while(tsq < 64) {
                WhitePassedMark[sq] |= (1ULL << tsq);
                tsq += 8;
            }

            tsq = sq - 7;
            while(tsq >= 0) {
                BlackPassedMask[sq] |= (1ULL << tsq);
                tsq -= 8;
            }
        }
	}

}

void initFilesRanksBoard(){

    int index=0;
    int file=FILE_A;
    int rank=RANK_1;
    int sq=A1;

    for(index=0;index<BOARD_NUMS_SQ;++index){
        filesBoard[index]=EMPTY;
        ranksBoard[index]=EMPTY;
    }

    for(rank=RANK_1;rank<=RANK_8;++rank){
        for(file=FILE_A;file<=FILE_H;++file){
            sq=FRtoSQ(file,rank);
            ranksBoard[sq]=rank;
            filesBoard[sq]=file;
        }
    }
}

void InitHashKeys(){
    int index=0;
    int index2=0;
    for (index=0;index<13;++index){
        for(index2=0;index2<BOARD_NUMS_SQ;++index2){
            pieceKeys[index][index2]=RAND_64;
        }
    }
    sideKey=RAND_64;

    for(index=0;index<16;++index){
        castleKeys[index]=RAND_64;
    }

}

void InitBitMasks(){
    int index=0;

    for(index=0;index<64;index++){
        SetMask[index]=0ULL;
        ClearMask[index]=0ULL;
    }
    for(index=0;index<64;index++){
        SetMask[index] |= (1ULL<<index);
        ClearMask[index] = ~SetMask[index];
    }
}

void AllInit(){
    InitBitMasks();
    InitHashKeys();
    initFilesRanksBoard();
    InitEvalMasks();
    InitMvvLva();
    InitPolyBook();
    InitAttacks();
}
