#include <inttypes.h>

typedef struct
{ 
    uint32_t a[4];
} number;

typedef struct
{
    uint32_t b;
} modulus;

typedef number InputA;
typedef modulus InputB;

typedef unsigned __int128 uint128_t;

int mpc_main(InputA INPUT_A, InputB INPUT_B) {
    uint128_t a2 = INPUT_A.a[1];
    uint128_t a3 = INPUT_A.a[2];
    uint128_t a4 = INPUT_A.a[3];
    uint128_t allA = INPUT_A.a[0] + (a2 << 32) + (a3 << 64) + (a4 << 96);
    uint128_t allB = INPUT_B.b;
    
    return allA % allB;
}

