# Preliminary pilot results — V2

FMD and PPS-GC rows are copied numerically from `results/pilot/raw_trials.jsonl`;
those old files were not modified or rerun. PPS-GC `admission_online_ms` is the
wall time for the complete batch of N signal updates, so V2 names it
`admission_batch_online_ms` and derives per-signal time by division.

PSVFSS used `local_trusted_pilot` preprocessing after real BTGen remained
unreliable. The dealer correlations were mathematically valid for the block
shuffle, but the resulting low-64-bit block shares cannot be independently
reduced into field61 additive shares: the boundary location `p-1` reconstructed
as 7. No PSVFSS result or response byte is accepted.

OMR runtime reports `poly_modulus_degree=32768` and `slot_count=32768`. Logical
N=32/64/128 is padded to 32768 physical lanes, with four targets restricted to
the logical range. N=32 reached homomorphic range checking but exceeded the
600-second warmup limit before final ciphertexts; N=64/128 confirmed the same
physical padding and were stopped early. No OMR time or response byte is used.

Per the user's request, this V2 output contains data only and generates no
plots. Missing/artifact-limited PSVFSS and OMR values remain null, not zero.
