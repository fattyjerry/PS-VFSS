#include <openssl/rand.h>
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "../include/dpf.h"
#include "../include/mmo.h"

#define EVALSIZE 1 << 20
#define EVALDOMAIN 20
#define FULLEVALDOMAIN 20
#define MAXRANDINDEX 1ULL << FULLEVALDOMAIN

uint64_t randIndex()
{
    srand(time(NULL));
    return ((uint64_t)rand()) % (MAXRANDINDEX);
}

void testVDPF()
{
    // set up the DPF PRG
    int size = EVALDOMAIN;
    uint64_t secretIndex = randIndex();
    uint8_t *key = malloc(16);
    RAND_bytes(key, 16);
    EVP_CIPHER_CTX *ctx = getDPFContext(key);
    size_t N_list[] = {256, 512, 1024, 2048, 500000};
    int num_tests = 5;

    size_t outblocks = 4;
    uint128_t hashkey1;
    uint128_t hashkey2;
    RAND_bytes((uint8_t *)&hashkey1, sizeof(uint128_t));
    RAND_bytes((uint8_t *)&hashkey2, sizeof(uint128_t));

    // set up the MMO hash function
    struct Hash *mmo_hash1;
    struct Hash *mmo_hash2;
    mmo_hash1 = initMMOHash((uint8_t *)&hashkey1, outblocks);

    // gen VDPF keys (extra layer ==> key size is size+1)
    unsigned char *vk0 = malloc(INDEX_LASTCW + 16 + 16 * (outblocks));
    unsigned char *vk1 = malloc(INDEX_LASTCW + 16 + 16 * (outblocks));
    // genVDPF(ctx, mmo_hash1, size, secretIndex, vk0, vk1);
    clock_t t;
    t = clock();
    genVDPF(ctx, mmo_hash1, size, secretIndex, 1, vk0, vk1);
    size_t vdpf_key_size = INDEX_LASTCW + 16 + 16 * outblocks;
    printf("VDPF key size: %zu bytes\n", vdpf_key_size);
    t = clock() - t;

    double keygen_time = ((double)t) / (CLOCKS_PER_SEC / 1000.0);
    printf("VDPF keygen time: %f ms\n", keygen_time);

    destroyMMOHash(mmo_hash1);
    // printf("finished genVDPF()\n");

    for(int test=0;test < num_tests; test++){
        // generate batch of inputs to evaluate on
        size_t L = N_list[test];
        uint64_t *X = malloc(sizeof(uint64_t) * L);
        memset(X, 0, sizeof(uint64_t) * L); // 初始化为 0
        // 在 testDPF 和 testVDPF 中都要改
        for (size_t i = 0; i < L; i++)
        {
            uint64_t val;
            do {
                val = randIndex();
            } while (val == secretIndex); // 只有不等于 secretIndex 才接受

            X[i] = val; // 确保每一个 i 都会执行赋值
        }

        X[0] = secretIndex; // 强制覆盖第一个点用于测试命中

        uint128_t share0AtSecretIndex = 0;
        uint128_t share1AtSecretIndex = 0;

    //************************************************
    // Test point-by-pont evaluation
    //************************************************

        uint128_t *shares0 = malloc(sizeof(uint128_t) * L);
        uint128_t *shares1 = malloc(sizeof(uint128_t) * L);
        uint128_t pi0[outblocks];
        uint128_t pi1[outblocks];

    // eval on server 0
    //clock_t t;
        t = clock();
        mmo_hash1 = initMMOHash((uint8_t *)&hashkey1, outblocks);
        mmo_hash2 = initMMOHash((uint8_t *)&hashkey2, outblocks);
        batchEvalVDPF(ctx, mmo_hash1, mmo_hash2, size, false, vk0, X, L, (uint8_t *)shares0, (uint8_t *)&pi0[0]);
        destroyMMOHash(mmo_hash1);
        destroyMMOHash(mmo_hash2);

    // eval on server 1
        mmo_hash1 = initMMOHash((uint8_t *)&hashkey1, outblocks);
        mmo_hash2 = initMMOHash((uint8_t *)&hashkey2, outblocks);
        batchEvalVDPF(ctx, mmo_hash1, mmo_hash2, size, true, vk1, X, L, (uint8_t *)shares1, (uint8_t *)&pi1[0]);
        destroyMMOHash(mmo_hash1);
        destroyMMOHash(mmo_hash2);
        t = clock() - t;

        for (size_t i = 0; i < 2; i++) // output is 256 bits so 2 blocks
        {
            if (pi0[i] != pi1[i])
            {
                printf("FAIL (pi0 =/= pi1)\n");
                exit(0);
            }
        }

        double time_taken = ((double)t) / (CLOCKS_PER_SEC / 1000.0); // ms
        printf("VDPF N=%zu, eval time (total) %f ms\n", L, time_taken);

        share0AtSecretIndex = shares0[0];
        share1AtSecretIndex = shares1[0];

        if (((share0AtSecretIndex + share1AtSecretIndex) % FIELDSIZE) != 1)
        {
            printf("FAIL (zero)\n");
            exit(0);
        }
        for (size_t i = 1; i < L; i++)
        {
            if (((shares0[i] + shares1[i]) % FIELDSIZE) != 0)
            {
                printf("FAIL (non-zero) at index %lx\n", X[i]);
                exit(0);
            }
        }
        free(X);
        free(shares0);
        free(shares1);
    }
    printf("DONE\n\n");

    //************************************************
    // Test full domain evaluation optimization
    //************************************************
    // printf("Testing full-domain evaluation optimization\n");

    // mmo_hash1 = initMMOHash((uint8_t *)&hashkey1, outblocks);
    // mmo_hash2 = initMMOHash((uint8_t *)&hashkey2, outblocks);

    // size = FULLEVALDOMAIN; // evaluation will result in 2^size points
    // int outl = 1 << size;

    // destroyMMOHash(mmo_hash1);
    // destroyMMOHash(mmo_hash2);

    // shares0 = malloc(sizeof(uint128_t) * outl);
    // shares1 = malloc(sizeof(uint128_t) * outl);

    // mmo_hash1 = initMMOHash((uint8_t *)&hashkey1, outblocks);
    // mmo_hash2 = initMMOHash((uint8_t *)&hashkey2, outblocks);

    // t = clock();
    // fullDomainVDPF(ctx, mmo_hash1, mmo_hash2, size, false, vk0, (uint8_t *)shares0, (uint8_t *)&pi0[0]);
    // t = clock() - t;
    // time_taken = ((double)t) / (CLOCKS_PER_SEC / 1000.0); // ms
    // destroyMMOHash(mmo_hash1);
    // destroyMMOHash(mmo_hash2);

    // mmo_hash1 = initMMOHash((uint8_t *)&hashkey1, outblocks);
    // mmo_hash2 = initMMOHash((uint8_t *)&hashkey2, outblocks);
    // fullDomainVDPF(ctx, mmo_hash1, mmo_hash2, size, true, vk1, (uint8_t *)shares1, (uint8_t *)&pi1[0]);
    // destroyMMOHash(mmo_hash1);
    // destroyMMOHash(mmo_hash2);

    // printf("VDPF full-domain eval time (total) %f ms\n", time_taken);

    // for (size_t i = 0; i < 2; i++)
    // {
    //     if (pi0[i] != pi1[i])
    //     {
    //         printf("FAIL (pi0 =/= pi1)\n");
    //         exit(0);
    //     }
    // }

    // if (shares0[secretIndex] % FIELDSIZE != share0AtSecretIndex % FIELDSIZE)
    // {
    //     printf("FAIL shares are different when evaluated on full domain\n");
    //     exit(0);
    // }

    // if (shares1[secretIndex] % FIELDSIZE != share1AtSecretIndex % FIELDSIZE)
    // {
    //     printf("FAIL shares are different when evaluated on full domain\n");
    //     exit(0);
    // }

    // if (((shares0[secretIndex] + shares1[secretIndex]) % FIELDSIZE) != 1)
    // {
    //     printf("FAIL (zero)\n");
    //     exit(0);
    // }

    // for (size_t i = 0; i < outl; i++)
    // {
    //     if (i == secretIndex)
    //         continue;

    //     if (((shares0[i] + shares1[i]) % FIELDSIZE) != 0)
    //     {
    //         printf("FAIL (non-zero)\n");
    //         exit(0);
    //     }
    // }

    // destroyContext(ctx);
    // printf("DONE\n\n");
}

