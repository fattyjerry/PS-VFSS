#include "server.h"
#include "test.h"
#include "opv.h"
#include "ss_shuffle.h"
#include "../../vdpf/include/field61.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
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
    bool local_trusted_preprocessing = false;
    bool component_pilot = false;
    uint64_t registered_users = 0;
};

struct BenchmarkResult {
    long long offline_ms = 0;
    long long keygen_total_us = 0;
    long long serialization_us = 0;
    double keygen_per_signal_us = 0.0;
    long long eval_us = 0;
    long long eval_core_us = 0;
    long long proof_check_us = 0;
    long long shuffle_us = 0;
    long long compress_us = 0;
    long long online_total_us = 0;
    long long reconstruction_us = 0;
    double recipient_processing_ns = 0.0;
    uint64_t recipient_inner_ops = 0;
    long long response_serialization_us = 0;
    long long admission_us = 0;
    uint64_t server0_response_bytes = 0;
    uint64_t server1_response_bytes = 0;
    uint64_t server_to_recipient_bytes = 0;
    uint64_t recovered_signals = 0;
    uint64_t diagnostic_bytes = 0;
    bool proof_verified = false;
    bool exact_locations_ok = false;
    bool cprs_indices_match = false;
    std::vector<uint64_t> expected_locations;
    std::vector<uint64_t> recovered_locations;
    std::vector<uint64_t> missing_locations;
    std::vector<uint64_t> unexpected_locations;
    uint64_t online_reps = 1;
};

constexpr uint64_t kCorrectnessSeed = UINT64_C(0x5053564653533341);

std::vector<uint64_t> MultisetDifference(const std::vector<uint64_t> &left,
                                         const std::vector<uint64_t> &right) {
    std::vector<uint64_t> out;
    std::set_difference(left.begin(), left.end(), right.begin(), right.end(),
                        std::back_inserter(out));
    return out;
}

void PrintLocations(const char *name, const std::vector<uint64_t> &values) {
    std::cout << "[CORRECTNESS] " << name << "={";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) std::cout << ',';
        std::cout << values[i];
    }
    std::cout << "}" << std::endl;
}

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

std::vector<uint8_t> SerializeFieldShares(const std::vector<uint64_t> &shares) {
    std::vector<uint8_t> buffer(4 + shares.size() * sizeof(uint64_t));
    const uint32_t count = static_cast<uint32_t>(shares.size());
    for (int i = 0; i < 4; ++i) buffer[i] = static_cast<uint8_t>(count >> (8 * i));
    for (size_t j = 0; j < shares.size(); ++j) {
        for (int i = 0; i < 8; ++i) {
            buffer[4 + j * 8 + i] = static_cast<uint8_t>(shares[j] >> (8 * i));
        }
    }
    return buffer;
}

std::vector<uint64_t> DeserializeFieldShares(const std::vector<uint8_t> &buffer) {
    if (buffer.size() < 4) throw std::runtime_error("truncated PSVFSS response");
    uint32_t count = 0;
    for (int i = 0; i < 4; ++i) count |= uint32_t(buffer[i]) << (8 * i);
    if (buffer.size() != 4 + size_t(count) * 8) {
        throw std::runtime_error("invalid PSVFSS response length");
    }
    std::vector<uint64_t> shares(count);
    for (size_t j = 0; j < count; ++j) {
        for (int i = 0; i < 8; ++i) shares[j] |= uint64_t(buffer[4 + j * 8 + i]) << (8 * i);
    }
    return shares;
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
              << result.recovered_signals << ",response_unresolved" << std::endl;
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
    std::cout << "[PSVFSS_CORRECTNESS] Ns=" << params.Ns
              << " k_actual=" << params.ell
              << " fixed_seed=" << kCorrectnessSeed
              << " proof_verified=" << (result.proof_verified ? "true" : "false")
              << " exact_locations=" << (result.exact_locations_ok ? "true" : "false")
              << " cprs_indices_match=" << (result.cprs_indices_match ? "true" : "false")
              << " status=" << (result.proof_verified && result.exact_locations_ok && result.cprs_indices_match ? "ok" : "failed")
              << std::endl;
    std::cout << "[PSVFSS_RESPONSE] status=implemented server0_bytes="
              << result.server0_response_bytes
              << " server1_bytes=" << result.server1_response_bytes
              << " server_to_recipient_bytes=" << result.server_to_recipient_bytes
              << std::endl;
}

