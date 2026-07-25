// This is the 2-party FSS for *verifiable* point functions from:
// "Lightweight, Maliciously Secure Verifiable Function Secret Sharing."
// by de Castro, Leo, and Polychroniadou, Antigoni.
// Annual International Conference on the Theory and Applications
// of Cryptographic Techniques. Springer, Cham, 2022
// ePrint: https://eprint.iacr.org/2021/580

#include "../include/dpf.h"
#include "../include/mmo.h"
#include "../include/common.h"
#include "../include/sha256.h"
#include <openssl/rand.h>
#include <pthread.h>
#include <stdlib.h>

struct Sha_256 sha_256;

void genVDPF(
	EVP_CIPHER_CTX *ctx,
	struct Hash *hash,
	int size,
	uint64_t index,
	unsigned char *k0,
	unsigned char *k1)
{

	int didFinish = false;
	while (!didFinish)
	{
		uint128_t seeds0[size + 1];
		uint128_t seeds1[size + 1];
		int bits0[size + 1];
		int bits1[size + 1];

		uint128_t sCW[size];
		int tCW0[size];
		int tCW1[size];

		seeds0[0] = getRandomBlock();
		seeds1[0] = getRandomBlock();
		bits0[0] = 0;
		bits1[0] = 1;

		uint128_t s0[2], s1[2]; // 0=L,1=R
		int t0[2], t1[2];
		for (int i = 1; i <= size; i++)
		{
			dpfPRG(ctx, seeds0[i - 1], &s0[LEFT], &s0[RIGHT], &t0[LEFT], &t0[RIGHT]);
			dpfPRG(ctx, seeds1[i - 1], &s1[LEFT], &s1[RIGHT], &t1[LEFT], &t1[RIGHT]);

			int keep, lose;
			int indexBit = getbit(index, size, i);
			if (indexBit == 0)
			{
				keep = LEFT;
				lose = RIGHT;
			}
			else
			{
				keep = RIGHT;
				lose = LEFT;
			}

			sCW[i - 1] = s0[lose] ^ s1[lose];

			tCW0[i - 1] = t0[LEFT] ^ t1[LEFT] ^ indexBit ^ 1;
			tCW1[i - 1] = t0[RIGHT] ^ t1[RIGHT] ^ indexBit;

			if (bits0[i - 1] == 1)
			{
				seeds0[i] = s0[keep] ^ sCW[i - 1];
				if (keep == 0)
					bits0[i] = t0[keep] ^ tCW0[i - 1];
				else
					bits0[i] = t0[keep] ^ tCW1[i - 1];
			}
			else
			{
				seeds0[i] = s0[keep];
				bits0[i] = t0[keep];
			}

			if (bits1[i - 1] == 1)
			{
				seeds1[i] = s1[keep] ^ sCW[i - 1];
				if (keep == 0)
					bits1[i] = t1[keep] ^ tCW0[i - 1];
				else
					bits1[i] = t1[keep] ^ tCW1[i - 1];
			}
			else
			{
				seeds1[i] = s1[keep];
				bits1[i] = t1[keep];
			}
		}

		// *********************************
		// START: verification code
		// *********************************
		uint128_t pi0[hash->outblocks];
		uint128_t pi1[hash->outblocks];

		uint128_t hashinput[2];
		hashinput[0] = index;
		hashinput[1] = seeds0[size];

		mmoHash2to4(hash, (uint8_t *)&hashinput[0], (uint8_t *)&pi0);

		hashinput[0] = index;
		hashinput[1] = seeds1[size];
		mmoHash2to4(hash, (uint8_t *)&hashinput[0], (uint8_t *)&pi1);

		uint128_t cs[4];
		cs[0] = pi0[0] ^ pi1[0];
		cs[1] = pi0[1] ^ pi1[1];
		cs[2] = pi0[2] ^ pi1[2];
		cs[3] = pi0[3] ^ pi1[3];

		int bit0 = seed_lsb(seeds0[size]);
		int bit1 = seed_lsb(seeds1[size]);

		if (bit0 != bit1)
			didFinish = true;
		else
			continue;
		// *********************************
		// END: DPF verification code
		// *********************************

		uint128_t sFinal0 = convert(&seeds0[size]);
		uint128_t sFinal1 = convert(&seeds1[size]);
		uint128_t lastCW = 1 ^ sFinal0 ^ sFinal1;

		k0[0] = 0;
		memcpy(&k0[1], seeds0, 16);
		k0[17] = bits0[0];
		for (int i = 1; i <= size; i++)
		{
			memcpy(&k0[18 * i], &sCW[i - 1], 16);
			k0[CWSIZE * i + CWSIZE - 2] = tCW0[i - 1];
			k0[CWSIZE * i + CWSIZE - 1] = tCW1[i - 1];
		}
		memcpy(&k0[INDEX_LASTCW], &lastCW, 16);
		memcpy(&k0[INDEX_LASTCW + 16], cs, 16 * (hash->outblocks));

		memcpy(k1, k0, INDEX_LASTCW + 16 + 16 * (hash->outblocks));
		memcpy(&k1[1], seeds1, 16); // only value that is different from k0
		k1[0] = 1;
		k1[17] = bits1[0];
	}
}

