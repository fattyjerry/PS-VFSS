# Server Online Comparison Experiment Contract

Contract version: `1.0.1`

> Status note: this is the frozen pre-instrumentation contract and is retained
> to document measurement decisions. Statements below headed “currently
> implemented” describe that historical checkpoint, not the repository's latest
> code. Current dataset validity and implementation status are indexed in
> `README.md` in this directory; row-level status and correctness fields remain
> authoritative.

This document fixes protocol lifecycle, workload, measurement, communication,
correctness, and resume semantics before instrumentation is added. It describes
what later benchmarks must measure; it does not claim that every phase is
currently implemented.

## 1. Lifecycle

Every trial consists of `Ns` signals arriving sequentially, followed by exactly
one retrieval for `target_recipient`.

### 1.1 Offline setup (excluded)

Offline setup includes global and protocol-key setup, sender key/flag/clue
generation, recipient key registration, detection/evaluation-key generation,
GC offline preparation, shuffle preprocessing, workload generation, benchmark
file loading or in-memory preload, and generation of fixed public parameters or
fixed public graphs. None of these operations may be charged to server online
time.

Inputs must be generated and loaded before an online timer starts. Moving work
outside a timer does not make signal-dependent work offline: any operation that
must occur because a particular signal arrived remains admission online.

### 1.2 Signal admission online

For each already-generated signal, admission begins when its complete bytes are
available in server memory and ends only after the signal is committed to the
server state used by the later retrieval. It includes required parsing,
decryption, verification, GC/2PC evaluation, rerandomization, append/update, and
state commit. Network transfer and waiting before the bytes arrive are excluded.

PPS-GC's two role-swapped GC table updates, including whole-table
rerandomization and updates of both the location table and index/counter column,
are admission online. They must never be reported as retrieval.

Phase status is one of `ok`, `not_implemented`, `unsupported`, or `error`.
Absent service-layer work is `not_implemented`, never a zero-duration phase.

### 1.3 Retrieval online

Retrieval begins after an already-constructed request is available in server
memory and ends when all application-layer response buffers have been fully
serialized. Network send/wait and recipient reconstruction are excluded.

It is split into:

- `retrieval_core`: request-dependent server computation through construction
  of all response objects;
- `response_serialization`: deterministic serialization/framing of those
  objects into byte buffers;
- `retrieval_online_total`: critical-path wall time from retrieval start through
  serialization completion. It is measured directly and is not assumed to equal
  the sum of rounded substage values.

### 1.4 Recipient correctness (excluded from server time)

Every measured trial must deserialize the actual response buffers and complete
recipient reconstruction. A trial with `correctness_ok=false` is invalid and
must not enter aggregate statistics.

Smoke trials store exact expected and recovered locations. Large trials may
replace each exact list with `{count, sha256}` only after the harness has compared
the canonical exact lists in memory. Canonicalization means unsigned numeric
ascending order, duplicates retained, each logical `uint64` encoded as exactly
8 big-endian bytes, concatenated without delimiters, then SHA-256 hashed.

## 2. Server online cost

The derived total is:

`server_online_total_ms = signal_admission_batch_wall_ms + retrieval_online_wall_ms`

Admission and retrieval must also remain separately reported. The total exists
only when both phase statuses are `ok`; otherwise it is absent and all metrics
for an unimplemented/unsupported/error phase are `null`, not zero.

For a two-server scheme, every online phase records:

- `critical_path_wall_ms`: synchronized elapsed wall time until both servers
  complete the phase;
- `server_cpu_ms_sum`: the sum of CPU time consumed by both server processes;
- `per_server_cpu_ms`: one labeled value per server, whose sum equals the prior
  field before reporting precision is applied;
- `inter_server_bytes`: application/channel bytes exchanged by the two servers.

Single-server schemes use one `per_server_cpu_ms` entry and normally record zero
measured inter-server bytes. `inter_server_bytes` is auxiliary and must never be
included in server-to-recipient response bytes.

## 3. Unified workload and function

The primary comparison asks each server implementation to help the target
recipient recover the same deterministic set of opaque logical location handles.
Logical handles are `uint64` values. Functional equivalence does not require an
equal wire width: each scheme must use its actual protocol representation, and
encoding overhead is part of its measured communication.

- PSVFSS recovers location handles represented by its point-function payload.
- PPS-GC recovers location handles from the target table row. Parameters are
  `m=Nr` and `ell=k_cap`; `ell` is never inferred from `Ns`, and `k_actual` is
  independent of row capacity.
- FMD returns candidate-board locations. The response includes true matches and
  false positives; false positives are retained and counted, never filtered by
  benchmark knowledge before serialization.
