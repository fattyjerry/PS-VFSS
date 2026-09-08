# Fast pilot run notes

This directory is a pilot result tier: one warmup followed by three measured
trials where the artifact completed.  No confidence intervals or paper-scale
runs were performed.

## Boundaries

- FMD admission is `unsupported`, not zero. Retrieval tests every flag and then
  serializes `uint32 count` plus all candidate `uint64` handles, little-endian.
  False positives remain in the response.
- PPS-GC admission contains all real two-round GC table updates, rerandomization,
  and commits. Retrieval only copies the target row and serializes two buffers;
  each buffer is `uint32 count` plus four native 32-byte row shares.
- PSVFSS BTGen framing now uses only NetIO with a fixed little-endian 4-byte
  length. A single unbuffered connection completed the full `Ns=2` correctness
  path, but `Ns=4` remained nondeterministically blocked in BTGen, so no PSVFSS
  pilot row or response byte was emitted.
- OMR setup, file loading, graph generation, and diagnostics were removed from
  the online accumulator. The N=32 point did not produce two final response
  ciphertexts and is outside this pilot summary.

`raw_trials.jsonl` intentionally carries `result_tier=pilot` and the compact
pilot fields emitted by the instrumented artifacts. It is not claimed to satisfy
the frozen formal raw-trial schema; formal-schema runner integration remains
deferred as requested by FAST PILOT RESULTS mode.
