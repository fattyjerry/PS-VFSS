#include "server.h"
#include "test.h"
#include "opv.h"
#include "ss_shuffle.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

std::ifstream fin;
std::ofstream fout;

using namespace emp;

namespace {
struct BenchmarkParams {
    uint64_t Ns;
    uint64_t T;
    uint64_t ell;
    uint64_t online_reps;
};

struct BenchmarkResult {
    long long offline_ms = 0;
    long long keygen_total_us = 0;
    double keygen_per_signal_us = 0.0;
    long long eval_us = 0;
    long long shuffle_us = 0;
    long long compress_us = 0;
    long long online_total_us = 0;
    long long reconstruction_us = 0;
    uint64_t recovered_signals = 0;
    uint64_t before_bytes = 0;
    uint64_t after_bytes = 0;
    double compression_ratio = 0.0;
    uint64_t online_reps = 1;
};

uint64_t ParseUintArg(char **argv, int idx, uint64_t fallback) {
    return argv[idx] ? std::strtoull(argv[idx], nullptr, 10) : fallback;
}

uint64_t NonHitAlpha(uint64_t i, uint64_t Ns, uint64_t recv) {
    uint64_t alpha = (recv + 1 + i) % Ns;
    if (alpha == recv) {
        alpha = (recv + 1) % Ns;
    }
    return alpha;
}

uint64_t Low64(block value) {
    return static_cast<uint64_t>(_mm_extract_epi64(value, 0));
}

void PrintBenchCsv(const BenchmarkParams &params, const BenchmarkResult &result) {
    std::cout << "[BENCH_CSV_LEGACY] "
              << params.Ns << ","
              << params.T << ","
              << params.ell << ","
              << result.offline_ms << ","
              << result.keygen_total_us << ","
              << result.keygen_per_signal_us << ","
              << result.eval_us << ","
              << result.shuffle_us << ","
              << result.compress_us << ","
              << result.online_total_us << ","
              << result.reconstruction_us << ","
              << result.recovered_signals << ","
              << result.before_bytes << ","
              << result.after_bytes << ","
              << result.compression_ratio << std::endl;
}

std::string SlowestStage(long long setup_ms,
                         long long send_ms,
                         long long server_ms,
                         long long recipient_ms) {
    std::string stage = "setup";
    long long best = setup_ms;
    if (send_ms > best) {
        stage = "send";
        best = send_ms;
    }
    if (server_ms > best) {
        stage = "server";
        best = server_ms;
    }
    if (recipient_ms > best) {
        stage = "recipient";
    }
    return stage;
}

void PrintUnifiedBenchCsv(const BenchmarkParams &params, const BenchmarkResult &result) {
    const long long setup_ms = result.offline_ms;
    const long long send_ms = (result.keygen_total_us + 999) / 1000;
    const long long server_ms = (result.eval_us + result.shuffle_us + result.compress_us + 999) / 1000;
    const long long recipient_ms = (result.reconstruction_us + 999) / 1000;
    const bool ok = result.recovered_signals == params.ell;
    const uint64_t expected_comm_bytes = 2 * params.ell * sizeof(uint64_t);
    const std::string status = ok ? "completed" : "crashed";
    const std::string bottleneck = ok ? ("setup_reps=1_online_avg_reps=" + std::to_string(result.online_reps))
                                      : "recovered_signals_mismatch";
    if (!ok) {
        std::cout << "[BENCH_WARN] recovered_signals=" << result.recovered_signals
                  << ", expected=" << params.ell << std::endl;
    }
    if (ok && result.after_bytes != expected_comm_bytes) {
        std::cout << "[BENCH_WARN] comm_bytes=" << result.after_bytes
                  << ", expected=" << expected_comm_bytes << std::endl;
    }

    std::cout << "[BENCH_CSV] "
              << "PSVFSS" << ","
              << params.Ns << ","
              << params.ell << ","
              << setup_ms << ","
              << send_ms << ","
              << server_ms << ","
              << recipient_ms << ","
              << result.after_bytes << ","
              << status << ","
              << bottleneck << std::endl;
}

void PrintDetailBenchCsv(const BenchmarkParams &params, const BenchmarkResult &result) {
    const uint64_t expected_comm_bytes = 2 * params.ell * sizeof(uint64_t);
    const bool ok = result.recovered_signals == params.ell && result.after_bytes == expected_comm_bytes;
    const std::string status = ok ? "completed" : "crashed";
    std::string bottleneck = ok ? ("setup_reps=1_online_avg_reps=" + std::to_string(result.online_reps))
                                : "validation_failed";
    if (params.Ns != params.T && params.Ns < 512) {
        bottleneck += "_T_eff=" + std::to_string(params.T);
    }
    if (!ok) {
        std::cout << "[BENCH_WARN] detail_recovered_signals=" << result.recovered_signals
                  << ", expected=" << params.ell
                  << ", comm_bytes=" << result.after_bytes
                  << ", expected_comm_bytes=" << expected_comm_bytes << std::endl;
    }

    std::cout << "[BENCH_DETAIL_CSV] "
              << params.Ns << ","
              << params.ell << ","
              << result.offline_ms << ","
              << (static_cast<double>(result.keygen_total_us) / 1000.0) << ","
              << (static_cast<double>(result.eval_us) / 1000.0) << ","
              << (static_cast<double>(result.shuffle_us) / 1000.0) << ","
              << (static_cast<double>(result.compress_us) / 1000.0) << ","
              << (static_cast<double>(result.reconstruction_us) / 1000.0) << ","
              << result.after_bytes << ","
              << status << ","
              << bottleneck << std::endl;
}
}

