#include "dpf.h"
#include "../../vdpf/include/field61.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
  constexpr uint64_t recipient_a = 1;
  constexpr uint64_t recipient_b = 2;
  constexpr uint64_t recipient_c = 3;
  constexpr uint64_t query = recipient_a;
  constexpr std::array<uint64_t, 4> recipients = {
      recipient_a, recipient_b, recipient_a, recipient_c};
  constexpr std::array<uint64_t, 4> locations = {11, 22, 33, 44};

  DPF dpf(64);
  std::vector<uint64_t> reconstructed;
  reconstructed.reserve(recipients.size());

  for (std::size_t i = 0; i < recipients.size(); ++i) {
    DPFKey keys[2];
    dpf.InitKey(keys);
    dpf.Gen(recipients[i], locations[i], keys);
    const uint64_t share0 = dpf.Eval(0, keys[0], query);
    const uint64_t share1 = dpf.Eval(1, keys[1], query);
    const uint64_t value = field61_add(share0, share1);
    if (value != 0) {
      reconstructed.push_back(value);
    }
    std::cout << "signal=" << (i + 1) << " recipient=" << recipients[i]
              << " location=" << locations[i] << " share0=" << share0
              << " share1=" << share1 << " additive_reconstruction=" << value
              << '\n';
    dpf.FreeKey(keys[0]);
    dpf.FreeKey(keys[1]);
  }

  auto actual = reconstructed;
  std::sort(actual.begin(), actual.end());
  const std::vector<uint64_t> expected = {11, 33};
  const bool ok = actual == expected;

  std::cout << "query_recipient=" << query << " expected={11,33} actual={";
  for (std::size_t i = 0; i < reconstructed.size(); ++i) {
    if (i != 0) std::cout << ',';
    std::cout << reconstructed[i];
  }
  std::cout << "}\ncurrent_artifact_end_to_end="
            << (ok ? "ok" : "failed_due_to_semantic_mismatch") << '\n';
  return ok ? 0 : 1;
}
