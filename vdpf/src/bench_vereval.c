#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include <openssl/rand.h>

#include "../include/dpf.h"
#include "../include/mmo.h"

enum
{
    PROOF_BYTES = 32,
    HASH1_OUT_BLOCKS = 4,
    HASH2_OUT_BLOCKS = 2
};

struct options
{
    uint64_t users;
    int bits;
    int repetitions;
    int warmups;
};

static void usage(const char *program)
{
    fprintf(
        stderr,
        "Usage: %s [--users N] [--bits N] [--repetitions N] [--warmups N]\n"
        "\n"
        "Serial VerEval benchmark over a deterministic, distinct X_reg.\n"
        "CSV is written to stdout and progress information to stderr.\n",
        program);
}

static bool parse_u64(const char *text, uint64_t *value)
{
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0')
    {
        return false;
    }
    *value = (uint64_t)parsed;
    return true;
}

static bool parse_int(const char *text, int *value)
{
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed < INT_MIN || parsed > INT_MAX)
    {
        return false;
    }
    *value = (int)parsed;
    return true;
}

static bool parse_options(int argc, char **argv, struct options *options)
{
    options->users = 10000;
    options->bits = 50;
    options->repetitions = 5;
    options->warmups = 1;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            usage(argv[0]);
            exit(EXIT_SUCCESS);
        }

        if (i + 1 >= argc)
        {
            fprintf(stderr, "Missing value for %s\n", argv[i]);
            return false;
        }

        if (strcmp(argv[i], "--users") == 0)
        {
            if (!parse_u64(argv[++i], &options->users))
            {
                fprintf(stderr, "Invalid --users value\n");
                return false;
            }
        }
        else if (strcmp(argv[i], "--bits") == 0)
        {
            if (!parse_int(argv[++i], &options->bits))
            {
                fprintf(stderr, "Invalid --bits value\n");
                return false;
            }
        }
        else if (strcmp(argv[i], "--repetitions") == 0)
        {
            if (!parse_int(argv[++i], &options->repetitions))
            {
                fprintf(stderr, "Invalid --repetitions value\n");
                return false;
            }
        }
        else if (strcmp(argv[i], "--warmups") == 0)
        {
            if (!parse_int(argv[++i], &options->warmups))
            {
                fprintf(stderr, "Invalid --warmups value\n");
                return false;
            }
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return false;
        }
    }

    if (options->users == 0 || options->bits < 1 || options->bits > 64 ||
        options->repetitions < 1 || options->warmups < 0)
    {
        fprintf(stderr, "Require users>0, 1<=bits<=64, repetitions>0, warmups>=0\n");
        return false;
    }

    if (options->bits < 64)
    {
        uint64_t domain_size = UINT64_C(1) << options->bits;
        if (options->users > domain_size)
        {
            fprintf(stderr, "users exceeds the identifier domain\n");
            return false;
        }
    }

    if (options->users > SIZE_MAX / sizeof(uint64_t) ||
        options->users > SIZE_MAX / sizeof(uint128_t))
    {
        fprintf(stderr, "Requested allocation is too large\n");
        return false;
    }

    return true;
}

static double elapsed_ms(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) * 1000.0 +
           (double)(end.tv_nsec - start.tv_nsec) / 1000000.0;
}

static double now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

static double peak_rss_mb(void)
{
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0)
    {
        return -1.0;
    }
    /* Linux reports ru_maxrss in KiB. */
    return (double)usage.ru_maxrss / 1024.0;
}

static void fill_registered_identifiers(uint64_t *identifiers, uint64_t users, int bits)
{
    /*
     * An odd multiplier is a permutation modulo 2^bits, so the first
     * 'users' values are distinct while exercising non-trivial tree paths.
     */
    const uint64_t multiplier = UINT64_C(0x9e3779b97f4a7c15);
    const uint64_t offset = UINT64_C(0x243f6a8885a308d3);
    const uint64_t mask =
        bits == 64 ? UINT64_MAX : ((UINT64_C(1) << bits) - UINT64_C(1));

    for (uint64_t i = 0; i < users; ++i)
    {
        identifiers[i] = (i * multiplier + offset) & mask;
    }
}

