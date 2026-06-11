#include <inttypes.h>

typedef struct
{ 
    uint8_t a[6];
} number;

typedef struct
{
    uint8_t b;
} modulus;

typedef number InputA;

typedef modulus InputB;
typedef uint64_t numberLarge;
typedef numberLarge Output;

Output mpc_main(InputA INPUT_A, InputB INPUT_B) {
    uint64_t a1 = 0;
    for (uint8_t i = 0; i < 6; i++) {
        a1 += (INPUT_A.a[i] << (8*i));
    }
    uint64_t b = INPUT_B.b;
    
    return a1 % b;
}