// Follows implementation of https://eprint.iacr.org/2021/580.pdf (Figure 1)
// mmo_hash1 = H, mmo_hash2 = H'; pi is the verification output
// (pi should be equal on both servers)
void batchEvalVDPF(
	EVP_CIPHER_CTX *ctx,
	struct Hash *mmo_hash1,
	struct Hash *mmo_hash2,
	int size,
	bool b,
	unsigned char *k,
	uint64_t *in,
	uint64_t inl,
	uint8_t *out,
	uint8_t *proof)
{

	// parse the key
	uint128_t seeds[size + 1];
	int bits[size + 1];
	uint128_t sCW[size + 1];
	int tCW0[size];
	int tCW1[size];
	uint128_t cs[4];
	uint128_t pi[4];

	memcpy(&seeds[0], &k[1], 16);
	bits[0] = b;

	for (int i = 1; i <= size; i++)
	{
		memcpy(&sCW[i - 1], &k[18 * i], 16);
		tCW0[i - 1] = k[CWSIZE * i + CWSIZE - 2];
		tCW1[i - 1] = k[CWSIZE * i + CWSIZE - 1];
	}

	memcpy(cs, &k[INDEX_LASTCW + 16], 16 * (mmo_hash1->outblocks));
	memcpy(pi, &k[INDEX_LASTCW + 16], 16 * (mmo_hash1->outblocks)); // pi = cs

	uint128_t hashinput[mmo_hash1->outblocks];
	uint128_t tpi[mmo_hash1->outblocks];
	uint128_t cpi[mmo_hash1->outblocks];
	uint128_t sL, sR;
	int tL, tR;

	// outter loop: iterate over all evaluation points
	for (int l = 0; l < inl; l++)
	{
		for (int i = 1; i <= size; i++)
		{
			dpfPRG(ctx, seeds[i - 1], &sL, &sR, &tL, &tR);

			if (bits[i - 1] == 1)
			{
				sL = sL ^ sCW[i - 1];
				sR = sR ^ sCW[i - 1];
				tL = tL ^ tCW0[i - 1];
				tR = tR ^ tCW1[i - 1];
			}

			int xbit = getbit(in[l], size, i);

			seeds[i] = (1 - xbit) * sL + xbit * sR;
			bits[i] = (1 - xbit) * tL + xbit * tR;
		}

		// *********************************
		// START: DPF verification code
		// *********************************
		int bit = seed_lsb(seeds[size]);

		hashinput[0] = in[l];
		hashinput[1] = seeds[size];

		// step 1: H(seeds[size]||X[l])
		mmoHash2to4(mmo_hash1, (uint8_t *)&hashinput[0], (uint8_t *)&tpi[0]);

		// step 2: pi^correct(tpi, cs, bit)
		hashinput[0] = pi[0] ^ correct(tpi[0], cs[0], bit);
		hashinput[1] = pi[1] ^ correct(tpi[1], cs[1], bit);
		hashinput[2] = pi[2] ^ correct(tpi[2], cs[2], bit);
		hashinput[3] = pi[3] ^ correct(tpi[3], cs[3], bit);

		// step 3: comptue pi^H'(pi^tpi)
		mmoHash4to4(mmo_hash2, (uint8_t *)&hashinput[0], (uint8_t *)&cpi[0]);

		pi[0] ^= cpi[0];
		pi[1] ^= cpi[1];
		pi[2] ^= cpi[2];
		pi[3] ^= cpi[3];
		// *********************************
		// END: DPF verification code
		// *********************************

		uint128_t res = convert(&seeds[size]);

		if (bits[size] == 1)
		{
			// correction word
			res = res ^ convert((uint128_t *)&k[INDEX_LASTCW]);
		}

		// copy block to byte output
		memcpy(&out[l * sizeof(uint128_t)], &res, sizeof(uint128_t));
	}

	// VDPF output hash (just SHA256 of pi)
	uint8_t hash[32];
	sha_256_init(&sha_256, hash);
	sha_256_write(&sha_256, (uint8_t *)&pi[0], sizeof(uint128_t) * 4);
	sha_256_close(&sha_256);
	memcpy(proof, hash, 32);
}