static void fresh_hashes(
    const uint8_t hash_key1[16],
    const uint8_t hash_key2[16],
    struct Hash **hash1,
    struct Hash **hash2)
{
    *hash1 = initMMOHash((uint8_t *)hash_key1, HASH1_OUT_BLOCKS);
    *hash2 = initMMOHash((uint8_t *)hash_key2, HASH2_OUT_BLOCKS);
    if (*hash1 == NULL || *hash2 == NULL)
    {
        fprintf(stderr, "Failed to initialize MMO hash contexts\n");
        exit(EXIT_FAILURE);
    }
}

static double evaluate_party(
    EVP_CIPHER_CTX *ctx,
    int bits,
    bool party,
    unsigned char *key,
    uint64_t *identifiers,
    uint64_t users,
    uint128_t *outputs,
    uint8_t proof[PROOF_BYTES],
    const uint8_t hash_key1[16],
    const uint8_t hash_key2[16])
{
    struct Hash *hash1 = NULL;
    struct Hash *hash2 = NULL;
    struct timespec start;
    struct timespec end;

    fresh_hashes(hash_key1, hash_key2, &hash1, &hash2);
    memset(proof, 0, PROOF_BYTES);

    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0)
    {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    batchEvalVDPF(
        ctx,
        hash1,
        hash2,
        bits,
        party,
        key,
        identifiers,
        users,
        (uint8_t *)outputs,
        proof);

    if (clock_gettime(CLOCK_MONOTONIC, &end) != 0)
    {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    destroyMMOHash(hash1);
    destroyMMOHash(hash2);
    return elapsed_ms(start, end);
}

static bool outputs_are_correct(
    const uint128_t *outputs0,
    const uint128_t *outputs1,
    uint64_t users,
    uint64_t target_position)
{
    for (uint64_t i = 0; i < users; ++i)
    {
        uint8_t combined = (uint8_t)((outputs0[i] + outputs1[i]) & 1);
        uint8_t expected = i == target_position ? 1 : 0;
        if (combined != expected)
        {
            fprintf(
                stderr,
                "Incorrect combined output at position %" PRIu64
                ": got %u, expected %u\n",
                i,
                (unsigned)combined,
                (unsigned)expected);
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    struct options options;
    if (!parse_options(argc, argv, &options))
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    const size_t key_bytes =
        (size_t)(18 * options.bits + 18 + 16 + 16 * HASH1_OUT_BLOCKS);
    uint64_t *identifiers =
        (uint64_t *)malloc((size_t)options.users * sizeof(uint64_t));
    uint128_t *outputs0 =
        (uint128_t *)malloc((size_t)options.users * sizeof(uint128_t));
    uint128_t *outputs1 =
        (uint128_t *)malloc((size_t)options.users * sizeof(uint128_t));
    unsigned char *key0 = (unsigned char *)calloc(key_bytes, 1);
    unsigned char *key1 = (unsigned char *)calloc(key_bytes, 1);

    if (identifiers == NULL || outputs0 == NULL || outputs1 == NULL ||
        key0 == NULL || key1 == NULL)
    {
        fprintf(stderr, "Allocation failed for users=%" PRIu64 "\n", options.users);
        return EXIT_FAILURE;
    }

    fill_registered_identifiers(identifiers, options.users, options.bits);
    const uint64_t target_position = options.users / 2;
    const uint64_t target_identifier = identifiers[target_position];

    uint8_t prf_key[16];
    uint8_t hash_key1[16];
    uint8_t hash_key2[16];
    if (RAND_bytes(prf_key, sizeof(prf_key)) != 1 ||
        RAND_bytes(hash_key1, sizeof(hash_key1)) != 1 ||
        RAND_bytes(hash_key2, sizeof(hash_key2)) != 1)
    {
        fprintf(stderr, "RAND_bytes failed\n");
        return EXIT_FAILURE;
    }

    EVP_CIPHER_CTX *ctx = getDPFContext(prf_key);
    struct Hash *keygen_hash = initMMOHash(hash_key1, HASH1_OUT_BLOCKS);
    if (ctx == NULL || keygen_hash == NULL)
    {
        fprintf(stderr, "Failed to initialize VDPF contexts\n");
        return EXIT_FAILURE;
    }

    double keygen_start_ms = now_ms();
    genVDPF(
        ctx,
        keygen_hash,
        options.bits,
        target_identifier,
        key0,
        key1);
    double keygen_ms = now_ms() - keygen_start_ms;
    destroyMMOHash(keygen_hash);

    fprintf(
        stderr,
        "[BENCH] implementation=serial users=%" PRIu64
        " bits=%d repetitions=%d warmups=%d target_position=%" PRIu64
        " key_bytes=%zu\n",
        options.users,
        options.bits,
        options.repetitions,
        options.warmups,
        target_position,
        key_bytes);

    uint8_t proof0[PROOF_BYTES];
    uint8_t proof1[PROOF_BYTES];

    for (int warmup = 0; warmup < options.warmups; ++warmup)
    {
        (void)evaluate_party(
            ctx,
            options.bits,
            false,
            key0,
            identifiers,
            options.users,
            outputs0,
            proof0,
            hash_key1,
            hash_key2);
        (void)evaluate_party(
            ctx,
            options.bits,
            true,
            key1,
            identifiers,
            options.users,
            outputs1,
            proof1,
            hash_key1,
            hash_key2);
    }

    printf(
        "implementation,users,bits,threads,repetition,keygen_ms,"
        "server0_ms,server1_ms,protocol_wall_ms,verify_ms,correctness_ms,"
        "users_per_second,peak_rss_mb,proof_equal,outputs_correct\n");

    bool all_correct = true;
    for (int repetition = 1; repetition <= options.repetitions; ++repetition)
    {
        double server0_ms = evaluate_party(
            ctx,
            options.bits,
            false,
            key0,
            identifiers,
            options.users,
            outputs0,
            proof0,
            hash_key1,
            hash_key2);
        double server1_ms = evaluate_party(
            ctx,
            options.bits,
            true,
            key1,
            identifiers,
            options.users,
            outputs1,
            proof1,
            hash_key1,
            hash_key2);

        double verify_start_ms = now_ms();
        bool proof_equal = memcmp(proof0, proof1, PROOF_BYTES) == 0;
        double verify_ms = now_ms() - verify_start_ms;

        double correctness_start_ms = now_ms();
        bool outputs_correct = outputs_are_correct(
            outputs0,
            outputs1,
            options.users,
            target_position);
        double correctness_ms = now_ms() - correctness_start_ms;

        double protocol_wall_ms =
            server0_ms > server1_ms ? server0_ms : server1_ms;
        double users_per_second =
            protocol_wall_ms > 0.0
                ? (double)options.users * 1000.0 / protocol_wall_ms
                : 0.0;

        printf(
            "serial,%" PRIu64 ",%d,1,%d,%.6f,%.6f,%.6f,%.6f,"
            "%.6f,%.6f,%.3f,%.3f,%d,%d\n",
            options.users,
            options.bits,
            repetition,
            keygen_ms,
            server0_ms,
            server1_ms,
            protocol_wall_ms,
            verify_ms,
            correctness_ms,
            users_per_second,
            peak_rss_mb(),
            proof_equal ? 1 : 0,
            outputs_correct ? 1 : 0);
        fflush(stdout);

        if (!proof_equal)
        {
            fprintf(stderr, "Proof mismatch at repetition %d\n", repetition);
        }
        all_correct = all_correct && proof_equal && outputs_correct;
    }

    destroyContext(ctx);
    free(key0);
    free(key1);
    free(outputs0);
    free(outputs1);
    free(identifiers);

    return all_correct ? EXIT_SUCCESS : EXIT_FAILURE;
}