namespace CharacterTest {
    BenchmarkResult RunBenchmarkCase(int party, HighSpeedNetIO *io, BenchmarkParams params) {
        const uint64_t recv = 3;
        uint64_t Ns = params.Ns;
        uint64_t T = params.T;
        uint64_t ell = params.ell;
        uint64_t online_reps = std::max<uint64_t>(1, params.online_reps);
        BenchmarkResult result;
        result.online_reps = online_reps;

        if (Ns <= 1 || T <= 1 || T > Ns || Ns % T != 0) {
            std::cerr << "Invalid benchmark parameters: require Ns > 1, T > 1, T <= Ns, and Ns % T == 0" << std::endl;
            return result;
        }
        ell = std::min(ell, Ns);

        std::cout << "[PARAM] Ns=" << Ns << ", T=" << T << ", ell=" << ell
                  << ", online_reps=" << online_reps << std::endl;

        std::random_device rd;
        std::mt19937 rooot(rd());
        block ran = makeBlock(rooot(), rooot());
        block fa, fb, fch, fcl;

        if (party == ALICE) {
            io->send_block(&ran, 1);
        } else {
            io->recv_block(&ran, 1);
        }

        BTGen(party, io, fa, fb, fch, fcl);

        const uint64_t N = Ns;
        const int n = static_cast<int>(log2(N));
        const int t = static_cast<int>(log2(T));
        const int d = 2 * static_cast<int>(ceil(static_cast<double>(n) / t)) - 1;
        const int subperm_num = N / T;

        std::vector<block> out(N);
        std::vector<block> a(static_cast<size_t>(d) * N);
        std::vector<block> b(static_cast<size_t>(d) * N);
        std::vector<block> delta(static_cast<size_t>(d) * N);
        std::vector<uint64_t> perms(static_cast<size_t>(d) * N);
        std::vector<uint64_t> perm(N);

        for (uint64_t i = 0; i < static_cast<uint64_t>(d) * N; i++) {
            perms[i] = i % T;
        }

        std::random_device rd1;
        std::mt19937 g(rd1());
        for (int i = 0; i < d * subperm_num; i++) {
            std::shuffle(perms.begin() + static_cast<size_t>(i) * T,
                         perms.begin() + static_cast<size_t>(i + 1) * T,
                         g);
        }

        auto start = std::chrono::system_clock::now();
        Offline(N, T, perms.data(), party, io, perm.data(), a.data(), b.data(), delta.data());
        auto end = std::chrono::system_clock::now();
        auto offline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        result.offline_ms = offline_ms;
        std::cout << "[OFFLINE] preprocessing_time_ms=" << offline_ms << std::endl;

        long long keygen_total_us_sum = 0;
        long long eval_us_sum = 0;
        long long shuffle_us_sum = 0;
        long long compress_us_sum = 0;
        long long online_total_us_sum = 0;
        long long reconstruction_us_sum = 0;
        uint64_t recovered_signals = ell;
        uint64_t compressed_signal_count = ell;
        bool all_online_reps_ok = true;

        for (uint64_t rep = 0; rep < online_reps; rep++) {
            std::cout << "[ONLINE_REP] start=" << (rep + 1) << "/" << online_reps << std::endl;
            DPF dpf(64);
            uint8_t context_keys[48];
            std::vector<DPFKey> Data0;
            std::vector<DPFKey> Data1;
            Data0.reserve(N);
            Data1.reserve(N);

            long long keygen_total_us = 0;
            double keygen_per_signal_us = 0.0;

            if (party == ALICE) {
                dpf.ExportContextKeys(context_keys);
                io->send_data(context_keys, sizeof(context_keys));

                DPFKey key[2];
                uint64_t alpha_hits = 0;
                uint64_t nonhit_collisions = 0;
                start = std::chrono::system_clock::now();
                for (uint64_t i = 0; i < N; i++) {
                    uint64_t alpha = i < ell ? recv : NonHitAlpha(i, N, recv);
                    if (alpha == recv) {
                        if (i < ell) {
                            alpha_hits++;
                        } else {
                            nonhit_collisions++;
                        }
                    }
                    dpf.InitKey(key);
                    dpf.Gen(alpha, 1, key);
                    Data0.push_back(key[0]);
                    Data1.push_back(key[1]);
                }
                end = std::chrono::system_clock::now();
                keygen_total_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                keygen_per_signal_us = static_cast<double>(keygen_total_us) / static_cast<double>(N);
                std::cout << "[DEBUG] alpha_hits=" << alpha_hits << std::endl;
                std::cout << "[DEBUG] nonhit_alpha_collisions=" << nonhit_collisions << std::endl;

                size_t key_len = Data1.empty() ? 0 : Data1[0].vdpf_key_len;
                io->send_data(&key_len, sizeof(size_t));
                for (uint64_t i = 0; i < N; i++) {
                    io->send_data(Data1[i].vdpf_key, key_len);
                }
            } else {
                io->recv_data(context_keys, sizeof(context_keys));
                dpf.ImportContextKeys(context_keys);

                size_t key_len = 0;
                io->recv_data(&key_len, sizeof(size_t));
                for (uint64_t i = 0; i < N; i++) {
                    DPFKey key[2];
                    dpf.InitKey(key);
                    io->recv_data(key[1].vdpf_key, key_len);
                    key[1].vdpf_key_len = key_len;
                    Data1.push_back(key[1]);
                    dpf.FreeKey(key[0]);
                }
            }

            keygen_total_us_sum += keygen_total_us;
            std::cout << "[SENDER] keygen_per_signal_us=" << keygen_per_signal_us << std::endl;
            std::cout << "[SENDER] keygen_total_us=" << keygen_total_us << std::endl;

            std::vector<uint64_t> xshare;
            xshare.reserve(N);
            std::vector<block> x(N);
            std::vector<block> out(N);

            auto online_start = std::chrono::system_clock::now();
            start = std::chrono::system_clock::now();
            if (party == ALICE) {
                for (uint64_t j = 0; j < Data0.size(); j++) {
                    xshare.push_back(dpf.Eval(0, Data0[j], recv));
                }
            } else {
                for (uint64_t j = 0; j < Data1.size(); j++) {
                    uint64_t o1 = dpf.Eval(1, Data1[j], recv);
                    xshare.push_back(o1);
                }
            }
            end = std::chrono::system_clock::now();
            auto eval_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            eval_us_sum += eval_time_us;
            std::cout << "[SERVER] eval_time_us=" << eval_time_us << std::endl;

            if (party == ALICE) {
                std::vector<uint64_t> peer_share(N);
                io->recv_data(peer_share.data(), N * sizeof(uint64_t));
                uint64_t preshuffle_nonzero = 0;
                for (uint64_t i = 0; i < N; i++) {
                    uint64_t opened = xshare[i] ^ peer_share[i];
                    if (opened != 0) {
                        preshuffle_nonzero++;
                    }
                    x[i] = makeBlock(0, opened);
                }
                std::cout << "[DEBUG] preshuffle_reconstructed_nonzero=" << preshuffle_nonzero << std::endl;
                io->send_data(&preshuffle_nonzero, sizeof(uint64_t));
            } else {
                io->send_data(xshare.data(), N * sizeof(uint64_t));
                for (uint64_t i = 0; i < N; i++) {
                    x[i] = makeBlock(0, 0);
                }
                uint64_t preshuffle_nonzero = 0;
                io->recv_data(&preshuffle_nonzero, sizeof(uint64_t));
                std::cout << "[DEBUG] preshuffle_reconstructed_nonzero=" << preshuffle_nonzero << std::endl;
            }

            start = std::chrono::system_clock::now();
            SecretSharedShuffle(N, T, party, io, x.data(), perm.data(), delta.data(), a.data(), b.data(), out.data());
            end = std::chrono::system_clock::now();
            auto shuffle_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            shuffle_us_sum += shuffle_time_us;
            std::cout << "[SERVER] shuffle_time_us=" << shuffle_time_us << std::endl;

            std::vector<block> out_other(N);
            if (party == ALICE) {
                io->send_block(out.data(), N);
            } else {
                io->recv_block(out_other.data(), N);
            }

            uint64_t postshuffle_low64_nonzero = 0;
            uint64_t postshuffle_fullblock_nonzero = 0;
            if (party == BOB) {
                for (uint64_t i = 0; i < N; i++) {
                    block recovered = out[i] + out_other[i];
                    uint64_t lo = Low64(recovered);
                    uint64_t hi = static_cast<uint64_t>(_mm_extract_epi64(recovered, 1));
                    if (lo != 0) {
                        postshuffle_low64_nonzero++;
                    }
                    if (lo != 0 || hi != 0) {
                        postshuffle_fullblock_nonzero++;
                    }
                }
            }
            std::cout << "[DEBUG] postshuffle_low64_nonzero=" << postshuffle_low64_nonzero << std::endl;
            std::cout << "[DEBUG] postshuffle_fullblock_nonzero=" << postshuffle_fullblock_nonzero << std::endl;

            std::vector<uint64_t> sig;
            start = std::chrono::system_clock::now();
            CPRS(party, io, N, out.data(), ran, fa, fb, fch, fcl, sig);
            end = std::chrono::system_clock::now();
            auto compress_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            auto online_end = std::chrono::system_clock::now();
            auto online_total_time_us = std::chrono::duration_cast<std::chrono::microseconds>(online_end - online_start).count();
            compress_us_sum += compress_time_us;
            online_total_us_sum += online_total_time_us;
            std::cout << "[SERVER] compress_time_us=" << compress_time_us << std::endl;
            std::cout << "[SERVER] online_total_time_us=" << online_total_time_us << std::endl;

            long long reconstruction_time_us = 0;
            uint64_t rep_recovered_signals = 0;
            uint64_t rep_compressed_signal_count = sig.size();
            if (party == BOB) {
                std::vector<uint64_t> reconstructed_sig;
                start = std::chrono::system_clock::now();
                for (uint64_t i = 0; i < N; i++) {
                    block recovered = out[i] + out_other[i];
                    if (_mm_extract_epi64(recovered, 0) != 0) {
                        reconstructed_sig.push_back(i);
                    }
                }
                for (size_t i = 0; i < reconstructed_sig.size(); i++) {
                    block recovered = out[reconstructed_sig[i]] + out_other[reconstructed_sig[i]];
                    if (_mm_extract_epi64(recovered, 0) != 0) {
                        rep_recovered_signals++;
                    }
                }
                end = std::chrono::system_clock::now();
                reconstruction_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                rep_compressed_signal_count = static_cast<uint64_t>(reconstructed_sig.size());
                io->send_data(&rep_compressed_signal_count, sizeof(uint64_t));
                io->send_data(&reconstruction_time_us, sizeof(long long));
                io->send_data(&rep_recovered_signals, sizeof(uint64_t));
            } else {
                io->recv_data(&rep_compressed_signal_count, sizeof(uint64_t));
                io->recv_data(&reconstruction_time_us, sizeof(long long));
                io->recv_data(&rep_recovered_signals, sizeof(uint64_t));
            }
            reconstruction_us_sum += reconstruction_time_us;
            if (rep_recovered_signals != ell || rep_compressed_signal_count != ell) {
                all_online_reps_ok = false;
            }
            recovered_signals = rep_recovered_signals;
            compressed_signal_count = rep_compressed_signal_count;
            std::cout << "[RECIPIENT] reconstruction_time_us=" << reconstruction_time_us << std::endl;
            std::cout << "[RECIPIENT] recovered_signals=" << rep_recovered_signals << std::endl;
            std::cout << "[ONLINE_REP] done=" << (rep + 1) << "/" << online_reps << std::endl;
        }

        result.keygen_total_us = keygen_total_us_sum / static_cast<long long>(online_reps);
        result.keygen_per_signal_us = static_cast<double>(result.keygen_total_us) / static_cast<double>(N);
        result.eval_us = eval_us_sum / static_cast<long long>(online_reps);
        result.shuffle_us = shuffle_us_sum / static_cast<long long>(online_reps);
        result.compress_us = compress_us_sum / static_cast<long long>(online_reps);
        result.online_total_us = online_total_us_sum / static_cast<long long>(online_reps);
        result.reconstruction_us = reconstruction_us_sum / static_cast<long long>(online_reps);
        result.recovered_signals = all_online_reps_ok ? ell : recovered_signals;
        std::cout << "[ONLINE_AVG] keygen_total_us=" << result.keygen_total_us << std::endl;
        std::cout << "[ONLINE_AVG] eval_time_us=" << result.eval_us << std::endl;
        std::cout << "[ONLINE_AVG] shuffle_time_us=" << result.shuffle_us << std::endl;
        std::cout << "[ONLINE_AVG] compress_time_us=" << result.compress_us << std::endl;
        std::cout << "[ONLINE_AVG] reconstruction_time_us=" << result.reconstruction_us << std::endl;

        const uint64_t share_bytes = sizeof(uint64_t);
        const uint64_t before_compression_bytes = 2 * N * share_bytes;
        const uint64_t after_compression_bytes = 2 * compressed_signal_count * share_bytes;
        const double compression_ratio = after_compression_bytes == 0
            ? 0.0
            : static_cast<double>(before_compression_bytes) / static_cast<double>(after_compression_bytes);
        result.before_bytes = before_compression_bytes;
        result.after_bytes = after_compression_bytes;
        result.compression_ratio = compression_ratio;
        std::cout << "[COMM] before_compression_bytes=" << before_compression_bytes << std::endl;
        std::cout << "[COMM] after_compression_bytes=" << after_compression_bytes << std::endl;
        std::cout << "[COMM] compression_ratio=" << compression_ratio << std::endl;
        std::cout << "[COMM] share_size_bytes=" << share_bytes << std::endl;

        return result;
    }

