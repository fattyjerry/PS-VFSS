# PSVFSS current-path online breakdown

- Binary: `pvfss/build-paillier3072/src/test`
- Binary SHA-256: `17c18edd37f88c8ad6ac14b7a0bcd76b85eb72673416a66c877d078d7c074512`
- Parameters: Ns={256,512,1024,2048,4096}, k=50; one warmup and three measured trials.
- Same command path as the current latency runner: local trusted preprocessing and component pilot.
- Offline preprocessing, connection setup, diagnostics, and correctness checks are excluded.
- Critical server components all come from the same slower party for each trial.
- The current binary differs from the archived pilot_fast8h binary because recipient timing instrumentation was added; update the PSVFSS latency curve from this campaign's Server_Retrieval_Total_ms.
- Archived pilot_fast8h binary SHA-256: `13e793aa8e0232d976136da030e63692786b2275665301f09a7852e476a06ef0`.
- All 15 measured trials completed. The maximum absolute per-trial difference between the recorded server total and its component sum is `0.005 ms` (integer-microsecond rounding).
- A few first attempts stalled in the local two-process Cprs synchronization; those attempts were terminated at the phase timeout and retried on a fresh port. They were not written as measurements.
- End-to-end correctness remains failed in component-composed pilot mode; results are preliminary.
