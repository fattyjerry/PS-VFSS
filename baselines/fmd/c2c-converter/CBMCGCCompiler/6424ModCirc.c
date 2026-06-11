#include <inttypes.h>

typedef struct
{ 
    uint8_t a[8];
} number;

typedef struct
{
    uint8_t b[3];
} modulus;

typedef number InputA;

typedef modulus InputB;
typedef modulus Output;
typedef unsigned __int128 uint128_t;

Output mpc_main(InputA INPUT_A, InputB INPUT_B) {
    uint128_t a1 = 0;
    for (uint8_t i = 0; i < 8; i++) {
        a1 += (INPUT_A.a[i] << (8*i));
    }
    uint128_t b1 = 0; 
    for (uint8_t i = 0; i < 3; i++) {
        b1 += (INPUT_B.b[i] << (8*i));
    }

    uint128_t res = a1 % b1; 
    Output ret;
    for (uint8_t i = 0; i < 3; i++) {
        ret.b[i] = (res >> (8*i)) & 0xff;
    }

    return ret;
}

