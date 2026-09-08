# Server-online comparison

This experiment separates signal admission, retrieval, response serialization,
and recipient processing for PSVFSS, PPS-GC, FMD, and OMR.

- `EXPERIMENT_CONTRACT.md`: workload and measurement boundary
- `workload.schema.json`: common workload format
- `raw_trial.schema.json`: raw result format
- `results`: campaign runners, measurements, and run notes

## Result organization

- FMD and PPS-GC directories contain native baseline pilot measurements.
- PSVFSS component campaigns record their preprocessing mode and timing split.
- OMR campaigns record the BFV degree, physical slot count, and security mode.
- Larger-workload and recipient-processing studies are stored separately from
  the common workload.

For each result, use the recorded measurement tier, logical/physical size,
parameter mode, source hash, and adjacent `RUN_NOTES.md`. These experiments
supplement the main workload driven by `scripts/run_unified_bench.py`.
