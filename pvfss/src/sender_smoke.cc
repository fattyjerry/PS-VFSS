#include "dpf.h"
#include "../../vdpf/include/field61.h"

#include <chrono>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

using Clock = std::chrono::steady_clock;
static volatile size_t prep_sink = 0;

static long long ns(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}

static void append(const std::string &path, int trial, const char *operation,
                   const char *warmup, long long elapsed, const std::string &bytes,
                   const char *bytes_status, const char *status) {
  bool header = !std::ifstream(path).good();
  std::ofstream out(path, std::ios::app);
  if (header) out << "timestamp,scheme,experiment,operation,role,trial,warmup,message_size_bytes,recipient_input_size,scheme_specific_parameters,wall_time_ns,output_bytes,output_bytes_status,status,git_commit,compiler,compiler_version,build_type,machine_id\n";
  char host[256] = {}; gethostname(host, sizeof(host) - 1);
  auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  out << now << ",PSVFSS,sender_signaling," << operation << ",sender," << trial << ',' << warmup
      << ",not_applicable,8,vdpf_domain_bits=64;security_bits=128," << elapsed << ',' << bytes << ','
      << bytes_status << ',' << status << ",unavailable,GCC," << __VERSION__
      << ",Release," << host << '\n'; out.flush();
}

static bool check(DPF &dpf, DPFKey *key, uint64_t alpha, uint64_t beta) {
  return (field61_add(dpf.Eval(0, key[0], alpha), dpf.Eval(1, key[1], alpha)) == beta) &&
         (field61_add(dpf.Eval(0, key[0], alpha + 1), dpf.Eval(1, key[1], alpha + 1)) == 0);
}

static void append_scaling(const std::string &path, int B, int trial, bool warm,
                           long long total, long long per, const char *status) {
  bool header = !std::ifstream(path).good(); std::ofstream out(path, std::ios::app);
  if (header) out << "timestamp,scheme,experiment,operation,role,B,trial,warmup,total_time_ns,per_signal_time_ns,scheme_specific_parameters,recipient_mode,beta_mode,status,compiler,compiler_version,build_type,machine_id,crypto_source_hash,benchmark_binary_hash\n";
  char host[256] = {}; gethostname(host, sizeof(host)-1);
  auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  out << now << ",PSVFSS,multiple_signal_sender_scalability,vergen,sender," << B << ',' << trial << ',' << (warm?"true":"false") << ',' << total << ',' << per << ",vdpf_domain_bits=64;security_bits=128,fixed_target_recipient,LocEnc(loc)=loc+1," << status << ",GCC," << __VERSION__ << ",Release," << host << ",8f7afe0edf5536c61a7a55b455ebaed6381faf2112efc1e43668477cfdf92ee1,benchmark_binary_hash_runtime\n"; out.flush();
}