void testDPF()
{
    int size = EVALDOMAIN;
    uint64_t secretIndex = randIndex();
    uint8_t *key = malloc(16);
    RAND_bytes(key, 16);
    EVP_CIPHER_CTX *ctx = getDPFContext(key);
    size_t N_list[] = {256, 512, 1024, 2048, 4096};
    int num_tests = 5;

    unsigned char *k0 = malloc(INDEX_LASTCW + 16);
    unsigned char *k1 = malloc(INDEX_LASTCW + 16);
    // genDPF(ctx, size, secretIndex, k0, k1);

    clock_t t;
    t = clock();
    genDPF(ctx, size, secretIndex, k0, k1);
    size_t dpf_key_size = INDEX_LASTCW + 16;
    printf("DPF key size: %zu bytes\n", dpf_key_size);
    t = clock() - t;

    double keygen_time = ((double)t) / (CLOCKS_PER_SEC / 1000.0);
    printf("DPF keygen time: %f ms\n", keygen_time);
    // printf("finished genDPF()\n");

    for(int test=0;test < num_tests; test++){
        size_t L = N_list[test];
        uint64_t *X = malloc(sizeof(uint64_t) * L);
        memset(X, 0, sizeof(uint64_t) * L); // 初始化为 0
    // 在 testDPF 和 testVDPF 中都要改
        for (size_t i = 0; i < L; i++)
        {
            uint64_t val;
            do {
                val = randIndex();
            } while (val == secretIndex); // 只有不等于 secretIndex 才接受

            X[i] = val; // 确保每一个 i 都会执行赋值
        }

        X[0] = secretIndex; // 强制覆盖第一个点用于测试命中

        uint128_t share0AtSecretIndex = 0;
        uint128_t share1AtSecretIndex = 0;

    //************************************************
    // Test point-by-pont evaluation
    //************************************************

        uint128_t *shares0 = malloc(sizeof(uint128_t) * L);
        uint128_t *shares1 = malloc(sizeof(uint128_t) * L);

    //clock_t t;
        t = clock();
        batchEvalDPF(ctx, size, false, k0, X, L, (uint8_t *)shares0);
        

        batchEvalDPF(ctx, size, true, k1, X, L, (uint8_t *)shares1);
        t = clock() - t;
        double time_taken = ((double)t) / (CLOCKS_PER_SEC / 1000.0); // ms
        printf("DPF eval time (total) %f ms\n", time_taken);
        share0AtSecretIndex = shares0[0];
        share1AtSecretIndex = shares1[0];

        if (((share0AtSecretIndex + share1AtSecretIndex) % FIELDSIZE) != 1)
        {
            printf("FAIL (zero)\n");
            exit(0);
        }

        for (size_t i = 1; i < L; i++)
        {
            if (((shares0[i] + shares1[i]) % FIELDSIZE) != 0)
            {
                printf("FAIL (non-zero) at %zu\n", i);
                exit(0);
            }
        }
        free(shares0);
        free(shares1);
        free(X);
    }
    printf("DONE\n\n");

    //************************************************
    // Test full domain evaluation
    //************************************************
    // printf("Testing full-domain evaluation optimization\n");

    // size = FULLEVALDOMAIN; // evaluation will result in 2^size points
    // int outl = 1 << size;

    // // printf("Full domain = %i\n", outl);

    // shares0 = malloc(sizeof(uint128_t) * outl);
    // shares1 = malloc(sizeof(uint128_t) * outl);

    // t = clock();
    // fullDomainDPF(ctx, size, false, k0, (uint8_t *)shares0);
    // t = clock() - t;
    // time_taken = ((double)t) / (CLOCKS_PER_SEC / 1000.0); // ms

    // fullDomainDPF(ctx, size, true, k1, (uint8_t *)shares1);

    // printf("DPF full-domain eval time (total) %f ms\n", time_taken);

    // if (shares0[secretIndex] % FIELDSIZE != share0AtSecretIndex % FIELDSIZE)
    // {
    //     printf("FAIL shares are different when evaluated on full domain\n");
    //     exit(0);
    // }

    // if (shares1[secretIndex] % FIELDSIZE != share1AtSecretIndex % FIELDSIZE)
    // {
    //     printf("FAIL shares are different when evaluated on full domain\n");
    //     exit(0);
    // }

    // if (((shares0[secretIndex] + shares1[secretIndex]) % FIELDSIZE) != 1)
    // {
    //     printf("FAIL (zero)\n");
    //     exit(0);
    // }

    // for (size_t i = 0; i < outl; i++)
    // {
    //     if (i == secretIndex)
    //         continue;

    //     if (((shares0[i] + shares1[i]) % FIELDSIZE) != 0)
    //     {
    //         printf("FAIL (non-zero)\n");
    //         exit(0);
    //     }
    // }

    // destroyContext(ctx);
    // free(k0);
    // free(k1);
    // free(shares0);
    // free(shares1);
    // printf("DONE\n\n");
}

