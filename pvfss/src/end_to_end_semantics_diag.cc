#include "dpf.h"
#include "../../vdpf/include/field61.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

int main() {
  constexpr uint64_t recipient_a = 1;
  constexpr uint64_t recipient_b = 2;
  constexpr uint64_t recipient_c = 3;
  constexpr uint64_t query = recipient_a;
  constexpr std::array<uint64_t, 4> recipients = {
      recipient_a, recipient_b, recipient_a, recipient_c};
  constexpr std::array<uint64_t, 4> locations = {
      VDPF_FIELD_MODULUS - 1, 22, 33, 44};

  DPF dpf(64);
  std::vector<uint64_t> reconstructed;
  reconstructed.reserve(recipients.size());

  for (std::size_t i = 0; i < recipients.size(); ++i) {
    DPFKey keys[2];
    dpf.InitKey(keys);
    dpf.Gen(recipients[i], locations[i], keys);
    const DPFEvaluation eval0 = dpf.EvalWithProof(0, keys[0], query);
    const DPFEvaluation eval1 = dpf.EvalWithProof(1, keys[1], query);
    const bool proof_ok = VerifyVDPFProofs(eval0.proof, eval1.proof);
    if (!proof_ok) {
      std::cerr << "unexpected proof mismatch for signal=" << (i + 1) << '\n';
      return 1;
    }
    const uint64_t value = field61_add(eval0.value, eval1.value);
    if (value != 0) {
      reconstructed.push_back(value);
    }
    std::cout << "signal=" << (i + 1) << " recipient=" << recipients[i]
              << " location=" << locations[i] << " share0=" << eval0.value
              << " share1=" << eval1.value << " additive_reconstruction=" << value
              << '\n';
    dpf.FreeKey(keys[0]);
    dpf.FreeKey(keys[1]);
  }

  auto actual = reconstructed;
  std::sort(actual.begin(), actual.end());
  std::vector<uint64_t> expected = {VDPF_FIELD_MODULUS - 1, 33};
  std::sort(expected.begin(), expected.end());
  const bool exact_locations_ok = actual == expected;

  DPFKey negative_keys[2];
  dpf.InitKey(negative_keys);
  dpf.Gen(recipient_a, 17, negative_keys);
  const DPFEvaluation honest0 = dpf.EvalWithProof(0, negative_keys[0], query);
  const DPFEvaluation honest1 = dpf.EvalWithProof(1, negative_keys[1], query);
  const bool honest_proof_ok = VerifyVDPFProofs(honest0.proof, honest1.proof);
  auto tampered_proof = honest1.proof;
  tampered_proof[0] ^= 1;
  const bool tampered_proof_rejected = !VerifyVDPFProofs(honest0.proof, tampered_proof);
  const bool target_value_ok = field61_add(honest0.value, honest1.value) == 17;
  const DPFEvaluation non_target0 = dpf.EvalWithProof(0, negative_keys[0], recipient_b);
  const DPFEvaluation non_target1 = dpf.EvalWithProof(1, negative_keys[1], recipient_b);
  const bool non_target_proof_ok = VerifyVDPFProofs(non_target0.proof, non_target1.proof);
  const bool non_target_zero_ok = field61_add(non_target0.value, non_target1.value) == 0;
  dpf.FreeKey(negative_keys[0]);
  dpf.FreeKey(negative_keys[1]);

  const bool ok = exact_locations_ok && honest_proof_ok &&
                  tampered_proof_rejected && target_value_ok &&
                  non_target_proof_ok && non_target_zero_ok;

  std::cout << "query_recipient=" << query << " expected={33,"
            << (VDPF_FIELD_MODULUS - 1) << "} actual={";
  for (std::size_t i = 0; i < reconstructed.size(); ++i) {
    if (i != 0) std::cout << ',';
    std::cout << reconstructed[i];
  }
  std::cout << "}\n"
            << "proof_verification_positive=" << (honest_proof_ok ? "ok" : "failed") << '\n'
            << "proof_verification_tampered=" << (tampered_proof_rejected ? "rejected" : "accepted") << '\n'
            << "target_eval=" << (target_value_ok ? "ok" : "failed") << '\n'
            << "non_target_eval=" << (non_target_zero_ok ? "zero" : "nonzero") << '\n'
            << "field_boundary_beta=" << (VDPF_FIELD_MODULUS - 1) << '\n'
            << "current_artifact_end_to_end=" << (ok ? "ok" : "failed") << '\n';
  return ok ? 0 : 1;
}