void PrintDetailBenchCsv(const BenchmarkParams &params, const BenchmarkResult &result) {
    (void)params;
    std::cout << "[DIAGNOSTIC_ONLY] full_vector_bytes=" << result.diagnostic_bytes
              << " excluded_from_timers=true excluded_from_response=true" << std::endl;
}

void PrintClientBenchJson(const BenchmarkParams &params, const BenchmarkResult &result) {
    const bool ok = result.proof_verified && result.exact_locations_ok && result.cprs_indices_match;
    std::cout << "[CLIENT_BENCH_JSON] {"
              << "\"scheme\":\"PSVFSS\","
              << "\"Ns\":" << params.Ns << ","
              << "\"Nr\":null,"
              << "\"actual_k\":" << params.ell << ","
              << "\"observed_result_count\":" << result.recovered_signals << ","
              << "\"sender_signaling_generation_ns\":" << result.keygen_total_us * 1000 << ","
              << "\"sender_serialization_ns\":" << result.serialization_us * 1000 << ","
              << "\"sender_total_online_ns\":" << (result.keygen_total_us + result.serialization_us) * 1000 << ","
              << "\"recipient_processing_ns\":" << result.recipient_processing_ns << ","
              << "\"recipient_inner_ops\":" << result.recipient_inner_ops << ","
              << "\"correctness\":" << (ok ? "true" : "false") << ","
              << "\"output_type\":\"diagnostic_location_multiset\","
              << "\"error_class\":\"" << (ok ? "" : "recovered_result_mismatch") << "\","
              << "\"scheme_specific_parameters\":\"vdpf_bit_length=64;T=" << params.T
              << ";Nr=not_exposed;response_status=implemented\"}" << std::endl;
}
}

