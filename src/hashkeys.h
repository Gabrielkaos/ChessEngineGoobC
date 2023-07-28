#ifndef HASHKEYS_H
#define HASHKEYS_H

//hashkeys.c
extern U64 GeneratePosKey(const S_BOARD *pos);
//extern U64 GeneratePawnPosKey(const S_BOARD *pos);
extern U64 GeneratePKHash(const S_BOARD *pos);

#endif // HASHKEYS_H