static int run_scaling(const std::string &out_path, int B, int trials, int warmups) {
  DPF dpf(64); uint8_t context[48]; dpf.ExportContextKeys(context);
  const uint64_t alpha = 0x1020304050607000ULL;
  for (int tr=0; tr<warmups+trials; ++tr) {
    bool warm=tr<warmups; DPFKey last[2]; bool ok=true; auto a=Clock::now();
    for (int i=0;i<B;++i) {
      DPFKey keys[2]; uint64_t beta=field61_add(10+static_cast<uint64_t>(i),1);
      dpf.InitKey(keys); dpf.Gen(alpha,beta,keys);
      volatile size_t sink=keys[0].vdpf_key_len+keys[1].vdpf_key_len; (void)sink;
      if (i==B-1) { last[0]=keys[0]; last[1]=keys[1]; }
      else { dpf.FreeKey(keys[0]); dpf.FreeKey(keys[1]); }
    }
    auto z=Clock::now(); long long total=ns(a,z), per=total/B;
    uint64_t beta_last=field61_add(10+static_cast<uint64_t>(B-1),1);
    ok=check(dpf,last,alpha,beta_last); dpf.FreeKey(last[0]); dpf.FreeKey(last[1]);
    append_scaling(out_path,B,tr-warmups,warm,total,per,ok?"ok":"failed");
    if(!ok) return 1;
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc >= 2 && std::string(argv[1]) == "--mode") {
    if (argc != 11 || std::string(argv[2]) != "scaling") { std::cerr << "usage: --mode scaling --signals B --trials N --warmup N --output CSV\n"; return 2; }
    int B=0,trials=0,warmups=0; std::string out;
    for(int i=3;i<argc;i+=2) { std::string k=argv[i],v=argv[i+1]; if(k=="--signals")B=std::stoi(v); else if(k=="--trials")trials=std::stoi(v); else if(k=="--warmup")warmups=std::stoi(v); else if(k=="--output")out=v; else return 2; }
    return run_scaling(out,B,trials,warmups);
  }
  if (argc < 2 || argc > 4) { std::cerr << "usage: ps_vfss_sender_smoke CSV [trials] [warmup]\n"; return 2; }
  const int trials = argc >= 3 ? std::max(1, std::stoi(argv[2])) : 3;
  const int warmups = argc >= 4 ? std::max(0, std::stoi(argv[3])) : 1;
  constexpr int GEN_INNER_OPS = 100;
  constexpr int PREP_INNER_OPS = 1000;
  DPF dpf(64); // setup/public context: constructed once and reused across signals.
  uint8_t context[48]; dpf.ExportContextKeys(context);
  for (int trial = 0; trial < trials + warmups; ++trial) {
    const bool warm = trial < warmups;
    const uint64_t alpha = 0x1020304050607000ULL + static_cast<uint64_t>(trial);
    const uint64_t loc = 10 + static_cast<uint64_t>(trial);
    const uint64_t beta = field61_add(loc, 1); // LocEnc(loc)=loc+1 in Z_q^*
    DPFKey component[2];
    auto a = Clock::now();
    for (int inner = 0; inner < GEN_INNER_OPS; ++inner) {
      if (inner) { dpf.FreeKey(component[0]); dpf.FreeKey(component[1]); }
      dpf.InitKey(component); dpf.Gen(alpha + static_cast<uint64_t>(inner), beta, component);
    }
    auto b = Clock::now();
    std::vector<unsigned char> ca(component[0].vdpf_key, component[0].vdpf_key + component[0].vdpf_key_len);
    std::vector<unsigned char> cb(component[1].vdpf_key, component[1].vdpf_key + component[1].vdpf_key_len);
    auto c = Clock::now();
    for (int inner = 0; inner < PREP_INNER_OPS; ++inner) {
      std::vector<unsigned char> submission;
      submission.reserve(ca.size() + cb.size());
      submission.insert(submission.end(), ca.begin(), ca.end());
      submission.insert(submission.end(), cb.begin(), cb.end());
      prep_sink += submission.size();
    }
    auto d = Clock::now();
    bool ok_component = check(dpf, component, alpha + GEN_INNER_OPS - 1, beta) && ca.size() == component[0].vdpf_key_len && cb.size() == component[1].vdpf_key_len;
    if (!warm) {
      const int measured_trial = trial - warmups;
      append(argv[1], measured_trial, "ps_vfss_vergen", "false", ns(a,b)/GEN_INNER_OPS, std::to_string(ca.size()+cb.size()), "measured", ok_component?"ok":"failed");
      append(argv[1], measured_trial, "ps_vfss_sender_preparation", "false", ns(c,d)/PREP_INNER_OPS, std::to_string(ca.size()+cb.size()), "measured", ok_component?"ok":"failed");
      append(argv[1], measured_trial, "message_encryption", "false", -1, "", "not_applicable", "not_applicable");
    }
    dpf.FreeKey(component[0]); dpf.FreeKey(component[1]);

    // The artifact has no protocol-level fresh sid or key-share PKE.  Do not
    // relabel another VerGen as a complete sender total measurement.
    if (!warm) append(argv[1], trial - warmups, "ps_vfss_sender_total_signaling", "false", -1, "", "not_applicable", "incomplete");
    if (!ok_component) return 1;
  }
  std::cout << "PSVFSS sender smoke ok; context_setup_bytes=48; per_signal_bytes=2500\n";
}
