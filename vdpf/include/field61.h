#ifndef VDPF_FIELD61_H
#define VDPF_FIELD61_H

#include <stdint.h>

#define VDPF_FIELD_MODULUS UINT64_C(2305843009213693951)

static inline uint64_t field61_reduce_u128(unsigned __int128 value) {
    return (uint64_t)(value % (unsigned __int128)VDPF_FIELD_MODULUS);
}

static inline uint64_t field61_add(uint64_t left, uint64_t right) {
    return field61_reduce_u128((unsigned __int128)left + right);
}

static inline uint64_t field61_sub(uint64_t left, uint64_t right) {
    return left >= right ? left - right : VDPF_FIELD_MODULUS - (right - left);
}

static inline uint64_t field61_neg(uint64_t value) {
    return value == 0 ? 0 : VDPF_FIELD_MODULUS - value;
}

static inline uint64_t field61_mul(uint64_t left, uint64_t right) {
    return field61_reduce_u128((unsigned __int128)left * right);
}

static inline uint64_t field61_from_u128(unsigned __int128 value) {
    return field61_reduce_u128(value);
}

#endif