namespace CharacterTest {
    BenchmarkResult RunBenchmarkCase(int party, int base_port, NetIO *io,
                                     BenchmarkParams params) {
        const uint64_t recv = 3;
        uint64_t Ns = params.Ns;
        uint64_t T = params.T;
        uint64_t ell = params.ell;
        uint64_t online_reps = std::max<uint64_t>(1, params.online_reps);
        const uint64_t registered_users = params.registered_users;
        BenchmarkResult result;
        result.online_reps = online_reps;

        if (Ns <= 1 || T <= 1 || T > Ns || Ns % T != 0) {
            std::cerr << "Invalid benchmark parameters: require Ns > 1, T > 1, T <= Ns, and Ns % T == 0" << std::endl;
            return result;
        }
        ell = std::min(ell, Ns);

        std::cout << "[PARAM] Ns=" << Ns << ", T=" << T << ", ell=" << ell
                  << ", Nr=" << registered_users
                  << ", online_reps=" << online_reps << std::endl;

        std::mt19937_64 deterministic_rng(kCorrectnessSeed);
        block ran = makeBlock(deterministic_rng(), deterministic_rng());
        block fa, fb, fch, fcl;

        if (party == ALICE) {
            io->send_block(&ran, 1);
            fflush(io->stream);
            io->flush();
        } else {
            io->recv_block(&ran, 1);
            fflush(io->stream);
            io->flush();
        }

        // The main channel is unbuffered and all BTGen frames use NetIO.  Keep
        // the full ordered protocol on this single connection so a second
        // listener cannot pair with a stale or mismatched peer.
        if (!params.local_trusted_preprocessing) {
            BTGen(party, io, fa, fb, fch, fcl);
            uint8_t btgen_barrier = 0x5a;
            if (party == ALICE) {
                io->send_data(&btgen_barrier, 1); io->flush();
                io->recv_data(&btgen_barrier, 1);
            } else {
                io->recv_data(&btgen_barrier, 1);
                io->send_data(&btgen_barrier, 1); io->flush();
            }
        } else {
            std::mt19937_64 dealer(kCorrectnessSeed ^ UINT64_C(0x424541564552));
            const block global_a = makeBlock(dealer(), dealer());
            const block global_b = makeBlock(dealer(), dealer());
            const block share_a0 = makeBlock(dealer(), dealer());
            const block share_b0 = makeBlock(dealer(), dealer());
            const block global_c = global_a * global_b;
            const block share_c0 = makeBlock(dealer(), dealer());
            fa = party == ALICE ? share_a0 : global_a - share_a0;
            fb = party == ALICE ? share_b0 : global_b - share_b0;
            fch = zero_block;
            fcl = party == ALICE ? share_c0 : global_c - share_c0;
            std::cout << "[PILOT_PREPROCESSING] mode=local_trusted_pilot security_equivalent=false"
                      << std::endl;
        }

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

        std::mt19937_64 g(kCorrectnessSeed ^ UINT64_C(0x53485546464c45));
        for (int i = 0; i < d * subperm_num; i++) {
            std::shuffle(perms.begin() + static_cast<size_t>(i) * T,
                         perms.begin() + static_cast<size_t>(i + 1) * T,
                         g);
        }

        auto start = std::chrono::system_clock::now();
        if (!params.local_trusted_preprocessing) {
            Offline(N, T, perms.data(), party, io, perm.data(), a.data(), b.data(), delta.data());
        } else {
            PermReconstruct(d, N, T, n, t, perms.data(), perm.data());
            std::mt19937_64 dealer(kCorrectnessSeed ^ UINT64_C(0x534855464445414c));
            for (uint64_t i = 0; i < N; ++i) {
                // Keep the protocol's uint64 location lane in field form while
                // retaining non-zero dealer randomness in the unused high lane.
                a[i] = makeBlock(dealer(), 0);
                b[i] = makeBlock(dealer(), 0);
            }
            for (uint64_t i = 0; i < N; ++i) delta[i] = b[i] - a[perm[i]];
        }
        auto end = std::chrono::system_clock::now();
        auto offline_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        result.offline_ms = offline_ms;
        std::cout << "[OFFLINE] preprocessing_time_ms=" << offline_ms << std::endl;

        long long keygen_total_us_sum = 0;
        long long serialization_us_sum = 0;
        long long eval_us_sum = 0;
        long long eval_core_us_sum = 0;
        long long proof_check_us_sum = 0;
        long long shuffle_us_sum = 0;
        long long compress_us_sum = 0;
        long long online_total_us_sum = 0;
        long long reconstruction_us_sum = 0;
        double recipient_processing_ns_sum = 0.0;
        uint64_t recovered_signals = 0;
        bool all_online_reps_ok = true;
        bool all_proofs_verified = true;
        bool all_cprs_indices_match = true;
        std::vector<uint64_t> location_values(N);
        for (uint64_t i = 0; i < N; ++i) {
            location_values[i] = i == 0
                ? VDPF_FIELD_MODULUS - 1
                : 1 + ((kCorrectnessSeed + i * UINT64_C(0x9e3779b97f4a7c15)) %
                       (VDPF_FIELD_MODULUS - 1));
        }
        std::vector<uint64_t> expected_locations(location_values.begin(),
                                                 location_values.begin() + ell);
        std::sort(expected_locations.begin(), expected_locations.end());

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
                    const uint64_t alpha_domain = registered_users > 0 ? registered_users : N;
                    uint64_t alpha = i < ell ? recv : NonHitAlpha(i, alpha_domain, recv);
                    if (alpha == recv) {
                        if (i < ell) {
                            alpha_hits++;
                        } else {
                            nonhit_collisions++;
                        }
                    }
                    dpf.InitKey(key);
                    dpf.Gen(alpha, location_values[i], key);
                    Data0.push_back(key[0]);
                    Data1.push_back(key[1]);
                }
                end = std::chrono::system_clock::now();
                keygen_total_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                keygen_per_signal_us = static_cast<double>(keygen_total_us) / static_cast<double>(N);
                std::cout << "[DEBUG] alpha_hits=" << alpha_hits << std::endl;
                std::cout << "[DEBUG] nonhit_alpha_collisions=" << nonhit_collisions << std::endl;

