# FAST PILOT V3 data notes

This directory contains data only; no plots were generated per the user's final
instruction. FMD and PPS-GC trials are inherited unchanged from
`results/pilot/raw_trials.jsonl` and were not rerun.

PSVFSS uses local trusted preprocessing and component composition. Admission
times real VDPF evaluation, proof comparison, and accepted-key-state append.
Retrieval measures a second real VDPF evaluation plus the artifact's real
Shuffle, Mult, masked-zero detection, Cprs, and serialization. Because the
artifact has no valid private field61-to-block share conversion, compression is
fed a separately constructed sparse field61 vector. The two 36-byte response
buffers are real (`uint32` count plus four `uint64` shares), but end-to-end
correctness is false.

OMR uses BFV `poly_modulus_degree=slot_count=4096`, the artifact's full
homomorphic operation sequence, four-`uint64` location payloads, and native
SEAL `Ciphertext::save`. The response is two ciphertext blobs plus two 8-byte
length fields. The reduced configuration completes and yields real timing and
buffer sizes, but its recipient correctness check fails and its security is not
comparable (`sec_level_type::none`). One failed N=64 process (rep 1) produced no
PILOT_JSON and is excluded; rep 4 replaces it.

For all schemes, summary values are medians of three measured trials after one
warmup. Null admission for FMD/OMR means unsupported, not zero.
