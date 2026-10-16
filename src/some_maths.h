
#ifndef SOME_MATH_H
#define SOME_MATH_H

#define MAX(A,B) (((A) > (B)) ? (A):(B))
#define MIN(A,B) (((A) > (B)) ? (B):(A))

static inline size_t floorPowerOf2(size_t n){
    if(n == 0) return 1;
    return 1ULL << (63 - __builtin_clzll(n));
}

#endif // SOME_MATH_H