                start = std::chrono::system_clock::now();
                size_t serialized_bytes = 0;
                for (uint64_t i = 0; i < N; i++) {
                    std::vector<unsigned char> encoded0(Data0[i].vdpf_key,
                                                        Data0[i].vdpf_key + Data0[i].vdpf_key_len);
                    std::vector<unsigned char> encoded1(Data1[i].vdpf_key,
                                                        Data1[i].vdpf_key + Data1[i].vdpf_key_len);
                    serialized_bytes += encoded0.size() + encoded1.size();
                }
                end = std::chrono::system_clock::now();
                serialization_us_sum += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                std::cout << "[SENDER] serialized_key_bytes=" << serialized_bytes << std::endl;

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
            std::vector<std::array<uint8_t, kVDPFProofBytes>> proofs;
            proofs.reserve(N);
            std::vector<block> x(N);
            std::vector<block> out(N);
            std::vector<uint64_t> registered_inputs;
            if (params.component_pilot && registered_users > 0) {
                registered_inputs.resize(registered_users);
                for (uint64_t i = 0; i < registered_users; ++i) {
                    registered_inputs[i] = i;
                }
            }

            // Component-pilot admission: keys and workload already exist. Time
            // only the server-side VerEval, cross-server proof verification,
            // and append to the benchmark's accepted-key state.
            if (params.component_pilot) {
                std::vector<std::array<uint8_t, kVDPFProofBytes>> admission_proofs;
                admission_proofs.reserve(N);
                std::vector<const DPFKey *> accepted_keys;
                accepted_keys.reserve(N);
                start = std::chrono::system_clock::now();
                if (party == ALICE) {
                    for (uint64_t j = 0; j < Data0.size(); ++j) {
                        admission_proofs.push_back(
                            registered_inputs.empty()
                                ? dpf.EvalWithProof(0, Data0[j], recv).proof
                                : dpf.BatchEvalProof(0, Data0[j], registered_inputs));
                        accepted_keys.push_back(&Data0[j]);
                    }
                } else {
                    for (uint64_t j = 0; j < Data1.size(); ++j) {
                        admission_proofs.push_back(
                            registered_inputs.empty()
                                ? dpf.EvalWithProof(1, Data1[j], recv).proof
                                : dpf.BatchEvalProof(1, Data1[j], registered_inputs));
                        accepted_keys.push_back(&Data1[j]);
                    }
                }
                uint8_t admission_ok = 1;
                if (party == ALICE) {
                    std::vector<std::array<uint8_t, kVDPFProofBytes>> peer(N);
                    io->recv_data(peer.data(), N * kVDPFProofBytes);
                    for (uint64_t i = 0; i < N; ++i) {
                        if (!VerifyVDPFProofs(admission_proofs[i], peer[i])) {
                            admission_ok = 0;
                            break;
                        }
                    }
                    io->send_data(&admission_ok, 1);
                    io->flush();
                } else {
                    io->send_data(admission_proofs.data(), N * kVDPFProofBytes);
                    io->flush();
                    io->recv_data(&admission_ok, 1);
                }
                end = std::chrono::system_clock::now();
                if (!admission_ok || accepted_keys.size() != N) {
                    throw std::runtime_error("component-pilot admission verification failed");
                }
                result.admission_us += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            }

            const auto eval_core_start = std::chrono::system_clock::now();
            if (party == ALICE) {
                for (uint64_t j = 0; j < Data0.size(); j++) {
                    DPFEvaluation evaluation = dpf.EvalWithProof(0, Data0[j], recv);
                    xshare.push_back(evaluation.value);
                    proofs.push_back(evaluation.proof);
                }
            } else {
                for (uint64_t j = 0; j < Data1.size(); j++) {
                    DPFEvaluation evaluation = dpf.EvalWithProof(1, Data1[j], recv);
                    xshare.push_back(evaluation.value);
                    proofs.push_back(evaluation.proof);
                }
            }
            const auto eval_core_end = std::chrono::system_clock::now();

