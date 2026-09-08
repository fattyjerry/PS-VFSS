/**
 * VDPF-backed point function wrapper.
 */

#pragma once

#ifndef __DPF_H
#define __DPF_H

#include "fss.h"
#include "dlen_prng.h"

#include <cryptoTools/Crypto/PRNG.h>
#include <cryptoTools/Common/block.h>

#include <cryptoTools/Common/Timer.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// TODO: support template like template <typename GroupEle>
struct DPFKey {
  osuCrypto::block s;
  osuCrypto::block *scw;
  uint8_t *tcw_l;
  uint8_t *tcw_r;
  uint64_t fcw; // the final cw, it is a group element
  unsigned char *vdpf_key;
  size_t vdpf_key_len;
};

constexpr std::size_t kVDPFProofBytes = 32;
using VDPFProof = std::array<uint8_t, kVDPFProofBytes>;

struct VDPFEvaluation {
  std::vector<uint64_t> outputs;
  VDPFProof proof{};
};

struct DPFEvaluation {
  uint64_t value;
  std::array<uint8_t, kVDPFProofBytes> proof;
};

bool VerifyVDPFProofs(
    const std::array<uint8_t, kVDPFProofBytes> &server0,
    const std::array<uint8_t, kVDPFProofBytes> &server1);

// TODO: support Group Element template
// !only support two parties
// !only support 128-bit security parameter now
// implement verifiable point functions
class DPF: public FSS<DPFKey, uint64_t> {
public:
  DPF(uint8_t bit_length_);
  ~DPF();

  void InitKey(DPFKey *key);

  uint64_t Convert(osuCrypto::block s);

  void Gen(uint64_t alpha, uint64_t beta, DPFKey *key);

  uint64_t Eval(uint8_t b, DPFKey key, uint64_t input);

  // The bundled VDPF emits a 32-byte verification value for each evaluated
  // input batch.  The two servers verify an evaluation by comparing these
  // values byte-for-byte, as in vdpf/src/test.c.
  DPFEvaluation EvalWithProof(uint8_t b, DPFKey key, uint64_t input);

  std::array<uint8_t, kVDPFProofBytes> BatchEvalProof(
      uint8_t b, DPFKey key, const std::vector<uint64_t> &inputs);

  // Compatibility API used by admission-time verification.  It returns the
  // same batch outputs and proof produced by the bundled VDPF evaluator.
  VDPFEvaluation VerEval(
      uint8_t b, const DPFKey &key, const std::vector<uint64_t> &inputs);

  static bool VerifyProofs(
      const VDPFProof &proof0, const VDPFProof &proof1);

  size_t ExpectedKeySize() const;

  void FreeKey(DPFKey key);

  void ExportContextKeys(uint8_t *out) const;

  void ImportContextKeys(const uint8_t *in);

private:
  struct VDPFState;
  std::shared_ptr<VDPFState> state;
};

void TestDPF();

#endif
