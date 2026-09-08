#include "dpf.h"

#include <openssl/rand.h>

#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>

extern "C" {
struct Hash {
  EVP_CIPHER_CTX *mmoCtx;
  int outblocks;
};

struct Hash *initMMOHash(uint8_t *seed, uint64_t outblocks);
void destroyMMOHash(struct Hash *hash);
EVP_CIPHER_CTX *getDPFContext(uint8_t *);
void destroyContext(EVP_CIPHER_CTX *);
void genVDPF(
    EVP_CIPHER_CTX *ctx,
    struct Hash *hash,
    int size,
    uint64_t index,
    uint64_t beta,
    unsigned char *k0,
    unsigned char *k1);
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
    uint8_t *pi);
}

namespace {
constexpr size_t kVdpfHash1OutBlocks = 4;
constexpr size_t kVdpfHash2OutBlocks = 2;

size_t vdpf_key_size(uint8_t bit_length) {
  return static_cast<size_t>(18 * bit_length + 18 + 16 + 16 * kVdpfHash1OutBlocks);
}
}

struct DPF::VDPFState {
  std::array<uint8_t, 16> prf_key{};
  std::array<uint8_t, 16> hash_key1{};
  std::array<uint8_t, 16> hash_key2{};
  EVP_CIPHER_CTX *ctx = nullptr;
  struct Hash *h1 = nullptr;
  struct Hash *h2 = nullptr;

  VDPFState() {
    if (RAND_bytes(prf_key.data(), static_cast<int>(prf_key.size())) != 1) {
      throw std::runtime_error("failed to initialize VDPF PRF key");
    }
    if (RAND_bytes(hash_key1.data(), static_cast<int>(hash_key1.size())) != 1) {
      throw std::runtime_error("failed to initialize VDPF hash key1");
    }
    if (RAND_bytes(hash_key2.data(), static_cast<int>(hash_key2.size())) != 1) {
      throw std::runtime_error("failed to initialize VDPF hash key2");
    }

    ctx = getDPFContext(prf_key.data());
    h1 = initMMOHash(hash_key1.data(), kVdpfHash1OutBlocks);
    h2 = initMMOHash(hash_key2.data(), kVdpfHash2OutBlocks);
    if (ctx == nullptr || h1 == nullptr || h2 == nullptr) {
      throw std::runtime_error("failed to initialize VDPF contexts");
    }
  }

  void reset_contexts() {
    if (h1 != nullptr) {
      destroyMMOHash(h1);
      h1 = nullptr;
    }
    if (h2 != nullptr) {
      destroyMMOHash(h2);
      h2 = nullptr;
    }
    if (ctx != nullptr) {
      destroyContext(ctx);
      ctx = nullptr;
    }

    ctx = getDPFContext(prf_key.data());
    h1 = initMMOHash(hash_key1.data(), kVdpfHash1OutBlocks);
    h2 = initMMOHash(hash_key2.data(), kVdpfHash2OutBlocks);
    if (ctx == nullptr || h1 == nullptr || h2 == nullptr) {
      throw std::runtime_error("failed to reset VDPF contexts");
    }
  }

  void reset_hashes() {
    if (h1 != nullptr) {
      destroyMMOHash(h1);
      h1 = nullptr;
    }
    if (h2 != nullptr) {
      destroyMMOHash(h2);
      h2 = nullptr;
    }

    h1 = initMMOHash(hash_key1.data(), kVdpfHash1OutBlocks);
    h2 = initMMOHash(hash_key2.data(), kVdpfHash2OutBlocks);
    if (h1 == nullptr || h2 == nullptr) {
      throw std::runtime_error("failed to reset VDPF hash contexts");
    }
  }

  ~VDPFState() {
    if (h1 != nullptr) {
      destroyMMOHash(h1);
    }
    if (h2 != nullptr) {
      destroyMMOHash(h2);
    }
    if (ctx != nullptr) {
      destroyContext(ctx);
    }
  }
};

DPF::DPF(uint8_t bit_length_) : FSS(127, bit_length_), state(std::make_shared<VDPFState>()) {}

DPF::~DPF() = default;

bool VerifyVDPFProofs(
    const std::array<uint8_t, kVDPFProofBytes> &server0,
    const std::array<uint8_t, kVDPFProofBytes> &server1) {
  // This is the verification predicate used by vdpf/src/test.c:108-115.
  return server0 == server1;
}

uint64_t DPF::Convert(osuCrypto::block s) {
  return *(uint64_t *)&s;
}

void DPF::InitKey(DPFKey *key) {
  const size_t legacy_len = static_cast<size_t>(this->bit_length);
  const size_t key_len = vdpf_key_size(this->bit_length);

  for (int i = 0; i < 2; i++) {
    key[i].scw = (osuCrypto::block *)calloc(legacy_len, sizeof(osuCrypto::block));
    key[i].tcw_l = (uint8_t *)calloc(legacy_len, sizeof(uint8_t));
    key[i].tcw_r = (uint8_t *)calloc(legacy_len, sizeof(uint8_t));
    key[i].vdpf_key = (unsigned char *)calloc(key_len, sizeof(unsigned char));
    key[i].vdpf_key_len = key_len;
    key[i].fcw = 0;
  }
}

