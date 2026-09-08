#include "dpf.h"
#include "../../vdpf/include/field61.h"
#include <cstdint>
#include <iostream>

int main() {
  constexpr uint64_t alpha = 0x12345678ULL;
  const uint64_t betas[] = {1, 2, 17, 0x1234};
  DPF dpf(64);
  bool all = true;
  for (uint64_t beta : betas) {
    DPFKey keys[2]; dpf.InitKey(keys); dpf.Gen(alpha, beta, keys);
    uint64_t a0=dpf.Eval(0,keys[0],alpha), a1=dpf.Eval(1,keys[1],alpha);
    uint64_t n0=dpf.Eval(0,keys[0],alpha+1), n1=dpf.Eval(1,keys[1],alpha+1);
    uint64_t target=field61_add(a0,a1), non_target=field61_add(n0,n1);
    bool ok=target==beta && non_target==0;
    std::cout << "beta=" << beta << " share0=" << a0 << " share1=" << a1
              << " reconstructed_target=" << target
              << " reconstructed_non_target=" << non_target
              << " expected=" << beta << " status=" << (ok?"ok":"failed") << '\n';
    all &= ok; dpf.FreeKey(keys[0]); dpf.FreeKey(keys[1]);
  }
  return all ? 0 : 1;
}