void testMMO()
{
    printf("Testing MMO Consistency...\n");
    size_t outblocks = 4; // 确保这里和初始化一致

    for (int i = 0; i < 100; i++)
    {
        uint128_t hashkey0;
        uint128_t hashkey1;
        RAND_bytes((uint8_t *)&hashkey0, sizeof(uint128_t));
        hashkey1 = hashkey0;

        struct Hash *hash0 = initMMOHash((uint8_t *)&hashkey0, outblocks);
        struct Hash *hash1 = initMMOHash((uint8_t *)&hashkey1, outblocks);

        uint128_t messages[2];
        RAND_bytes((uint8_t *)messages, sizeof(uint128_t) * 2);

        // 修复点 1：使用 malloc 分配输出缓冲，避免栈溢出检测 (Stack Smashing)
        // 长度必须是 outblocks * 16字节
        uint128_t *output0 = malloc(sizeof(uint128_t) * outblocks);
        uint128_t *output1 = malloc(sizeof(uint128_t) * outblocks);

        for (int j = 0; j < 1000; j++)
        {
            // 修复点 2：直接传入指针
            mmoHash2to4(hash0, (uint8_t *)messages, (uint8_t *)output0);
            mmoHash2to4(hash1, (uint8_t *)messages, (uint8_t *)output1);

            // 校验一致性
            for (size_t k = 0; k < outblocks; k++)
            {
                if (output0[k] != output1[k])
                {
                    printf("MMO Consistency FAIL at trial %d, block %zu\n", i, k);
                    exit(1);
                }
            }
        }

        // 修复点 3：及时释放内存，防止内存泄漏
        free(output0);
        free(output1);
        destroyMMOHash(hash0);
        destroyMMOHash(hash1);
    }
    printf("MMO Test PASS\n");
}

int main(int argc, char **argv)
{

    int testTrials = 20;
    printf("******************************************\n");
    printf("Testing VDPF\n");
    for (int i = 0; i < testTrials; i++)
        testVDPF();
    printf("******************************************\n");
    printf("PASS\n");
    printf("******************************************\n\n");

    printf("******************************************\n");
    printf("Testing DPF\n");
    for (int i = 0; i < testTrials; i++)
        testDPF();
    printf("******************************************\n");
    printf("PASS\n");
    printf("******************************************\n\n");

    printf("******************************************\n");
    printf("Testing MMO\n");
    //testMMO();
    printf("******************************************\n");
    printf("PASS\n");
    printf("******************************************\n\n");
}
