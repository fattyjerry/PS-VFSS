# Preliminary pilot results — V2

FMD and PPS-GC rows are copied numerically from `results/pilot/raw_trials.jsonl`;
those old files were not modified or rerun. PPS-GC `admission_online_ms` is the
wall time for the complete batch of N signal updates, so V2 names it
`admission_batch_online_ms` and derives per-signal time by division.

PSVFSS used `local_trusted_pilot` preprocessing. This component-composed mode is
recorded separately from the native end-to-end configuration, and its timing
and response fields retain that measurement label.

OMR runtime reports `poly_modulus_degree=32768` and `slot_count=32768`. Logical
N=32/64/128 is padded to 32768 physical lanes, with four targets restricted to
the logical range. N=32 reached homomorphic range checking but exceeded the
pilot window before final ciphertexts; N=64/128 confirmed the same physical
padding. These points are outside the timing and response-byte summary.

This V2 output contains data only and generates no plots. Fields outside the
campaign scope remain null.
