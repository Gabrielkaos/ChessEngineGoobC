
#ifndef SOME_MATH_H
#define SOME_MATH_H

#define MAX(A,B) (((A) > (B)) ? (A):(B))
#define MIN(A,B) (((A) > (B)) ? (B):(A))

static inline int floorPowerOf2(int n){
    if(n <= 0) return 1;
    int p = 1;
    while((p << 1) <= n) p <<= 1;
    return p;
}

#endif // SOME_MATH_H

