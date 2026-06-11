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
#include <cstddef>
#include <memory>

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

  void FreeKey(DPFKey key);

  void ExportContextKeys(uint8_t *out) const;

  void ImportContextKeys(const uint8_t *in);

private:
  struct VDPFState;
  std::shared_ptr<VDPFState> state;
};

void TestDPF();

#endif
