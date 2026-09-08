#include "dpf.h"
#include "../../vdpf/include/field61.h"
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "share_algebra_diag.log";
  std::ofstream out(path);
  DPF dpf(64);
  std::map<std::pair<uint64_t,uint64_t>,uint64_t> target_pairs, non_target_pairs;
  std::map<uint64_t,uint64_t> target_xor,target_add,non_target_xor,non_target_add;
  uint64_t additive_failures=0;
  for(uint64_t i=0;i<1000;i++) {
    uint64_t alpha=0x100000000ULL+i*2; DPFKey keys[2]; dpf.InitKey(keys); dpf.Gen(alpha,1,keys);
    uint64_t t0=dpf.Eval(0,keys[0],alpha),t1=dpf.Eval(1,keys[1],alpha);
    uint64_t n0=dpf.Eval(0,keys[0],alpha+1),n1=dpf.Eval(1,keys[1],alpha+1);
    target_pairs[{t0,t1}]++; non_target_pairs[{n0,n1}]++;
    target_xor[t0^t1]++; target_add[field61_add(t0,t1)]++; non_target_xor[n0^n1]++; non_target_add[field61_add(n0,n1)]++;
    if(field61_add(t0,t1)!=1 || field61_add(n0,n1)!=0) additive_failures++;
    dpf.FreeKey(keys[0]); dpf.FreeKey(keys[1]);
  }
  auto printmap=[&](const char *name,const auto &m){ out<<name; for(auto &e:m) out<<" "<<e.first<<":"<<e.second; out<<"\n"; };
  auto printpairs=[&](const char *name,const auto &m){ out<<name; for(auto &e:m) out<<" ("<<e.first.first<<","<<e.first.second<<"):"<<e.second; out<<"\n"; };
  out<<"independent_keys=1000\n"; printpairs("target_pairs",target_pairs); printmap("target_xor",target_xor); printmap("target_add",target_add);
  printpairs("non_target_pairs",non_target_pairs); printmap("non_target_xor",non_target_xor); printmap("non_target_add",non_target_add);
  out<<"additive_failures="<<additive_failures<<"\nprotocol_blocking_mismatch="<<(additive_failures?"yes":"no")<<"\n";
  out.close(); std::ifstream in(path); std::cout<<in.rdbuf(); return additive_failures?1:0;
}
