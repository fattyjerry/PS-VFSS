# Server-online comparison

This experiment separates signal admission, retrieval, response serialization,
and recipient processing for PVFSS, PPS-GC, FMD, and OMR.

- `EXPERIMENT_CONTRACT.md`: workload and measurement boundary
- `workload.schema.json`: common workload format
- `raw_trial.schema.json`: raw result format
- `results`: campaign runners, measurements, and run notes

## Result validity

- FMD rows with passed correctness are valid end-to-end pilot measurements.
- PPS-GC rows without recorded correctness remain pilot measurements.
- PVFSS `component_composed_pilot` rows use local trusted preprocessing and
  failed end-to-end correctness; use them only as component diagnostics.
- OMR `reduced_parameter_pilot` rows use reduced parameters with
  `sec_level_type::none` and failed recipient correctness; they are diagnostic
  and not security-comparable.

Missing or failed points are not replaced with zero or interpolated. Check each
row's measurement quality, correctness, logical/physical size, parameter mode,
and adjacent `RUN_NOTES.md` before using it. These pilots supplement rather than
replace the main workload driven by `scripts/run_unified_bench.py`.
