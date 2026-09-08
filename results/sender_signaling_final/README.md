# Sender signaling microbenchmarks

These files measure sender-side signaling-object generation for PSVFSS, FMD,
PPS-GC, and OMR. They exclude server processing, recipient processing, network
latency, and unrelated setup.

## Preferred summaries

- `single_signal_summary.csv`: one-signal distribution and confidence interval;
- `multiple_signal_summary.csv`: total and per-signal cost for repeated batches;
- `single_vs_repeated_reference.csv`: comparison of the two experiment forms;
- `scaling_fit_final.csv`: linear fit over the completed repeated-signal data.

The `*_raw*.csv` files are retained for audit. Files with `_v2` or `final` in
their name supersede the earlier same-purpose intermediate file where both are
present. `fmd_1500_summary.csv` includes the additional FMD samples used to
stabilize its single-signal estimate.

## Interpretation

The benchmark reports native scheme operations and therefore does not assert
identical wire formats or equal formal security levels. Output-byte fields are
reported when the implementation constructs the corresponding output object.
Compare timings only under the
recorded compiler, build type, CPU-affinity, recipient mode, and scheme-specific
parameters.

For the cleaner common batch grid `B={10,32,100,316,1000}` with 100 measured
trials per point, prefer `../sender_scalability_logspace_summary.csv` and its
runner `../run_sender_scalability_logspace.sh`.