struct vdpf_parallel_worker
{
	EVP_CIPHER_CTX *ctx;
	int size;
	bool party;
	const uint64_t *inputs;
	uint64_t begin;
	uint64_t end;
	uint8_t *outputs;
	const uint128_t *seed_correction_words;
	const int *left_bit_correction_words;
	const int *right_bit_correction_words;
	uint128_t initial_seed;
	uint128_t final_correction_word;
	uint128_t *terminal_seeds;
	uint8_t *proof_bits;
};

static void *vdpf_parallel_evaluate_paths(void *opaque)
{
	struct vdpf_parallel_worker *worker =
		(struct vdpf_parallel_worker *)opaque;

	for (uint64_t l = worker->begin; l < worker->end; ++l)
	{
		uint128_t seed = worker->initial_seed;
		int control_bit = worker->party ? 1 : 0;

		for (int level = 1; level <= worker->size; ++level)
		{
			uint128_t left_seed;
			uint128_t right_seed;
			int left_bit;
			int right_bit;

			dpfPRG(
				worker->ctx,
				seed,
				&left_seed,
				&right_seed,
				&left_bit,
				&right_bit);

			if (control_bit == 1)
			{
				left_seed ^= worker->seed_correction_words[level - 1];
				right_seed ^= worker->seed_correction_words[level - 1];
				left_bit ^=
					worker->left_bit_correction_words[level - 1];
				right_bit ^=
					worker->right_bit_correction_words[level - 1];
			}

			int input_bit =
				getbit(worker->inputs[l], worker->size, level);
			seed = input_bit == 0 ? left_seed : right_seed;
			control_bit = input_bit == 0 ? left_bit : right_bit;
		}

		worker->terminal_seeds[l] = seed;
		worker->proof_bits[l] = seed_lsb(seed);

		uint128_t result = seed;
		if (control_bit == 1)
		{
			result ^= worker->final_correction_word;
		}
		memcpy(
			&worker->outputs[l * sizeof(uint128_t)],
			&result,
			sizeof(uint128_t));
	}

	return NULL;
}

