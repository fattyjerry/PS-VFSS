#include "admission.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Expect(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void ExpectDecision(
    AdmissionDecision actual,
    AdmissionDecision expected,
    const std::string &context) {
  if (actual != expected) {
    throw std::runtime_error(
        context + ": expected " + AdmissionDecisionName(expected) +
        ", got " + AdmissionDecisionName(actual));
  }
}

void CheckHonestOutputs(
    const std::vector<uint64_t> &identifiers,
    uint64_t alpha,
    const PreparedAdmission &server0,
    const PreparedAdmission &server1) {
  Expect(
      server0.outputs().size() == identifiers.size() &&
          server1.outputs().size() == identifiers.size(),
      "VerEval returned an unexpected output length");
  for (size_t i = 0; i < identifiers.size(); ++i) {
    const uint64_t reconstructed =
        server0.outputs()[i] ^ server1.outputs()[i];
    const uint64_t expected = identifiers[i] == alpha ? 1 : 0;
    Expect(
        reconstructed == expected,
        "honest VerEval outputs do not reconstruct to the point function");
  }
}
}

int main() {
  try {
    constexpr uint8_t kBits = 16;
    constexpr uint64_t kAlpha = 23;
    const std::vector<uint64_t> registered_identifiers = {
        10, 20, kAlpha, 30, 40};

    DPF client(kBits);
    DPF server0_dpf(kBits);
    DPF server1_dpf(kBits);

    std::array<uint8_t, 48> context_keys{};
    client.ExportContextKeys(context_keys.data());
    server0_dpf.ImportContextKeys(context_keys.data());
    server1_dpf.ImportContextKeys(context_keys.data());

    DPFKey honest_keys[2];
    client.InitKey(honest_keys);
    client.Gen(kAlpha, 1, honest_keys);

    DPFKey other_keys[2];
    client.InitKey(other_keys);
    client.Gen(31, 1, other_keys);

    AdmissionServer server0(0, &server0_dpf);
    AdmissionServer server1(1, &server1_dpf);

    const uint64_t honest_sid = 1001;
    PreparedAdmission honest0 =
        server0.Prepare(honest_sid, honest_keys[0], registered_identifiers);
    PreparedAdmission honest1 =
        server1.Prepare(honest_sid, honest_keys[1], registered_identifiers);

    Expect(honest0.ready() && honest1.ready(), "honest precheck failed");
    Expect(
        DPF::VerifyProofs(honest0.proof(), honest1.proof()),
        "honest VDPF proofs do not match");
    CheckHonestOutputs(
        registered_identifiers, kAlpha, honest0, honest1);

    ExpectDecision(
        server0.Finalize(honest0, honest1.message()),
        AdmissionDecision::Accept,
        "server 0 honest admission");
    ExpectDecision(
        server1.Finalize(honest1, honest0.message()),
        AdmissionDecision::Accept,
        "server 1 honest admission");

    Expect(
        server0.signal_count() == 1 && server1.signal_count() == 1,
        "honest admission did not update both verified databases");
    const std::vector<uint8_t> *stored0 = server0.Lookup(honest_sid);
    const std::vector<uint8_t> *stored1 = server1.Lookup(honest_sid);
    Expect(stored0 != nullptr && stored1 != nullptr, "stored key is missing");
    Expect(
        stored0->size() == honest_keys[0].vdpf_key_len &&
            stored1->size() == honest_keys[1].vdpf_key_len,
        "stored key length is incorrect");
    Expect(
        std::memcmp(
            stored0->data(),
            honest_keys[0].vdpf_key,
            stored0->size()) == 0 &&
            std::memcmp(
                stored1->data(),
                honest_keys[1].vdpf_key,
                stored1->size()) == 0,
        "stored key bytes differ from the verified shares");
    std::cout << "[PASS] honest key pair accepted and stored" << std::endl;

    const size_t count_before_reject0 = server0.signal_count();
    const size_t count_before_reject1 = server1.signal_count();
    const uint64_t inconsistent_sid = 1002;
    PreparedAdmission inconsistent0 =
        server0.Prepare(
            inconsistent_sid, honest_keys[0], registered_identifiers);
    PreparedAdmission inconsistent1 =
        server1.Prepare(
            inconsistent_sid, other_keys[1], registered_identifiers);

    Expect(
        inconsistent0.ready() && inconsistent1.ready(),
        "inconsistent pair failed before proof comparison");
    Expect(
        !DPF::VerifyProofs(
            inconsistent0.proof(), inconsistent1.proof()),
        "inconsistent key shares unexpectedly produced equal proofs");
    ExpectDecision(
        server0.Finalize(inconsistent0, inconsistent1.message()),
        AdmissionDecision::RejectProofMismatch,
        "server 0 inconsistent admission");
    ExpectDecision(
        server1.Finalize(inconsistent1, inconsistent0.message()),
        AdmissionDecision::RejectProofMismatch,
        "server 1 inconsistent admission");
    Expect(
        server0.signal_count() == count_before_reject0 &&
            server1.signal_count() == count_before_reject1 &&
            !server0.Contains(inconsistent_sid) &&
            !server1.Contains(inconsistent_sid),
        "proof rejection modified verified database state");
    std::cout
        << "[PASS] inconsistent key pair rejected without state update"
        << std::endl;

    const uint64_t tampered_proof_sid = 1003;
    PreparedAdmission tampered0 =
        server0.Prepare(
            tampered_proof_sid, honest_keys[0], registered_identifiers);
    PreparedAdmission tampered1 =
        server1.Prepare(
            tampered_proof_sid, honest_keys[1], registered_identifiers);
    AdmissionMessage peer_for0 = tampered1.message();
    AdmissionMessage peer_for1 = tampered0.message();
    peer_for0.proof[0] ^= 1U;
    peer_for1.proof[0] ^= 1U;

    ExpectDecision(
        server0.Finalize(tampered0, peer_for0),
        AdmissionDecision::RejectProofMismatch,
        "server 0 tampered proof");
    ExpectDecision(
        server1.Finalize(tampered1, peer_for1),
        AdmissionDecision::RejectProofMismatch,
        "server 1 tampered proof");
    Expect(
        server0.signal_count() == count_before_reject0 &&
            server1.signal_count() == count_before_reject1 &&
            !server0.Contains(tampered_proof_sid) &&
            !server1.Contains(tampered_proof_sid),
        "tampered proof modified verified database state");
    std::cout
        << "[PASS] tampered proofs rejected without state update"
        << std::endl;

    PreparedAdmission duplicate0 =
        server0.Prepare(
            honest_sid, honest_keys[0], registered_identifiers);
    PreparedAdmission duplicate1 =
        server1.Prepare(
            honest_sid, honest_keys[1], registered_identifiers);
    Expect(
        !duplicate0.ready() && !duplicate1.ready(),
        "duplicate signal identifier passed the freshness check");
    ExpectDecision(
        server0.Finalize(duplicate0, duplicate1.message()),
        AdmissionDecision::RejectDuplicateSignalId,
        "server 0 duplicate signal identifier");
    ExpectDecision(
        server1.Finalize(duplicate1, duplicate0.message()),
        AdmissionDecision::RejectDuplicateSignalId,
        "server 1 duplicate signal identifier");
    Expect(
        server0.signal_count() == count_before_reject0 &&
            server1.signal_count() == count_before_reject1,
        "duplicate rejection modified verified database state");
    std::cout
        << "[PASS] duplicate signal identifier rejected without state update"
        << std::endl;

    client.FreeKey(honest_keys[0]);
    client.FreeKey(honest_keys[1]);
    client.FreeKey(other_keys[0]);
    client.FreeKey(other_keys[1]);

    std::cout << "All admission-time VDPF verification tests passed."
              << std::endl;
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "[FAIL] " << error.what() << std::endl;
    return 1;
  }
}
