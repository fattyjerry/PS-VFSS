# PSVFSS breakdown extension: Ns=8192 and Ns=16384

- These rows were extracted from the exact measured trials used by the current server-latency extension, not from a separate rerun.
- Source logs: `../latency_extension_8192_16384/logs/ps_N{8192,16384}_measured{0,1,2}_{alice,bob}.log`.
- Binary SHA-256: `17c18edd37f88c8ad6ac14b7a0bcd76b85eb72673416a66c877d078d7c074512`.
- Parameters: `k=50`, component-pilot measurement, local trusted preprocessing, three measured trials per Ns.
- Offline preprocessing, connection setup, diagnostics, and correctness checking are excluded from every online field.
- For each trial, all server components are taken from the same critical (slower) party selected by `retrieval_online_ms`.
- Per-trial server totals close against Eval + Shuffle + Cprs + Serialization within 0.006 ms at Ns=8192 and 0.021 ms at Ns=16384. The larger +/-0.504 ms shown for the sum of medians is solely because each component is independently summarized by its median.
- Cprs accounts for 99.583% and 99.564% of median server retrieval time at Ns=8192 and Ns=16384, respectively.
- Component-composed end-to-end correctness remains false; these are preliminary timing data and must not be described as a correctness-passing end-to-end protocol run.