            const auto proof_check_start = std::chrono::system_clock::now();
            uint8_t proof_verified = 1;
            if (party == ALICE) {
                std::vector<std::array<uint8_t, kVDPFProofBytes>> peer_proofs(N);
                io->recv_data(peer_proofs.data(), N * kVDPFProofBytes);
                for (uint64_t i = 0; i < N; ++i) {
                    if (!VerifyVDPFProofs(proofs[i], peer_proofs[i])) {
                        proof_verified = 0;
                        break;
                    }
                }
                io->send_data(&proof_verified, sizeof(proof_verified));
                io->flush();
            } else {
                io->send_data(proofs.data(), N * kVDPFProofBytes);
                io->flush();
                io->recv_data(&proof_verified, sizeof(proof_verified));
            }
            const auto proof_check_end = std::chrono::system_clock::now();
            const auto eval_core_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                eval_core_end - eval_core_start).count();
            const auto proof_check_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
                proof_check_end - proof_check_start).count();
            const auto eval_time_us = eval_core_time_us + proof_check_time_us;
            eval_us_sum += eval_time_us;
            eval_core_us_sum += eval_core_time_us;
            proof_check_us_sum += proof_check_time_us;
            std::cout << "[SERVER] eval_core_time_us=" << eval_core_time_us << std::endl;
            std::cout << "[SERVER] retrieval_proof_check_time_us=" << proof_check_time_us << std::endl;
            std::cout << "[SERVER] eval_and_proof_verification_time_us=" << eval_time_us << std::endl;
            std::cout << "[VERIFICATION] vdpf_proofs="
                      << (proof_verified ? "verified" : "mismatch") << std::endl;
            if (!proof_verified) {
                throw std::runtime_error("VDPF proof mismatch; refusing Shuffle/Cprs");
            }

            // The VDPF returns field61 additive shares, while the recovered
            // shuffle harness accepts emp::block additive shares.  The artifact
            // has no private share-conversion protocol.  Keep the existing open
            // only as an explicitly untimed correctness/domain-bridge diagnostic.
            if (params.component_pilot) {
                // The artifact lacks a private field61-to-block conversion.
                // Compose the measured components using a valid field vector
                // with exactly ell nonzero handles; do not time this bridge.
                for (uint64_t i = 0; i < N; ++i) {
                    x[i] = party == ALICE && i < ell
                        ? makeBlock(0, location_values[i]) : zero_block;
                }
                std::cout << "[COMPONENT_PILOT] compression_input=legal_sparse_field61_vector"
                          << " end_to_end_correctness=false" << std::endl;
            } else if (party == ALICE) {
                std::vector<uint64_t> peer_share(N);
                io->recv_data(peer_share.data(), N * sizeof(uint64_t));
                uint64_t preshuffle_nonzero = 0;
                for (uint64_t i = 0; i < N; i++) {
                    uint64_t opened = field61_add(xshare[i], peer_share[i]);
                    if (opened != 0) {
                        preshuffle_nonzero++;
                    }
                    x[i] = makeBlock(0, opened);
                }
                std::cout << "[DIAGNOSTIC_ONLY] field61_bridge_nonzero=" << preshuffle_nonzero
                          << " bytes=" << (N * sizeof(uint64_t))
                          << " excluded_from_timers=true" << std::endl;
            } else {
                io->send_data(xshare.data(), N * sizeof(uint64_t));
                for (uint64_t i = 0; i < N; i++) {
                    x[i] = makeBlock(0, 0);
                }
            }

            start = std::chrono::system_clock::now();
            SecretSharedShuffle(N, T, party, io, x.data(), perm.data(), delta.data(), a.data(), b.data(), out.data());
            end = std::chrono::system_clock::now();
            auto shuffle_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            shuffle_us_sum += shuffle_time_us;
            std::cout << "[SERVER] shuffle_time_us=" << shuffle_time_us << std::endl;

            std::vector<uint64_t> sig;
            std::vector<uint64_t> compressed_shares;
            start = std::chrono::system_clock::now();
            CPRS(party, io, N, out.data(), ran, fa, fb, fch, fcl,
                 sig, compressed_shares);
            end = std::chrono::system_clock::now();
            auto compress_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            auto online_total_time_us = eval_time_us + shuffle_time_us + compress_time_us;
            compress_us_sum += compress_time_us;
            online_total_us_sum += online_total_time_us;
            std::cout << "[SERVER] compress_time_us=" << compress_time_us << std::endl;
            std::cout << "[SERVER] timed_stage_sum_us=" << online_total_time_us
                      << " diagnostic_excluded=true" << std::endl;

            start = std::chrono::system_clock::now();
            std::vector<uint8_t> response_buffer = SerializeFieldShares(compressed_shares);
            end = std::chrono::system_clock::now();
            const auto response_serialization_us =
                std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            result.response_serialization_us += response_serialization_us;
            if (party == ALICE) result.server0_response_bytes = response_buffer.size();
            else result.server1_response_bytes = response_buffer.size();

            uint64_t peer_response_size = 0;
            std::vector<uint8_t> peer_response;
            if (party == ALICE) {
                peer_response_size = response_buffer.size();
                io->send_data(&peer_response_size, sizeof(peer_response_size));
                io->send_data(response_buffer.data(), response_buffer.size());
            } else {
                io->recv_data(&peer_response_size, sizeof(peer_response_size));
                peer_response.resize(peer_response_size);
                io->recv_data(peer_response.data(), peer_response.size());
            }

            // Exact-location reconstruction is diagnostic-only because the
            // PSVFSS response contract is unresolved.  It runs after all timed
            // Eval/Shuffle/Cprs stages and is not response communication.
            std::vector<block> out_other(N);
            long long reconstruction_time_us = 0;
            uint64_t rep_recovered_signals = 0;
            bool rep_exact_locations_ok = false;
            bool rep_cprs_indices_match = false;
            std::array<uint64_t, 5> correctness_summary{};
            if (party == ALICE) {
                io->send_block(out.data(), N);
            } else {
                io->recv_block(out_other.data(), N);
            }
            if (party == BOB) {
                std::vector<uint64_t> reconstructed_indices;
                std::vector<uint64_t> recovered_locations;
                constexpr uint64_t kRecipientInnerOps = 1000;
                const auto recipient_start = std::chrono::steady_clock::now();
                for (uint64_t op = 0; op < kRecipientInnerOps; ++op) {
                    const std::vector<uint64_t> server0_shares =
                        DeserializeFieldShares(peer_response);
                    const std::vector<uint64_t> server1_shares =
                        DeserializeFieldShares(response_buffer);
                    if (server0_shares.size() != server1_shares.size()) {
                        throw std::runtime_error("PSVFSS response element counts differ");
                    }
                    std::vector<uint64_t> decoded;
                    decoded.reserve(server0_shares.size());
                    for (size_t i = 0; i < server0_shares.size(); ++i) {
                        // Location handles are the field elements in this pilot;
                        // LocDec is therefore the identity after field61 opening.
                        decoded.push_back(field61_add(server0_shares[i], server1_shares[i]));
                    }
                    if (op + 1 == kRecipientInnerOps) recovered_locations = std::move(decoded);
                }
                const auto recipient_end = std::chrono::steady_clock::now();
                const double recipient_processing_ns =
                    static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        recipient_end - recipient_start).count()) /
                    static_cast<double>(kRecipientInnerOps);
                recipient_processing_ns_sum += recipient_processing_ns;
                reconstruction_time_us = static_cast<long long>(recipient_processing_ns / 1000.0);

                // Everything below is diagnostic correctness work and is kept
                // outside the recipient response-consumption timer.
                for (uint64_t i = 0; i < N; i++) {
                    block recovered = out[i] + out_other[i];
                    uint64_t location = Low64(recovered);
                    if (location != 0) {
                        reconstructed_indices.push_back(i);
                    }
                }
                std::sort(recovered_locations.begin(), recovered_locations.end());
                std::vector<uint64_t> missing =
                    MultisetDifference(expected_locations, recovered_locations);
                std::vector<uint64_t> unexpected =
                    MultisetDifference(recovered_locations, expected_locations);
                rep_exact_locations_ok = missing.empty() && unexpected.empty();
                rep_cprs_indices_match = sig == reconstructed_indices;
                rep_recovered_signals = recovered_locations.size();
                PrintLocations("expected_locations", expected_locations);
                PrintLocations("recovered_locations", recovered_locations);
                PrintLocations("missing_locations", missing);
                PrintLocations("unexpected_locations", unexpected);
                std::cout << "[CORRECTNESS] cprs_indices_match="
                          << (rep_cprs_indices_match ? "true" : "false") << std::endl;

                result.expected_locations = expected_locations;
                result.recovered_locations = recovered_locations;
                result.missing_locations = missing;
                result.unexpected_locations = unexpected;
                correctness_summary = {
                    rep_exact_locations_ok ? 1u : 0u,
                    rep_cprs_indices_match ? 1u : 0u,
                    rep_recovered_signals,
                    static_cast<uint64_t>(missing.size()),
                    static_cast<uint64_t>(unexpected.size())};
                correctness_summary[4] = response_buffer.size();
                io->send_data(correctness_summary.data(),
                              correctness_summary.size() * sizeof(uint64_t));
            } else {
                io->recv_data(correctness_summary.data(),
                              correctness_summary.size() * sizeof(uint64_t));
                rep_exact_locations_ok = correctness_summary[0] != 0;
                rep_cprs_indices_match = correctness_summary[1] != 0;
                rep_recovered_signals = correctness_summary[2];
                result.server1_response_bytes = correctness_summary[4];
            }
            result.server_to_recipient_bytes =
                result.server0_response_bytes + result.server1_response_bytes;
            reconstruction_us_sum += reconstruction_time_us;
            if ((!rep_exact_locations_ok && !params.component_pilot) || !rep_cprs_indices_match) {
                all_online_reps_ok = false;
            }
            all_proofs_verified = all_proofs_verified && proof_verified;
            all_cprs_indices_match = all_cprs_indices_match && rep_cprs_indices_match;
            recovered_signals = rep_recovered_signals;
            result.diagnostic_bytes = N * sizeof(uint64_t) +
                                      N * sizeof(block) +
                                      correctness_summary.size() * sizeof(uint64_t);
            std::cout << "[DIAGNOSTIC_ONLY] reconstruction_time_us=" << reconstruction_time_us
                      << " bytes=" << result.diagnostic_bytes
                      << " excluded_from_timers=true excluded_from_response=true" << std::endl;
            std::cout << "[ONLINE_REP] done=" << (rep + 1) << "/" << online_reps << std::endl;
        }

        result.keygen_total_us = keygen_total_us_sum / static_cast<long long>(online_reps);
        result.serialization_us = serialization_us_sum / static_cast<long long>(online_reps);
        result.keygen_per_signal_us = static_cast<double>(result.keygen_total_us) / static_cast<double>(N);
        result.eval_us = eval_us_sum / static_cast<long long>(online_reps);
        result.eval_core_us = eval_core_us_sum / static_cast<long long>(online_reps);
        result.proof_check_us = proof_check_us_sum / static_cast<long long>(online_reps);
        result.shuffle_us = shuffle_us_sum / static_cast<long long>(online_reps);
        result.compress_us = compress_us_sum / static_cast<long long>(online_reps);
        result.online_total_us = online_total_us_sum / static_cast<long long>(online_reps);
            result.reconstruction_us = reconstruction_us_sum / static_cast<long long>(online_reps);
        result.recipient_processing_ns = recipient_processing_ns_sum / static_cast<double>(online_reps);
        result.recipient_inner_ops = 1000;
        result.response_serialization_us /= static_cast<long long>(online_reps);
        result.admission_us /= static_cast<long long>(online_reps);
        result.recovered_signals = recovered_signals;
        result.proof_verified = all_proofs_verified;
        result.exact_locations_ok = all_online_reps_ok;
        result.cprs_indices_match = all_cprs_indices_match;
        result.expected_locations = expected_locations;
        std::cout << "[ONLINE_AVG] keygen_total_us=" << result.keygen_total_us << std::endl;
        std::cout << "[ONLINE_AVG] eval_core_time_us=" << result.eval_core_us << std::endl;
        std::cout << "[ONLINE_AVG] retrieval_proof_check_time_us=" << result.proof_check_us << std::endl;
        std::cout << "[ONLINE_AVG] eval_time_us=" << result.eval_us << std::endl;
        std::cout << "[ONLINE_AVG] shuffle_time_us=" << result.shuffle_us << std::endl;
        std::cout << "[ONLINE_AVG] compress_time_us=" << result.compress_us << std::endl;
        std::cout << "[ONLINE_AVG] reconstruction_time_us=" << result.reconstruction_us << std::endl;

        std::cout << "[PILOT_JSON] {\"scheme\":\"PSVFSS\",\"N\":" << N
                  << ",\"k_actual\":" << ell
                  << ",\"admission_batch_online_ms\":" << result.admission_us / 1000.0
                  << ",\"registered_users\":" << registered_users
                  << ",\"admission_per_signal_ms\":" << (result.admission_us / 1000.0) / N
                  << ",\"retrieval_eval_core_ms\":" << result.eval_core_us / 1000.0
                  << ",\"retrieval_proof_check_ms\":" << result.proof_check_us / 1000.0
                  << ",\"retrieval_eval_ms\":" << result.eval_us / 1000.0
                  << ",\"retrieval_shuffle_ms\":" << result.shuffle_us / 1000.0
                  << ",\"retrieval_compression_ms\":" << result.compress_us / 1000.0
                  << ",\"response_serialization_ms\":" << result.response_serialization_us / 1000.0
                  << ",\"retrieval_online_ms\":"
                  << (result.online_total_us + result.response_serialization_us) / 1000.0
                  << ",\"server0_response_bytes\":" << result.server0_response_bytes
                  << ",\"server1_response_bytes\":" << result.server1_response_bytes
                  << ",\"server_to_recipient_bytes\":" << result.server_to_recipient_bytes
                  << ",\"recipient_processing_ns\":" << result.recipient_processing_ns
                  << ",\"recipient_inner_ops\":" << result.recipient_inner_ops
                  << ",\"preprocessing_mode\":\""
                  << (params.local_trusted_preprocessing ? "local_trusted_pilot" : "real_offline") << "\""
                  << ",\"measurement_quality\":\""
                  << (params.component_pilot ? "component_composed_pilot" : "end_to_end_pilot") << "\""
                  << ",\"end_to_end_correctness\":"
                  << ((!params.component_pilot && result.exact_locations_ok) ? "true" : "false")
                  << ",\"measurement_status\":\"ok\"}" << std::endl;

        return result;
    }

    void ServerTest0(int argc, char **argv) {
        int port, party;
        emp::parse_party_and_port(argv, &party, &port);

        NetIO *io = new NetIO(party == 1 ? nullptr : "127.0.0.1", port, false);
        // NetIO installs a 1 MiB stdio buffer.  Offline IKNP sends short,
        // request/response frames, so a buffered fread can wait for bytes that
        // the peer will only send after receiving our response.  Use actual
        // unbuffered I/O; this changes no protocol frame or message ordering.
        setvbuf(io->stream, nullptr, _IONBF, 0);

        if (argc > 3 && std::string(argv[3]) == "bench") {
            const std::vector<BenchmarkParams> cases = {
                {4096, 512, 50, 5},
                {8192, 512, 50, 5},
                {16384, 512, 50, 5},
            };

            std::cout << "[PSVFSS_RESPONSE] status=unresolved server_to_recipient_bytes=null"
                      << std::endl;
            for (const auto &params : cases) {
                std::cout << "[BENCH] start Ns=" << params.Ns
                          << ", T=" << params.T
                          << ", ell=" << params.ell << std::endl;
                BenchmarkResult result = RunBenchmarkCase(party, port, io, params);
                PrintBenchCsv(params, result);
                if (party == ALICE) {
                    PrintUnifiedBenchCsv(params, result);
                }
                if (!result.proof_verified || !result.exact_locations_ok ||
                    !result.cprs_indices_match) {
                    throw std::runtime_error("PSVFSS exact correctness failed");
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
                argc > 7 && (std::string(argv[7]) == "--pilot-preprocessing-mode=local-trusted" ||
                             std::string(argv[7]) == "--preprocessing-mode=local-trusted"),
                argc > 8 && std::string(argv[8]) == "--measurement-mode=component-pilot",
                ParseUintArg(argv, 9, 0),
            };
            BenchmarkResult result = RunBenchmarkCase(party, port, io, params);
	            if (party == ALICE) {
	                PrintUnifiedBenchCsv(params, result);
	                PrintDetailBenchCsv(params, result);
	                PrintClientBenchJson(params, result);
	            }
	            if (!result.proof_verified || (!params.component_pilot && !result.exact_locations_ok) ||
	                !result.cprs_indices_match) {
	                throw std::runtime_error("PSVFSS exact correctness failed");
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
