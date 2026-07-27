/**
 * Admission-time VDPF verification and verified signal-key storage.
 */

#pragma once

#include "dpf.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

enum class AdmissionDecision {
  Accept,
  RejectInvalidRequest,
  RejectDuplicateSignalId,
  RejectPeerPrecheck,
  RejectProofMismatch,
};

const char *AdmissionDecisionName(AdmissionDecision decision);

struct AdmissionMessage {
  uint64_t signal_id = 0;
  bool ready = false;
  VDPFProof proof{};
};

class AdmissionServer;

class PreparedAdmission {
public:
  bool ready() const;
  uint64_t signal_id() const;
  AdmissionDecision precheck_decision() const;
  const VDPFProof &proof() const;
  const std::vector<uint64_t> &outputs() const;
  AdmissionMessage message() const;

private:
  friend class AdmissionServer;

  const AdmissionServer *owner_ = nullptr;
  uint64_t signal_id_ = 0;
  bool ready_ = false;
  AdmissionDecision precheck_decision_ =
      AdmissionDecision::RejectInvalidRequest;
  VDPFProof proof_{};
  std::vector<uint64_t> outputs_;
  std::vector<uint8_t> key_share_;
};

class AdmissionServer {
public:
  AdmissionServer(uint8_t party, DPF *dpf);

  PreparedAdmission Prepare(
      uint64_t signal_id,
      const DPFKey &key_share,
      const std::vector<uint64_t> &registered_identifiers);

  AdmissionDecision Finalize(
      const PreparedAdmission &prepared,
      const AdmissionMessage &peer_message);

  size_t signal_count() const;
  bool Contains(uint64_t signal_id) const;
  const std::vector<uint8_t> *Lookup(uint64_t signal_id) const;

private:
  uint8_t party_;
  DPF *dpf_;
  std::unordered_map<uint64_t, std::vector<uint8_t>> verified_keys_;
};