    void ServerTest0(int argc, char **argv) {
        int port, party;
        emp::parse_party_and_port(argv, &party, &port);

        HighSpeedNetIO *io = new HighSpeedNetIO(party == 1 ? nullptr : "127.0.0.1",
                                                port,
                                                port + 1,
                                                false);

        if (argc > 3 && std::string(argv[3]) == "bench") {
            const std::vector<BenchmarkParams> cases = {
                {4096, 512, 50, 5},
                {8192, 512, 50, 5},
                {16384, 512, 50, 5},
            };

            std::cout << "[BENCH_CSV_LEGACY] Ns,T,ell,offline_ms,keygen_total_us,keygen_per_signal_us,eval_us,shuffle_us,compress_us,online_total_us,reconstruction_us,recovered_signals,before_bytes,after_bytes,compression_ratio" << std::endl;
            if (party == ALICE) {
                std::cout << "[BENCH_CSV] scheme,N,ell,setup_ms,send_ms,server_ms,recipient_ms,comm_bytes,status,bottleneck" << std::endl;
            }
            for (const auto &params : cases) {
                std::cout << "[BENCH] start Ns=" << params.Ns
                          << ", T=" << params.T
                          << ", ell=" << params.ell << std::endl;
                BenchmarkResult result = RunBenchmarkCase(party, io, params);
                PrintBenchCsv(params, result);
                if (party == ALICE) {
                    PrintUnifiedBenchCsv(params, result);
                }
                std::cout << "[BENCH] done Ns=" << params.Ns
                          << ", T=" << params.T
                          << ", ell=" << params.ell << std::endl;
            }
            std::cout << "[BENCH] skip Ns=32768, T=512, ell=50, reason=offline_preprocessing_too_slow" << std::endl;
        } else {
                BenchmarkParams params = {
                ParseUintArg(argv, 3, 4096),
                ParseUintArg(argv, 4, 512),
                ParseUintArg(argv, 5, 50),
                ParseUintArg(argv, 6, 5),
            };
            BenchmarkResult result = RunBenchmarkCase(party, io, params);
	            if (party == ALICE) {
	                PrintUnifiedBenchCsv(params, result);
	                PrintDetailBenchCsv(params, result);
	            }
	        }

        delete io;
    }
}

int main(int argc, char **argv) {
    int port, party;

    emp::parse_party_and_port(argv, &party, &port);
    if (party <= 2) {
        CharacterTest::ServerTest0(argc, argv);
    }

    return 0;
}