// Semantics-preserving parallel batch evaluation.
//
// The independent DPF tree paths are evaluated in parallel with one cloned
// EVP context per worker. The proof transcript is intentionally folded in the
// original input order because pi and the current MMO implementation are
// stateful. Therefore this function produces the same outputs and proof bytes
// as batchEvalVDPF for the same inputs and freshly initialized hash contexts.
int batchEvalVDPFParallel(
	EVP_CIPHER_CTX *ctx,
	struct Hash *mmo_hash1,
	struct Hash *mmo_hash2,
	int size,
	bool b,
	unsigned char *k,
	uint64_t *in,
	uint64_t inl,
	uint8_t *out,
	uint8_t *proof,
	int threads)
{
	if (threads <= 1 || inl <= 1)
	{
		batchEvalVDPF(
			ctx,
			mmo_hash1,
			mmo_hash2,
			size,
			b,
			k,
			in,
			inl,
			out,
			proof);
		return 0;
	}

	if (size <= 0 ||
		inl > SIZE_MAX / sizeof(uint128_t) ||
		inl > SIZE_MAX / sizeof(uint8_t))
	{
		return -1;
	}

	int worker_count = threads;
	if (inl < (uint64_t)worker_count)
	{
		worker_count = (int)inl;
	}

	uint128_t seed_correction_words[size];
	int left_bit_correction_words[size];
	int right_bit_correction_words[size];
	uint128_t initial_seed;
	uint128_t final_correction_word;
	uint128_t cs[4];
	uint128_t pi[4];

	memcpy(&initial_seed, &k[1], sizeof(initial_seed));
	for (int level = 1; level <= size; ++level)
	{
		memcpy(
			&seed_correction_words[level - 1],
			&k[18 * level],
			sizeof(uint128_t));
		left_bit_correction_words[level - 1] =
			k[CWSIZE * level + CWSIZE - 2];
		right_bit_correction_words[level - 1] =
			k[CWSIZE * level + CWSIZE - 1];
	}
	memcpy(
		&final_correction_word,
		&k[INDEX_LASTCW],
		sizeof(final_correction_word));
	memcpy(cs, &k[INDEX_LASTCW + 16], sizeof(cs));
	memcpy(pi, cs, sizeof(pi));

	uint128_t *terminal_seeds =
		(uint128_t *)malloc((size_t)inl * sizeof(uint128_t));
	uint8_t *proof_bits =
		(uint8_t *)malloc((size_t)inl * sizeof(uint8_t));
	EVP_CIPHER_CTX **worker_contexts =
		(EVP_CIPHER_CTX **)calloc(
			(size_t)worker_count,
			sizeof(EVP_CIPHER_CTX *));
	pthread_t *worker_threads =
		(pthread_t *)calloc((size_t)worker_count, sizeof(pthread_t));
	struct vdpf_parallel_worker *workers =
		(struct vdpf_parallel_worker *)calloc(
			(size_t)worker_count,
			sizeof(struct vdpf_parallel_worker));

	if (terminal_seeds == NULL ||
		proof_bits == NULL ||
		worker_contexts == NULL ||
		worker_threads == NULL ||
		workers == NULL)
	{
		free(terminal_seeds);
		free(proof_bits);
		free(worker_contexts);
		free(worker_threads);
		free(workers);
		return -1;
	}

	int status = 0;
	for (int worker_index = 0;
		 worker_index < worker_count;
		 ++worker_index)
	{
		worker_contexts[worker_index] = EVP_CIPHER_CTX_new();
		if (worker_contexts[worker_index] == NULL ||
			EVP_CIPHER_CTX_copy(
				worker_contexts[worker_index],
				ctx) != 1)
		{
			status = -1;
			break;
		}
	}

	int started_workers = 0;
	if (status == 0)
	{
		uint64_t base_work = inl / (uint64_t)worker_count;
		uint64_t extra_work = inl % (uint64_t)worker_count;
		uint64_t next_begin = 0;

		for (int worker_index = 0;
			 worker_index < worker_count;
			 ++worker_index)
		{
			uint64_t work =
				base_work +
				((uint64_t)worker_index < extra_work ? 1 : 0);
			struct vdpf_parallel_worker *worker =
				&workers[worker_index];

			worker->ctx = worker_contexts[worker_index];
			worker->size = size;
			worker->party = b;
			worker->inputs = in;
			worker->begin = next_begin;
			worker->end = next_begin + work;
			worker->outputs = out;
			worker->seed_correction_words =
				seed_correction_words;
			worker->left_bit_correction_words =
				left_bit_correction_words;
			worker->right_bit_correction_words =
				right_bit_correction_words;
			worker->initial_seed = initial_seed;
			worker->final_correction_word =
				final_correction_word;
			worker->terminal_seeds = terminal_seeds;
			worker->proof_bits = proof_bits;
			next_begin += work;

			if (pthread_create(
					&worker_threads[worker_index],
					NULL,
					vdpf_parallel_evaluate_paths,
					worker) != 0)
			{
				status = -1;
				break;
			}
			++started_workers;
		}
	}

	for (int worker_index = 0;
		 worker_index < started_workers;
		 ++worker_index)
	{
		if (pthread_join(worker_threads[worker_index], NULL) != 0)
		{
			status = -1;
		}
	}

	for (int worker_index = 0;
		 worker_index < worker_count;
		 ++worker_index)
	{
		EVP_CIPHER_CTX_free(worker_contexts[worker_index]);
	}
	free(worker_contexts);
	free(worker_threads);
	free(workers);

	if (status == 0)
	{
		uint128_t hash_input[4];
		uint128_t terminal_proof[4];
		uint128_t compressed_proof[4];

		for (uint64_t l = 0; l < inl; ++l)
		{
			hash_input[0] = in[l];
			hash_input[1] = terminal_seeds[l];
			mmoHash2to4(
				mmo_hash1,
				(uint8_t *)hash_input,
				(uint8_t *)terminal_proof);

			for (int block = 0; block < 4; ++block)
			{
				hash_input[block] =
					pi[block] ^
					correct(
						terminal_proof[block],
						cs[block],
						proof_bits[l]);
			}

			mmoHash4to4(
				mmo_hash2,
				(uint8_t *)hash_input,
				(uint8_t *)compressed_proof);
			for (int block = 0; block < 4; ++block)
			{
				pi[block] ^= compressed_proof[block];
			}
		}

		uint8_t hash[32];
		struct Sha_256 local_sha_256;
		sha_256_init(&local_sha_256, hash);
		sha_256_write(
			&local_sha_256,
			(uint8_t *)pi,
			sizeof(pi));
		sha_256_close(&local_sha_256);
		memcpy(proof, hash, sizeof(hash));
	}

	free(terminal_seeds);
	free(proof_bits);
	return status;
}

