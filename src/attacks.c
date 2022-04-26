#include "defs.h"
#include "string.h"

//CMK attacks.c

const U64 NOT_A_FILE=18374403900871474942ULL;
const U64 NOT_H_FILE=9187201950435737471ULL;
const U64 NOT_HG_FILE=4557430888798830399ULL;
const U64 NOT_AB_FILE=18229723555195321596ULL;

const U64 rook_magic_numbers[64] = {
    0x8a80104000800020ULL,
    0x140002000100040ULL,
    0x2801880a0017001ULL,
    0x100081001000420ULL,
    0x200020010080420ULL,
    0x3001c0002010008ULL,
    0x8480008002000100ULL,
    0x2080088004402900ULL,
    0x800098204000ULL,
    0x2024401000200040ULL,
    0x100802000801000ULL,
    0x120800800801000ULL,
    0x208808088000400ULL,
    0x2802200800400ULL,
    0x2200800100020080ULL,
    0x801000060821100ULL,
    0x80044006422000ULL,
    0x100808020004000ULL,
    0x12108a0010204200ULL,
    0x140848010000802ULL,
    0x481828014002800ULL,
    0x8094004002004100ULL,
    0x4010040010010802ULL,
    0x20008806104ULL,
    0x100400080208000ULL,
    0x2040002120081000ULL,
    0x21200680100081ULL,
    0x20100080080080ULL,
    0x2000a00200410ULL,
    0x20080800400ULL,
    0x80088400100102ULL,
    0x80004600042881ULL,
    0x4040008040800020ULL,
    0x440003000200801ULL,
    0x4200011004500ULL,
    0x188020010100100ULL,
    0x14800401802800ULL,
    0x2080040080800200ULL,
    0x124080204001001ULL,
    0x200046502000484ULL,
    0x480400080088020ULL,
    0x1000422010034000ULL,
    0x30200100110040ULL,
    0x100021010009ULL,
    0x2002080100110004ULL,
    0x202008004008002ULL,
    0x20020004010100ULL,
    0x2048440040820001ULL,
    0x101002200408200ULL,
    0x40802000401080ULL,
    0x4008142004410100ULL,
    0x2060820c0120200ULL,
    0x1001004080100ULL,
    0x20c020080040080ULL,
    0x2935610830022400ULL,
    0x44440041009200ULL,
    0x280001040802101ULL,
    0x2100190040002085ULL,
    0x80c0084100102001ULL,
    0x4024081001000421ULL,
    0x20030a0244872ULL,
    0x12001008414402ULL,
    0x2006104900a0804ULL,
    0x1004081002402ULL
};
const U64 bishop_magic_numbers[64] = {
    0x40040844404084ULL,
    0x2004208a004208ULL,
    0x10190041080202ULL,
    0x108060845042010ULL,
    0x581104180800210ULL,
    0x2112080446200010ULL,
    0x1080820820060210ULL,
    0x3c0808410220200ULL,
    0x4050404440404ULL,
    0x21001420088ULL,
    0x24d0080801082102ULL,
    0x1020a0a020400ULL,
    0x40308200402ULL,
    0x4011002100800ULL,
    0x401484104104005ULL,
    0x801010402020200ULL,
    0x400210c3880100ULL,
    0x404022024108200ULL,
    0x810018200204102ULL,
    0x4002801a02003ULL,
    0x85040820080400ULL,
    0x810102c808880400ULL,
    0xe900410884800ULL,
    0x8002020480840102ULL,
    0x220200865090201ULL,
    0x2010100a02021202ULL,
    0x152048408022401ULL,
    0x20080002081110ULL,
    0x4001001021004000ULL,
    0x800040400a011002ULL,
    0xe4004081011002ULL,
    0x1c004001012080ULL,
    0x8004200962a00220ULL,
    0x8422100208500202ULL,
    0x2000402200300c08ULL,
    0x8646020080080080ULL,
    0x80020a0200100808ULL,
    0x2010004880111000ULL,
    0x623000a080011400ULL,
    0x42008c0340209202ULL,
    0x209188240001000ULL,
    0x400408a884001800ULL,
    0x110400a6080400ULL,
    0x1840060a44020800ULL,
    0x90080104000041ULL,
    0x201011000808101ULL,
    0x1a2208080504f080ULL,
    0x8012020600211212ULL,
    0x500861011240000ULL,
    0x180806108200800ULL,
    0x4000020e01040044ULL,
    0x300000261044000aULL,
    0x802241102020002ULL,
    0x20906061210001ULL,
    0x5a84841004010310ULL,
    0x4010801011c04ULL,
    0xa010109502200ULL,
    0x4a02012000ULL,
    0x500201010098b028ULL,
    0x8040002811040900ULL,
    0x28000010020204ULL,
    0x6000020202d0240ULL,
    0x8918844842082200ULL,
    0x4010011029020020ULL
};