- OMR's primary workload encodes the same logical location handle as its payload.
  The existing 612-byte application-payload experiment is a supplementary
  workload and must not be mixed into the primary location-only communication
  plot. OMR uses `kbar=k_cap`, not `k_actual`.

The manifest in `workload.schema.json` is authoritative. All four schemes read
the same `match_positions`, `location_values`, and `recipient_assignment`.
Scheme code must not independently choose matches with `random_device`,
`crypto/rand`, or current time. Runtime validation additionally enforces:

- array lengths equal `Ns` where specified;
- match positions are unique and in `[0,Ns)`;
- `match_positions.length == k_actual` and `k_actual <= k_cap`;
- exactly those positions, and exactly `k_actual` signals, are assigned to
  `target_recipient`;
- `query_count == 1` for this experiment;
- `fmd_false_positive_rate == 2^-fmd_gamma` to the manifest's decimal precision.

## 4. Communication

`signaling_response_bytes` (named `server_to_recipient_bytes` in raw phase data)
is the sum of the lengths of every complete application-layer response buffer
constructed by all servers for one retrieval. It excludes request bytes,
TCP/IP/TLS/socket headers, server-to-server traffic, recipient-local objects,
and theoretical estimates.

`payload_download_bytes` records any later download of a common ciphertext or
payload referenced by the signaling response. `end_to_end_bytes` is the measured
sum of signaling response and payload download buffers. The primary signaling
communication plot uses `signaling_response_bytes` only.

Response encodings are:

- `artifact_native`: an existing artifact serializer, such as SEAL
  `Ciphertext::save`;
- `benchmark_defined`: a minimal deterministic serializer added only when the
  artifact has no wire format. It must serialize every required response object,
  specify byte order, element widths, length prefixes, and framing, construct a
  real byte buffer, and count that buffer. `count * sizeof` is forbidden.

### 4.1 Response measurability

| Scheme | Intended response object | Currently implemented object | Minimal serializer allowed later | Encoding source | Measurable now |
|---|---|---|---|---|---|
| PSVFSS | Two ordered compressed additive-share vectors over `Z_q`, one vector per server | Both servers currently obtain the same plaintext shuffled-index vector; the harness also performs diagnostic-only full-vector openings, but no compressed share-vector response is constructed | Benchmark-defined framing is pending: it must preserve server identity, vector order, element count, and every native field element without substituting opened indices or a full vector | benchmark_defined_pending | No; semantic status is `resolved_from_paper`, response measurement is `not_implemented`, bytes `null` |
| PPS-GC | One target-row location share from each server, each of `k_cap` entries | Local benchmark XOR of two in-memory rows; no server response | Benchmark-defined framing after retrieval semantics are implemented: version, server id, entry count, then all native location-share entries in documented native width and byte order | benchmark_defined | No; retrieval/response not implemented |
| FMD | Candidate board-location handles, including false positives | In-memory candidate indices | Benchmark-defined framing: version, candidate count as unsigned 64-bit big-endian, followed by every candidate's actual protocol handle encoding in deterministic board order | benchmark_defined | No; serializer absent |
| OMR | Primary: final index/detection ciphertext plus final packed location-handle payload ciphertext. Supplementary: the same digest form for full application payloads | `lhs_multi[0]` and `rhs_multi[0]` SEAL ciphertexts | Serialize both ciphertexts independently with `Ciphertext::save`; frame as version, object count, and unsigned 64-bit big-endian length before each native blob | artifact_native (inside benchmark-defined outer framing) | Yes after framing and serialization timer are instrumented |

The proposed PPS-GC and FMD framing fixes only transport ambiguity; it must not
change their protocol response objects or pad them to 32 bytes for comparison.

## 5. PSVFSS frozen boundary and paper-resolved response semantics

No new compression algorithm or wire format is selected by this contract.  The
protocol response semantics are resolved from the supplied paper: Section 4;
Sections 5.1 and 5.2; page 6 Figure 2 and Algorithm 1; Section 7.3; page 9
Figure 5; and page 12 Table 3.

The semantic contract is:

1. `Cprs([x])` outputs `[z] = (z_0, z_1)`.
2. After Shuffle, the servers hold additive shares `[v] = (v_0, v_1)` over
   `Z_q`.
3. The servers open only masked values `t_j = v_j * rho_j` and form
   `Z = {j | t_j = 0}`.
4. Server `b` locally retains the ordered vector
   `z_b = (v_b[j])` for every `j` not in `Z`.