void fullDomainVDPF(
	EVP_CIPHER_CTX *ctx,
	struct Hash *mmo_hash1,
	struct Hash *mmo_hash2,
	int size,
	bool b,
	unsigned char *k,
	uint8_t *out,
	uint8_t *proof)
{

	int numLeaves = 1 << size;
	int maxLayer = size;

	int currLevel = 0;
	int levelIndex = 0;
	int numIndexesInLevel = 2;

	int treeSize = 2 * numLeaves - 1;

	// treeSize too big to allocate on stack
	uint128_t *seeds = malloc(sizeof(uint128_t) * treeSize);
	int *bits = malloc(sizeof(int) * treeSize);
	uint128_t sCW[maxLayer + 1];
	int tCW0[maxLayer + 1];
	int tCW1[maxLayer + 1];
	uint128_t cs[4];
	uint128_t pi[4];

	uint128_t hashinput[4];
	uint128_t tpi[4];
	uint128_t cpi[4];

	memcpy(seeds, &k[1], 16);
	bits[0] = b;

	for (int i = 1; i <= maxLayer; i++)
	{
		memcpy(&sCW[i - 1], &k[18 * i], 16);
		tCW0[i - 1] = k[CWSIZE * i + CWSIZE - 2];
		tCW1[i - 1] = k[CWSIZE * i + CWSIZE - 1];
	}

	memcpy(cs, &k[INDEX_LASTCW + 16], 16 * (mmo_hash1->outblocks));
	memcpy(pi, &k[INDEX_LASTCW + 16], 16 * (mmo_hash1->outblocks)); // pi = cs

	uint128_t sL, sR;
	int tL, tR;
	for (int i = 1; i < treeSize; i += 2)
	{
		int parentIndex = 0;
		if (i > 1)
		{
			parentIndex = i - levelIndex - ((numIndexesInLevel - levelIndex) / 2);
		}

		dpfPRG(ctx, seeds[parentIndex], &sL, &sR, &tL, &tR);

		if (bits[parentIndex] == 1)
		{
			sL = sL ^ sCW[currLevel];
			sR = sR ^ sCW[currLevel];
			tL = tL ^ tCW0[currLevel];
			tR = tR ^ tCW1[currLevel];
		}

		int lIndex = i;
		int rIndex = i + 1;
		seeds[lIndex] = sL;
		bits[lIndex] = tL;
		seeds[rIndex] = sR;
		bits[rIndex] = tR;

		levelIndex += 2;
		if (levelIndex == numIndexesInLevel)
		{
			currLevel++;
			numIndexesInLevel *= 2;
			levelIndex = 0;
		}
	}

	uint128_t *outBlocks = (uint128_t *)out;
	for (int i = 0; i < numLeaves; i++)
	{
		int index = treeSize - numLeaves + i;

		uint128_t res = convert(&seeds[index]);

		if (bits[index] == 1)
		{
			// correction word
			res = res ^ convert((uint128_t *)&k[INDEX_LASTCW]);
		}

		// copy block to byte output
		outBlocks[i] = res;

		// *********************************
		// START: DPF verification code
		// *********************************
		int bit = seed_lsb(seeds[index]);

		hashinput[0] = index;
		hashinput[1] = seeds[index];
		hashinput[2] = 0;
		hashinput[3] = 0;

		// step 1: H(seeds[size]||X[l])
		mmoHash2to4(mmo_hash1, (uint8_t *)&hashinput[0], (uint8_t *)&tpi[0]);

		// step 2: pi^correct(tpi, cs, bit)
		hashinput[0] = pi[0] ^ correct(tpi[0], cs[0], bit);
		hashinput[1] = pi[1] ^ correct(tpi[1], cs[1], bit);
		hashinput[2] = pi[2] ^ correct(tpi[2], cs[2], bit);
		hashinput[3] = pi[3] ^ correct(tpi[3], cs[3], bit);

		// step 3: comptue pi^H'(pi^tpi)
		mmoHash4to4(mmo_hash2, (uint8_t *)&hashinput[0], (uint8_t *)&cpi[0]);

		pi[0] ^= cpi[0];
		pi[1] ^= cpi[1];
		pi[2] ^= cpi[2];
		pi[3] ^= cpi[3];
		// *********************************
		// END: DPF verification code
		// *********************************
	}

	// VDPF output hash
	uint8_t hash[32];
	sha_256_init(&sha_256, hash);
	sha_256_write(&sha_256, (uint8_t *)&pi[0], sizeof(uint128_t) * 4);
	sha_256_close(&sha_256);
	memcpy(proof, hash, sizeof(uint8_t) * 32);

	free(bits);
	free(seeds);
}
