#include "include/regevEncryption.h"

#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

using Clock = std::chrono::steady_clock;

static void append(const std::string &path, int trial, bool warm, const char *op,
                   long long elapsed, const char *bytes_status, const char *status) {
    bool header = !std::ifstream(path).good(); std::ofstream out(path, std::ios::app);
    if (header) out << "timestamp,scheme,experiment,operation,role,trial,warmup,message_size_bytes,recipient_input_size,scheme_specific_parameters,wall_time_ns,output_bytes,output_bytes_status,status,git_commit,compiler,compiler_version,build_type,machine_id\n";
    char host[256] = {}; gethostname(host, sizeof(host)-1);
    auto ts=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    out << ts << ",OMR,sender_signaling," << op << ",sender," << trial << ',' << (warm?"true":"false")
        << ",not_applicable,unclear,PVW_n=450;PVW_q=65537;PVW_stddev=1.3;PVW_m=16000;PVW_ell=4,"
        << elapsed << ",," << bytes_status << ',' << status << ",unavailable,GCC," << __VERSION__
        << ",Release," << host << '\n'; out.flush();
}

static void append_scale(const std::string &path,int B,int trial,bool warm,long long total,long long per,const char *status){
 bool header=!std::ifstream(path).good(); std::ofstream out(path,std::ios::app); if(header) out<<"timestamp,scheme,experiment,operation,role,B,trial,warmup,total_time_ns,per_signal_time_ns,scheme_specific_parameters,recipient_mode,status,session_id,session_start_time\n"; char host[256]={};gethostname(host,sizeof(host)-1); auto ts=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); const char *sid=std::getenv("OMR_SESSION_ID"); const char *sst=std::getenv("OMR_SESSION_START"); out<<ts<<",OMR,multiple_signal_sender_scalability,clue_generation,sender,"<<B<<","<<trial<<","<<(warm?"true":"false")<<","<<total<<","<<per<<",PVW_n=450;PVW_q=65537;PVW_stddev=1.3;PVW_m=16000;PVW_ell=4,fixed_target_recipient,"<<status<<","<<(sid?sid:"unknown")<<","<<(sst?sst:"unknown")<<"\n";out.flush();
}

int main(int argc, char **argv) {
    if (argc >= 2 && std::string(argv[1]) == "--mode") {
        if (argc != 11 || std::string(argv[2]) != "scaling") { std::cerr << "usage: --mode scaling --signals B --trials N --warmup N --output CSV\n"; return 2; }
        int B=0,trials=0,warmups=0; std::string out; for(int i=3;i<argc;i+=2){std::string k=argv[i],v=argv[i+1];if(k=="--signals")B=std::atoi(v.c_str());else if(k=="--trials")trials=std::atoi(v.c_str());else if(k=="--warmup")warmups=std::atoi(v.c_str());else if(k=="--output")out=v;else return 2;}
        const PVWParam params(450,65537,1.3,16000,4); std::srand(static_cast<unsigned>(std::time(nullptr))); const PVWsk sk=PVWGenerateSecretKey(params); const PVWpk pk=PVWGeneratePublicKey(params,sk); const std::vector<int> zeros(params.ell,0);
        for(int tr=0;tr<warmups+trials;tr++){bool warm=tr<warmups;PVWCiphertext last;auto a=Clock::now();for(int i=0;i<B;i++){PVWCiphertext clue;PVWEncPK(clue,zeros,pk,params);last=clue;}auto b=Clock::now();std::vector<int> decoded;PVWDec(decoded,last,sk,params);bool ok=decoded==zeros;append_scale(out,B,tr-warmups,warm,std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count(),std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count()/B,ok?"ok":"failed");if(!ok)return 1;}
        return 0;
    }
    if (argc < 2 || argc > 4) { std::cerr << "usage: omr_sender_smoke CSV [trials] [warmup]\n"; return 2; }
    int trials = argc >= 3 ? std::max(1, std::atoi(argv[2])) : 3;
    int warmups = argc >= 4 ? std::max(0, std::atoi(argv[3])) : 1;
    const PVWParam params(450,65537,1.3,16000,4); std::srand(static_cast<unsigned>(std::time(nullptr)));
    const PVWsk sk=PVWGenerateSecretKey(params); const PVWpk pk=PVWGeneratePublicKey(params,sk);
    const std::vector<int> zeros(params.ell,0);
    for(int trial=0;trial<trials+warmups;trial++) { bool warm=trial<warmups; int measured=trial-warmups;
        PVWCiphertext clue; auto a=Clock::now(); PVWEncPK(clue,zeros,pk,params); auto b=Clock::now();
        std::vector<int> decoded; PVWDec(decoded,clue,sk,params); bool ok=decoded==zeros;
        append(argv[1],measured,warm,"omr_clue_generation",std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count(),"unclear",ok?"ok":"failed");
        if(!warm) { append(argv[1],measured,false,"omr_sender_preparation",-1,"unclear","unclear"); append(argv[1],measured,false,"message_encryption",-1,"not_applicable","not_applicable"); }
        PVWCiphertext total; auto t0=Clock::now(); PVWEncPK(total,zeros,pk,params); auto t1=Clock::now();
        decoded.clear(); PVWDec(decoded,total,sk,params); bool total_ok=decoded==zeros;
        if (!warm) append(argv[1],measured,false,"omr_sender_total_signaling",std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count(),"unclear",total_ok?"ok":"failed");
        if(!ok||!total_ok) return 1;
    }
    std::cout << "OMR sender smoke ok; server_retrieval=false; file_io=false; output_bytes=unclear\n";
}
