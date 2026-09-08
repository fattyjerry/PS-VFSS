# Phase 3A.2 PSVFSS evidence

## Source and paper identity

- Phase 3A after hashes verified before this phase: all four entries passed.
- Supplied paper SHA-256:
  `a648dcb89bd6ef52b24c0c43a2cb397c1e2a23cc3fdcf568db17b8a4d977686b`.
- The response contract uses the supplied paper's Section 4, Sections 5.1/5.2,
  page 6 Figure 2 and Algorithm 1, Section 7.3, page 9 Figure 5, and page 12
  Table 3.  No repository behavior was substituted for the paper semantics.

## Host

- WSL2 kernel: `6.18.33.2-microsoft-standard-WSL2`, x86-64.
- Memory: 14 GiB total, approximately 12 GiB available during diagnosis.
- Swap: 4 GiB total, unused.
- CPUs: 16.
- Stack limit: 8192 KiB.
- Compiler: Ubuntu GCC/G++ 11.4.0.
- No residual process named `test` remained after bounded runs.

## Build audit

All builds use Release, `-O3 -DNDEBUG`, C++20, `/usr/bin/c++`, and the same
current source tree.  Their RELIC configurations differ:

| Build | RELIC library | `BN_PRECI` | `ALLOC` | Source calls Paillier modulus | Current Phase 3A.2 `test.cc` object |
|---|---|---:|---|---:|---|
| `pvfss/build` | `/home/zjr/.local/psvfss/lib/librelic_s.a` | 1024 | AUTO | 3072 | stale |
| `pvfss/build-paillier3072` | `relic-install-paillier3072/lib/librelic_s.a` | 3072 | AUTO | 3072 | stale |
| `pvfss/build-sender-smoke` | `relic-install-paillier3072-bn6144/lib/librelic_s.a` | 6144 | AUTO | 3072 | current |

The Paillier modulus is fixed by `pvfss/src/server.cpp::BTGen` at
`cp_phpe_gen(pub, prv, 3072)`, independently of the directory name.  Paillier
ciphertext arithmetic uses a modulus-square value of approximately 6144 bits.
With `ALLOC=AUTO`, the 3072-capacity build raises `ERR_NO_PRECI` in
`relic_bn_mem.c:80-81` and `:131-134`; it cannot hold that intermediate.  The
BN6144-capacity build is therefore the compatible build for the paper's
Paillier-3072 modulus, not a different Paillier security parameter.

The three `test.cc.o` files were identical at the Phase 3A baseline.  After the
one-line Phase 3A.2 harness fix, only `build-sender-smoke` was rebuilt and is
current; the other two binaries are explicitly stale relative to Phase 3A.2.

## Reproducible runtime evidence

### `build-paillier3072`, `Ns=2`, `T=2`, `k=1`

- Both logical servers started and connected.
- ALICE emitted repeated RELIC `ERR_NO_PRECI` errors followed by stack-smashing
  termination.
- Both wrapper exit codes: 134 (SIGABRT).
- Elapsed: approximately 2.35/2.37 seconds.
- Peak RSS: 7576/7720 KiB.
- Last phase: BTGen; Offline and Eval were never entered.
- This is not OOM: host had about 12 GiB available and swap was unused.

### Main-channel Offline deadlock before the harness fix

With BN6144, both servers completed BTGen, then timed out after 180 seconds.
The `Ns=2` syscall trace showed BOB writing a 69-byte first-IKNP frame while
ALICE issued a 1,048,576-byte buffered `read`; both then waited.  `offline done`
was never printed.  This directly identified the main `NetIO` 1 MiB stdio
buffer as one blocker.  `pvfss/src/test.cc` now selects `_IONBF`; no protocol
frame, random value, algorithm, or message ordering was changed.

### Remaining BN6144 BTGen blocker after the harness fix

Bounded `Ns=2` runs still do not reliably complete BTGen.  In the 25-second
trace:

- ALICE sent three framed BN values of 768, 768, and 384 bytes on the dedicated
  BTGen socket, then blocked in `recv_bn(Ev)` waiting for its 4-byte length.
- BOB received all three frames, sent a framed 768-byte result, printed its
  BTGen timing marker, then blocked waiting for the following BN frame.
- Both were terminated by the timeout (exit 124); no OOM or RELIC error occurred.
- Offline, Eval, Shuffle, Cprs, and recipient correctness were not reached.

The remaining blocker is narrowed to BTGen's dedicated-socket BN exchange,
specifically the raw-socket `send_bn`/`recv_bn` sequence around
`server.cpp:249` and `server.cpp:338`, after successful Paillier computation.
The evidence is consistent with a framing/socket-state problem caused by mixing
the `NetIO` FILE stream and direct `send`/`recv` access on the same descriptor;
the exact defect is not yet proven, so no protocol-source fix was made.

## Ordered smoke status

| Smoke | Build | Last stage | Server exits | Correctness |
|---|---|---|---|---|
| `Ns=2,k=1` | BN_PRECI=3072 | BTGen capacity failure | 134 / 134 | not reached |
| `Ns=2,k=1` | BN_PRECI=6144 | BTGen framed exchange | 124 / 124 | not reached |
| `Ns=4,k=1` | not run | gated by Ns=2 | N/A | not reached |
| `Ns=8,k=2` | not run | gated by Ns=2 | N/A | not reached |
| `Ns=32,k=4` | not run | gated by smaller smoke | N/A | not reached |
| `Ns=256,k=10` | not run | gated by Ns=32 | N/A | not reached |

Timeouts and failures are diagnostics only and are not performance data.

## Response status

- Semantic status: `resolved_from_paper`.
- Object: two ordered compressed additive-share vectors over `Z_q`, one vector
  per server.
- Wire encoding: `benchmark_defined_pending`.
- Measurement: `not_implemented`.
- Measured server-to-recipient bytes: `null`.

The paper's `2*k*8` and `k=50 -> 800 B` values count bare field elements.  No
implemented serializer currently constructs complete application response
buffers, so these values remain theoretical sanity checks only.