const int bishop_relevant_bits[64]={
    6, 5, 5, 5, 5, 5, 5, 6,
    5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 9, 9, 7, 5, 5,
    5, 5, 7, 7, 7, 7, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5,
    6, 5, 5, 5, 5, 5, 5, 6
};
const int rook_relevant_bits[64]={
12, 11, 11, 11, 11, 11, 11, 12,
11, 10, 10, 10, 10, 10, 10, 11,
11, 10, 10, 10, 10, 10, 10, 11,
11, 10, 10, 10, 10, 10, 10, 11,
11, 10, 10, 10, 10, 10, 10, 11,
11, 10, 10, 10, 10, 10, 10, 11,
11, 10, 10, 10, 10, 10, 10, 11,
12, 11, 11, 11, 11, 11, 11, 12
};

U64 bishop_attacks[64][512];
U64 rook_attacks[64][4096];
U64 bishop_masks[64];
U64 rook_masks[64];
U64 knight_attacks[64];
U64 pawn_attacks[2][64];
U64 king_attacks[64];

U64 rook_attack_on_fly(int sq,U64 block){

    //attacks bitboard
    U64 attacksbb=0ULL;

    int r,f;
    int tf=sq%8;
    int tr=sq / 8;

    //mask relevant occupancy bits
    for(r=tr+1;r<=7;r++){
        attacksbb |= (1ULL << (r*8+tf));
        if(((1ULL << (r*8+tf)) & block)) break;
    }
    for(r=tr-1;r>=0;r--){
        attacksbb |= (1ULL << (r*8+tf));
        if(((1ULL << (r*8+tf)) & block)) break;
    }
    for(f=tf+1;f<=7;f++){
        attacksbb |= (1ULL << (tr*8+f));
        if(((1ULL << (tr*8+f)) & block)) break;
    }
    for(f=tf-1;f>=0;f--){
        attacksbb |= (1ULL << (tr*8+f));
        if(((1ULL << (tr*8+f)) & block)) break;
    }

    return attacksbb;

}
U64 bishop_attack_on_fly(int sq,U64 block){

    //attacks bitboard
    U64 attacksbb=0ULL;

    int r,f;
    int tf=sq%8;
    int tr=sq / 8;

    //mask relevant occupancy bits
    for(r=tr+1,f=tf+1;r <=7 && f <=7;r++,f++){
        attacksbb |= (1ULL << (r*8+f));
        if((1ULL << (r*8+f) & block)) break;
    }
    for(r=tr-1,f=tf+1;r >=0 && f <=7;r--,f++){
        attacksbb |= (1ULL << (r*8+f));
        if((1ULL << (r*8+f) & block)) break;
    }
    for(r=tr+1,f=tf-1;r <=7 && f >=0;r++,f--){
        attacksbb |= (1ULL << (r*8+f));
        if((1ULL << (r*8+f) & block)) break;
    }
    for(r=tr-1,f=tf-1;r >=0 && f >=0;r--,f--){
        attacksbb |= (1ULL << (r*8+f));
        if((1ULL << (r*8+f) & block)) break;
    }

    return attacksbb;

}
U64 bishop_attack_mask(int sq){

    //attacks bitboard
    U64 attacksbb=0ULL;

    int r,f;
    int tf=sq%8;
    int tr=sq / 8;

    //mask relevant occupancy bits
    for(r=tr+1,f=tf+1;r <=6 && f <=6;r++,f++){
        attacksbb |= (1ULL << (r*8+f));
    }
    for(r=tr-1,f=tf+1;r >=1 && f <=6;r--,f++){
        attacksbb |= (1ULL << (r*8+f));
    }
    for(r=tr+1,f=tf-1;r <=6 && f >=1;r++,f--){
        attacksbb |= (1ULL << (r*8+f));
    }
    for(r=tr-1,f=tf-1;r >=1 && f >=1;r--,f--){
        attacksbb |= (1ULL << (r*8+f));
    }

    return attacksbb;

}
U64 rook_attack_mask(int sq){

    //attacks bitboard
    U64 attacksbb=0ULL;

    int r,f;
    int tf=sq%8;
    int tr=sq / 8;

    //mask relevant occupancy bits
    for(r=tr+1;r<=6;r++){
        attacksbb |= (1ULL << (r*8+tf));
    }
    for(r=tr-1;r>=1;r--){
        attacksbb |= (1ULL << (r*8+tf));
    }
    for(f=tf+1;f<=6;f++){
        attacksbb |= (1ULL << (tr*8+f));
    }
    for(f=tf-1;f>=1;f--){
        attacksbb |= (1ULL << (tr*8+f));
    }

    return attacksbb;

}
U64 pawn_attack_mask(int sq, int side){

    //piece bitboard
    U64 bitboard=0ULL;

    //attacks bitboard
    U64 attacksbb=0ULL;

    SETBIT(bitboard,sq);

    if(side==WHITE){
        // captures diagonal
        if((bitboard << 7) & NOT_H_FILE) attacksbb |= (bitboard << 7);
        if((bitboard << 9) & NOT_A_FILE) attacksbb |= (bitboard << 9);
    }else if (side==BLACK){
        // captures diagonal
        if((bitboard >> 9) & NOT_H_FILE) attacksbb |= (bitboard >> 9);
        if((bitboard >> 7) & NOT_A_FILE) attacksbb |= (bitboard >> 7);
    }

    return attacksbb;
}
U64 knight_attack_mask(int sq){
    //piece bitboard
    U64 bitboard=0ULL;

    //attacks bitboard
    U64 attacksbb=0ULL;

    SETBIT(bitboard,sq);


    if((bitboard>>17) & NOT_H_FILE) attacksbb |= (bitboard >>17);
    if((bitboard>>15) & NOT_A_FILE) attacksbb |= (bitboard >>15);
    if((bitboard>>10) & NOT_HG_FILE) attacksbb |= (bitboard >>10);
    if((bitboard>>6) & NOT_AB_FILE) attacksbb |= (bitboard >>6);

    if((bitboard<<17) & NOT_A_FILE) attacksbb |= (bitboard <<17);
    if((bitboard<<15) & NOT_H_FILE) attacksbb |= (bitboard <<15);
    if((bitboard<<10) & NOT_AB_FILE) attacksbb |= (bitboard <<10);
    if((bitboard<<6) & NOT_HG_FILE) attacksbb |= (bitboard <<6);

    return attacksbb;
}
U64 king_attack_mask(int sq){
    //piece bitboard
    U64 bitboard=0ULL;

    //attacks bitboard
    U64 attacksbb=0ULL;

    SETBIT(bitboard,sq);

    //dirs= 8,7,9,1

    //up and down
    attacksbb |= (bitboard >> 8);
    attacksbb |= (bitboard << 8);

    //diagonals
    if((bitboard >> 7) & NOT_A_FILE) attacksbb |= (bitboard >> 7);
    if((bitboard >> 9) & NOT_H_FILE) attacksbb |= (bitboard >> 9);
    if((bitboard << 7) & NOT_H_FILE) attacksbb |= (bitboard << 7);
    if((bitboard << 9) & NOT_A_FILE) attacksbb |= (bitboard << 9);

    //right and left
    if((bitboard >> 1) & NOT_H_FILE) attacksbb |= (bitboard >> 1);
    if((bitboard << 1) & NOT_A_FILE) attacksbb |= (bitboard << 1);

    return attacksbb;

}
U64 set_occupancy(int index,int bits_in_mask,U64 attack_mask){
    U64 occupancy=0ULL;

    //loop through the number of bits
    for(int count=0;count<bits_in_mask;count++){

        int square=LSBINDEX(attack_mask);

        POPBIT(attack_mask,square);

        if(index & (1 << count)){
            occupancy |= (1ULL<<square);
        }
    }

    return occupancy;
}