void DPF::Gen(uint64_t alpha, uint64_t beta, DPFKey *key) {
  if (!state) {
    throw std::runtime_error("DPF state is not initialized");
  }
  state->reset_contexts();

  std::memset(key[0].vdpf_key, 0, key[0].vdpf_key_len);
  std::memset(key[1].vdpf_key, 0, key[1].vdpf_key_len);

  genVDPF(
      state->ctx,
      state->h1,
      this->bit_length,
      alpha,
      beta,
      key[0].vdpf_key,
      key[1].vdpf_key);

  key[0].fcw = beta;
  key[1].fcw = beta;
}

uint64_t DPF::Eval(uint8_t b, DPFKey key, uint64_t input) {
  return EvalWithProof(b, key, input).value;
}

DPFEvaluation DPF::EvalWithProof(uint8_t b, DPFKey key, uint64_t input) {
  if (!state) {
    throw std::runtime_error("DPF state is not initialized");
  }
  state->reset_hashes();

  uint64_t in[1] = {input};
  unsigned char out[sizeof(uint64_t) * 2] = {0};
  DPFEvaluation result{};

  batchEvalVDPF(
      state->ctx,
      state->h1,
      state->h2,
      this->bit_length,
      b != 0,
      key.vdpf_key,
      in,
      1,
      out,
      result.proof.data());

  std::memcpy(&result.value, out, sizeof(uint64_t));
  return result;
}

std::array<uint8_t, kVDPFProofBytes> DPF::BatchEvalProof(
    uint8_t b, DPFKey key, const std::vector<uint64_t> &inputs) {
  if (!state || inputs.empty()) {
    throw std::runtime_error("DPF state is not initialized or input batch is empty");
  }
  state->reset_hashes();
  std::vector<unsigned char> out(inputs.size() * sizeof(uint64_t) * 2, 0);
  std::array<uint8_t, kVDPFProofBytes> proof{};
  batchEvalVDPF(
      state->ctx,
      state->h1,
      state->h2,
      this->bit_length,
      b != 0,
      key.vdpf_key,
      const_cast<uint64_t *>(inputs.data()),
      inputs.size(),
      out.data(),
      proof.data());
  return proof;
}

VDPFEvaluation DPF::VerEval(
    uint8_t b, const DPFKey &key, const std::vector<uint64_t> &inputs) {
  if (!state || inputs.empty()) {
    throw std::runtime_error("DPF state is not initialized or input batch is empty");
  }
  if (b > 1 || key.vdpf_key == nullptr ||
      key.vdpf_key_len != ExpectedKeySize()) {
    throw std::invalid_argument("invalid VDPF party or key share");
  }

  state->reset_hashes();
  std::vector<uint64_t> mutable_inputs(inputs);
  std::vector<unsigned char> raw_outputs(inputs.size() * sizeof(uint64_t) * 2, 0);
  VDPFEvaluation result;
  result.outputs.resize(inputs.size());
  batchEvalVDPF(
      state->ctx, state->h1, state->h2, this->bit_length, b != 0,
      key.vdpf_key, mutable_inputs.data(), mutable_inputs.size(),
      raw_outputs.data(), result.proof.data());
  for (size_t i = 0; i < inputs.size(); ++i) {
    std::memcpy(&result.outputs[i],
                raw_outputs.data() + i * sizeof(uint64_t) * 2,
                sizeof(uint64_t));
  }
  return result;
}

bool DPF::VerifyProofs(const VDPFProof &proof0, const VDPFProof &proof1) {
  return VerifyVDPFProofs(proof0, proof1);
}

size_t DPF::ExpectedKeySize() const {
  return vdpf_key_size(this->bit_length);
}

void DPF::FreeKey(DPFKey key) {
  free(key.scw);
  free(key.tcw_l);
  free(key.tcw_r);
  free(key.vdpf_key);
}

void DPF::ExportContextKeys(uint8_t *out) const {
  std::memcpy(out, state->prf_key.data(), state->prf_key.size());
  std::memcpy(out + 16, state->hash_key1.data(), state->hash_key1.size());
  std::memcpy(out + 32, state->hash_key2.data(), state->hash_key2.size());
}

void DPF::ImportContextKeys(const uint8_t *in) {
  std::memcpy(state->prf_key.data(), in, state->prf_key.size());
  std::memcpy(state->hash_key1.data(), in + 16, state->hash_key1.size());
  std::memcpy(state->hash_key2.data(), in + 32, state->hash_key2.size());
  state->reset_contexts();
}

void TestDPF() {
  std::cout << "--------------- VDPF TEST ---------------" << std::endl;
  uint64_t alpha = 6;
  uint64_t beta = 1;

  DPFKey key[2];
  DPF dpf(50);

  printf("lambda is %d, bit length is %d\n", dpf.lambda, dpf.bit_length);

  dpf.InitKey(key);
  dpf.Gen(alpha, beta, key);

  uint64_t o0, o1;

  int times = 1000000;
  osuCrypto::Timer timer;

  timer.setTimePoint("start");

  for (int i = 0; i < times; i++) {
    o0 = dpf.Eval(0, key[0], i);
  }

  timer.setTimePoint("10^6 vdpf eval test finish");

  std::cout << timer << std::endl;
}