5. Server 0 and Server 1 send `z_0` and `z_1`, respectively.  The recipient
   reconstructs corresponding elements with field addition and applies
   `LocDec` to recover logical locations.

Thus:

- `response_semantic_status = resolved_from_paper`;
- response object = `two ordered compressed additive-share vectors over Z_q,
  one vector per server`;
- `wire_encoding_status = benchmark_defined_pending`;
- `measurement_status = not_implemented`;
- `server_to_recipient_bytes = null`.

Plaintext shuffled indices, opened `v_j` values, and diagnostic full vectors are
not protocol responses.  Table 3's `2*k*8` expression (including `k=50` giving
800 bytes) is only a theoretical bare-field-element payload sanity check.  It
does not include a concrete serializer/framing and must never populate a raw
trial communication field.

The current implementation gap remains:

- `pvfss/src/server.cpp:450-469` multiplies every shuffled element, exchanges the
  two results, opens their sum at both parties, and appends plaintext indices to
  each local `sig` vector.
- `pvfss/src/server.h:30` and `pvfss/src/server.cpp:450` currently expose only
  `std::vector<uint64_t>& sig`, which is the opened zero/nonzero index set rather
  than `z_b`.
- `pvfss/src/test.cc` calls `CPRS` with that index vector and never constructs,
  serializes, or sends either ordered compressed additive-share vector.

The next implementation phase must minimally change the Cprs output boundary in
`pvfss/src/server.h` and `pvfss/src/server.cpp::CPRS`, then update the call and
recipient reconstruction in `pvfss/src/test.cc`.  It must preserve the current
masking, `Mult` calls, opening used to derive `Z`, ordering, and compression
mathematics.  Only after actual response buffers exist may a deterministic
benchmark serializer and measured response-byte counter be added.

## 6. Scheme lifecycle commitments

| Scheme | Admission online | Retrieval online | Current status |
|---|---|---|---|
| PSVFSS | Parse signal shares, verify VDPF proofs, and append/commit function shares to retrieval state | Eval, Shuffle, existing Cprs, then serialize the protocol-defined response | response semantics resolved from paper; compressed share-vector construction and serialization not implemented; current debug path is not admissible measurement |
| PPS-GC | Both role-swapped GC updates, all table/index rerandomization, and state assignment commit | Target-row lookup and serialization only; no table update | admission computation implemented; service admission and retrieval response not implemented |
| FMD | Parse and append/store each already-generated flag; no invented validation | Test all `Ns` flags, collect all candidates, serialize candidate handles | core detection implemented; admission service and serialization not implemented |
| OMR | Parse/append clue and associate preloaded payload state; file creation/loading excluded | Homomorphic detection, masking, index and payload-handle packing, digest construction, final serialization | full core implemented; timer boundary and complete framed response measurement need instrumentation |

## 7. Security-parameter disclosure

No result may claim that all schemes have the same formal security level.

- PSVFSS: source declares 128-bit security and a 64-bit DPF input domain; shares
  use the field `2^61-1`. The exact end-to-end proof/security claim is not
  independently established by this artifact.
- PPS-GC: the implementation uses 128-bit block-based garbling/OT primitives;
  the exact instantiated security claim must be recorded from the pinned
  dependency/build configuration.
- FMD: P-256 with manifest-fixed `gamma`; `gamma` determines the false-positive
  rate and is not a conventional security-bit parameter.
- OMR: the current BFV context is constructed with `sec_level_type::none`.
  This is a comparability limitation and must be printed in every OMR raw trial.

This phase does not alter cryptographic parameters.

## 8. Raw trials, nulls, and aggregation

Each JSONL line validates against `raw_trial.schema.json` and represents one
actual trial, never an aggregate. Mean, median, standard deviation, confidence
interval, or other cross-trial summaries are forbidden in raw rows.

`null` means not applicable, not implemented, unsupported, unresolved, or not
measured as explained by the associated status. Zero is reserved for an
actually executed measurement whose result is zero. In particular, missing
phases and formula-based communication must never be written as zero.

Only rows with `measurement_status=ok` and `correctness_ok=true` may enter paper
statistics.

## 9. Resumable execution

The unique trial key is the ordered tuple:

`scheme + workload_id + trial_index + source_snapshot_id + binary_sha256`

A runner may skip a trial only when exactly one complete, schema-valid JSONL row
with that key already exists and has both `measurement_status=ok` and
`correctness_ok=true`. Failed, truncated, duplicate, invalid, unsupported,
not-implemented, old-source, old-binary, or correctness-false rows never satisfy
the resume condition and must be rerun when the underlying phase becomes
available.
