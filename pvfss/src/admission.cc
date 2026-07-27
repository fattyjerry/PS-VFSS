#include "admission.h"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace {
bool HasDistinctInputs(const std::vector<uint64_t> &inputs) {
  std::unordered_set<uint64_t> seen;
  seen.reserve(inputs.size());
  for (uint64_t input : inputs) {
    if (!seen.insert(input).second) {
      return false;
    }
  }
  return true;
}
}

const char *AdmissionDecisionName(AdmissionDecision decision) {
  switch (decision) {
    case AdmissionDecision::Accept:
      return "accept";
    case AdmissionDecision::RejectInvalidRequest:
      return "reject_invalid_request";
    case AdmissionDecision::RejectDuplicateSignalId:
      return "reject_duplicate_signal_id";
    case AdmissionDecision::RejectPeerPrecheck:
      return "reject_peer_precheck";
    case AdmissionDecision::RejectProofMismatch:
      return "reject_proof_mismatch";
  }
  return "reject_unknown";
}

bool PreparedAdmission::ready() const {
  return ready_;
}

uint64_t PreparedAdmission::signal_id() const {
  return signal_id_;
}

AdmissionDecision PreparedAdmission::precheck_decision() const {
  return precheck_decision_;
}

const VDPFProof &PreparedAdmission::proof() const {
  return proof_;
}

const std::vector<uint64_t> &PreparedAdmission::outputs() const {
  return outputs_;
}

AdmissionMessage PreparedAdmission::message() const {
  return AdmissionMessage{signal_id_, ready_, proof_};
}

AdmissionServer::AdmissionServer(uint8_t party, DPF *dpf)
    : party_(party), dpf_(dpf) {
  if (party_ > 1 || dpf_ == nullptr) {
    throw std::invalid_argument("invalid admission server configuration");
  }
}

PreparedAdmission AdmissionServer::Prepare(
    uint64_t signal_id,
    const DPFKey &key_share,
    const std::vector<uint64_t> &registered_identifiers) {
  PreparedAdmission prepared;
  prepared.owner_ = this;
  prepared.signal_id_ = signal_id;

  if (Contains(signal_id)) {
    prepared.precheck_decision_ =
        AdmissionDecision::RejectDuplicateSignalId;
    return prepared;
  }
  if (registered_identifiers.empty() ||
      !HasDistinctInputs(registered_identifiers)) {
    prepared.precheck_decision_ =
        AdmissionDecision::RejectInvalidRequest;
    return prepared;
  }

  try {
    VDPFEvaluation evaluation =
        dpf_->VerEval(party_, key_share, registered_identifiers);
    prepared.proof_ = evaluation.proof;
    prepared.outputs_ = std::move(evaluation.outputs);
    prepared.key_share_.assign(
        key_share.vdpf_key,
        key_share.vdpf_key + key_share.vdpf_key_len);
  } catch (const std::invalid_argument &) {
    prepared.precheck_decision_ =
        AdmissionDecision::RejectInvalidRequest;
    return prepared;
  }

  prepared.ready_ = true;
  prepared.precheck_decision_ = AdmissionDecision::Accept;
  return prepared;
}

AdmissionDecision AdmissionServer::Finalize(
    const PreparedAdmission &prepared,
    const AdmissionMessage &peer_message) {
  if (prepared.owner_ != this) {
    return AdmissionDecision::RejectInvalidRequest;
  }
  if (!prepared.ready_) {
    return prepared.precheck_decision_;
  }
  if (!peer_message.ready ||
      peer_message.signal_id != prepared.signal_id_) {
    return AdmissionDecision::RejectPeerPrecheck;
  }
  if (Contains(prepared.signal_id_)) {
    return AdmissionDecision::RejectDuplicateSignalId;
  }
  if (!DPF::VerifyProofs(prepared.proof_, peer_message.proof)) {
    return AdmissionDecision::RejectProofMismatch;
  }

  const auto inserted = verified_keys_.emplace(
      prepared.signal_id_,
      prepared.key_share_);
  if (!inserted.second) {
    return AdmissionDecision::RejectDuplicateSignalId;
  }
  return AdmissionDecision::Accept;
}

size_t AdmissionServer::signal_count() const {
  return verified_keys_.size();
}

bool AdmissionServer::Contains(uint64_t signal_id) const {
  return verified_keys_.find(signal_id) != verified_keys_.end();
}

const std::vector<uint8_t> *AdmissionServer::Lookup(
    uint64_t signal_id) const {
  const auto it = verified_keys_.find(signal_id);
  return it == verified_keys_.end() ? nullptr : &it->second;
}