///////////////////////
//QUEEN ATTACKS GET IT
///////////////////////
U64 get_queen_attacks(int square,U64 occupancy){

    return (get_bishop_attacks(square,occupancy) | get_rook_attacks(square,occupancy));
}
///////////////////////
//BISHOP ATTACKS GET IT
///////////////////////
U64 get_bishop_attacks(int square,U64 occupancy){

    occupancy&=bishop_masks[square];
    occupancy*=bishop_magic_numbers[square];
    occupancy >>=64-bishop_relevant_bits[square];

    return bishop_attacks[square][occupancy];
}
///////////////////////
//ROOK ATTACKS GET IT
///////////////////////
U64 get_rook_attacks(int square,U64 occupancy){

    occupancy&=rook_masks[square];
    occupancy*=rook_magic_numbers[square];
    occupancy>>=64-rook_relevant_bits[square];
    return rook_attacks[square][occupancy];

}

static void initSliderPiecesAttacks(int bishop){
    for(int sq=0;sq<64;sq++){
        bishop_masks[sq]=bishop_attack_mask(sq);
        rook_masks[sq]=rook_attack_mask(sq);

        //attack mask
        U64 attack_mask=bishop ? bishop_masks[sq]:rook_masks[sq];

        //relevant bits
        int relevant_bits=COUNTBIT(attack_mask);

        //occupancy index
        int occupancy_index=(1<<relevant_bits);

        for(int index=0;index<occupancy_index;index++){
            //bishop
            if(bishop){
                U64 occupancy=set_occupancy(index,relevant_bits,attack_mask);

                //magic index
                int magic_index=(occupancy*bishop_magic_numbers[sq]) >> (64-bishop_relevant_bits[sq]);

                bishop_attacks[sq][magic_index]=bishop_attack_on_fly(sq,occupancy);
            }
            //rook
            else{
                U64 occupancy=set_occupancy(index,relevant_bits,attack_mask);

                //magic index
                int magic_index=(occupancy*rook_magic_numbers[sq]) >> (64-rook_relevant_bits[sq]);

                rook_attacks[sq][magic_index]=rook_attack_on_fly(sq,occupancy);
            }
        }

    }
}
static void initLeaperAttacks(){
    for(int sq=0;sq<64;sq++){
        king_attacks[sq]=king_attack_mask(sq);
        knight_attacks[sq]=knight_attack_mask(sq);
        pawn_attacks[WHITE][sq]=pawn_attack_mask(sq,WHITE);
        pawn_attacks[BLACK][sq]=pawn_attack_mask(sq,BLACK);
    }
}
void InitAttacks(){
    initLeaperAttacks();
    initSliderPiecesAttacks(1);
    initSliderPiecesAttacks(0);
}

//CMK
int is_square_attacked_BB(const int square,const int side,const S_BOARD *state){


    if ((side == WHITE) && (pawn_attacks[BLACK][square] & state->bitboards[wP])) return 1;
    if ((side == BLACK) && (pawn_attacks[WHITE][square] & state->bitboards[bP])) return 1;
    if (knight_attacks[square] & ((side == WHITE) ? state->bitboards[wN] : state->bitboards[bN])) return 1;
    if (get_bishop_attacks(square, state->occupancy[BOTH]) & ((side == WHITE) ? state->bitboards[wB] : state->bitboards[bB])) return 1;
    if (get_rook_attacks(square, state->occupancy[BOTH]) & ((side == WHITE) ? state->bitboards[wR] : state->bitboards[bR])) return 1;
    if (get_queen_attacks(square, state->occupancy[BOTH]) & ((side == WHITE) ? state->bitboards[wQ] : state->bitboards[bQ])) return 1;
    if (king_attacks[square] & ((side == WHITE) ? state->bitboards[wK] : state->bitboards[bK])) return 1;

    return 0;
}
