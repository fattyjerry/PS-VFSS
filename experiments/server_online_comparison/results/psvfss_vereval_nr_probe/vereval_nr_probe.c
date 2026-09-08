#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dpf.h"
#include "mmo.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

static int run_case(size_t nr, int trial, int is_warmup) {
    const int domain_bits = 64;
    const uint64_t target = UINT64_C(0x123456789abcdef0);
    const size_t key_size = (size_t)(18 * domain_bits + 18) + 16 + 16 * MMO_HASH_OUT_1;
    uint8_t prf_key[16] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
                           0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01};
    uint8_t hash_key1[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
                             0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
    uint8_t hash_key2[16] = {0xa5, 0x5a, 0xa5, 0x5a, 0xa5, 0x5a, 0xa5, 0x5a,
                             0x3c, 0xc3, 0x3c, 0xc3, 0x3c, 0xc3, 0x3c, 0xc3};

    EVP_CIPHER_CTX *ctx = getDPFContext(prf_key);
    struct Hash *generation_hash = initMMOHash(hash_key1, MMO_HASH_OUT_1);
    unsigned char *key0 = malloc(key_size);
    unsigned char *key1 = malloc(key_size);
    uint64_t *inputs = malloc(nr * sizeof(*inputs));
    uint8_t *out0 = malloc(nr * sizeof(uint128_t));
    uint8_t *out1 = malloc(nr * sizeof(uint128_t));
    if (!ctx || !generation_hash || !key0 || !key1 || !inputs || !out0 || !out1) {
        fprintf(stderr, "allocation/setup failure for Nr=%zu\n", nr);
        return 2;
    }

    genVDPF(ctx, generation_hash, domain_bits, target, 1, key0, key1);
    destroyMMOHash(generation_hash);
    uint64_t rng = UINT64_C(0x6a09e667f3bcc909) ^ (uint64_t)nr;
    inputs[0] = target;
    for (size_t i = 1; i < nr; ++i) {
        do {
            inputs[i] = splitmix64(&rng);
        } while (inputs[i] == target);
    }

    uint8_t proof0[32] = {0};
    uint8_t proof1[32] = {0};
    struct Hash *h10 = initMMOHash(hash_key1, MMO_HASH_OUT_1);
    struct Hash *h20 = initMMOHash(hash_key2, MMO_HASH_OUT_2);
    double start0 = now_ms();
    batchEvalVDPF(ctx, h10, h20, domain_bits, false, key0, inputs, nr, out0, proof0);
    double server0_ms = now_ms() - start0;
    destroyMMOHash(h10);
    destroyMMOHash(h20);

    struct Hash *h11 = initMMOHash(hash_key1, MMO_HASH_OUT_1);
    struct Hash *h21 = initMMOHash(hash_key2, MMO_HASH_OUT_2);
    double start1 = now_ms();
    batchEvalVDPF(ctx, h11, h21, domain_bits, true, key1, inputs, nr, out1, proof1);
    double server1_ms = now_ms() - start1;
    destroyMMOHash(h11);
    destroyMMOHash(h21);

    double proof_start = now_ms();
    int proof_ok = memcmp(proof0, proof1, sizeof(proof0)) == 0;
    double proof_compare_ms = now_ms() - proof_start;
    int correctness_ok = proof_ok;
    for (size_t i = 0; i < nr && correctness_ok; ++i) {
        uint64_t share0 = 0;
        uint64_t share1 = 0;
        memcpy(&share0, out0 + i * sizeof(uint128_t), sizeof(share0));
        memcpy(&share1, out1 + i * sizeof(uint128_t), sizeof(share1));
        uint64_t opened = field61_add(share0, share1);
        uint64_t expected = i == 0 ? 1 : 0;
        if (opened != expected) correctness_ok = 0;
    }

    double critical_ms = server0_ms > server1_ms ? server0_ms : server1_ms;
    printf("%zu,%d,%s,%.6f,%.6f,%.6f,%.6f,%.6f,%s,%s\n",
           nr, trial, is_warmup ? "true" : "false", server0_ms, server1_ms,
           critical_ms, server0_ms + server1_ms, proof_compare_ms,
           proof_ok ? "true" : "false", correctness_ok ? "true" : "false");

    free(key0);
    free(key1);
    free(inputs);
    free(out0);
    free(out1);
    destroyContext(ctx);
    return correctness_ok ? 0 : 3;
}

int main(void) {
    const size_t nr_values[] = {10000, 100000};
    puts("Nr,trial,is_warmup,server0_ms,server1_ms,critical_path_ms,server_cpu_sum_ms,proof_compare_ms,proof_ok,correctness_ok");
    for (size_t n = 0; n < sizeof(nr_values) / sizeof(nr_values[0]); ++n) {
        if (run_case(nr_values[n], 0, 1) != 0) return 1;
        for (int trial = 0; trial < 5; ++trial) {
            if (run_case(nr_values[n], trial, 0) != 0) return 1;
        }
    }
    return 0;
}
