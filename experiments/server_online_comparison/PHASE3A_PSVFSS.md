# Phase 3A — PSVFSS correctness foundation

## Scope and measurement boundary

This phase changes no Cprs/Compress or shuffle implementation and defines no new
recipient response protocol.  Its only purpose is to establish correctness before
later measurement of:

1. server online computation time, excluding setup, preprocessing, file I/O, and
   correctness diagnostics; and
2. the byte length of an actually serialized server-to-recipient response.

The PSVFSS response contract remains `unresolved`; consequently
`server_to_recipient_bytes` remains `null`.  The former theoretical `2*k*8` value
is not a measurement.

## Implemented correctness checks

- VDPF output shares are reconstructed with `field61_add`, including the
  `2^61-2` boundary value.  Target evaluations recover their configured location
  and non-target evaluations recover zero.
- `DPF::EvalWithProof` preserves the 32-byte proof emitted by `batchEvalVDPF`.
  Servers compare the two proofs byte-for-byte, matching `vdpf/src/test.c`; a
  mismatch aborts before Shuffle/Cprs.
- The standalone diagnostic flips one proof byte and confirms rejection.
- Expected and recovered location multisets are compared exactly.  Missing and
  unexpected values retain duplicate multiplicity.
- The Cprs-selected plaintext shuffled indices are only checked against the
  diagnostic full-vector nonzero indices.  They are not called a recipient
  response and produce no response-byte value.
- Both full-vector openings occur outside the Eval/Shuffle/Cprs timers and are
  labelled `DIAGNOSTIC_ONLY`; their bytes are reported separately.

## Validation status

- `ps_vfss_end_to_end_semantics_diag`: passed.  Honest proof verification passed,
  a tampered proof was rejected, target and non-target reconstruction passed, and
  the field boundary value was recovered exactly.
- The full two-server `Ns=32, k_actual=4` smoke did not reach its online phase.
  The default RELIC build aborted in BTGen.  The existing BN6144-compatible build
  passed BTGen but remained inside the pre-existing `Offline(...)` shuffle
  preprocessing for approximately six minutes, after which the diagnostic run
  was stopped.  A second run used the legal one-layer `T=Ns` configuration but
  likewise failed to reach the online phase within the bounded smoke window.  A
  temporary main-stream buffering experiment did not resolve the stall and was
  reverted.  No online timing or communication result was produced.
- `Ns=256, k_actual=10` was not started because the required smaller smoke did
  not complete.

This is a build/runtime blocker, not evidence that Cprs is correct or incorrect.
No protected Cprs, shuffle, or server source was changed to bypass it.
