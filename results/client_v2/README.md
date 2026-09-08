# Client benchmark v2

This directory is separate from the frozen legacy results. `raw/` contains one
CSV file per scheme, experiment, parameter point, and trial. A completed trial
contains one row per client metric. `summary/` aggregates completed trials.

Timing values in raw files are integer nanoseconds. Summary timing values are
floating-point milliseconds. `logs/` retains the complete adapter output.

The benchmark measures protocol roles (`sender`, `recipient`) independently of
the physical host. It does not include network RTT or server timing.

## Scope and status

The committed `client-v2-smoke` run uses `Ns=16`, `Nr=2`, `k=1`, and three
requested measured trials. It validates the resumable harness and output
schema; the summary reports the completed scheme/metric combinations.

## Reproduce

Build the scheme binaries first, then run:

```bash
python3 scripts/run_client_v2.py \
  --Ns 16 --Nr 2 --k 1 --trials 3 \
  --run-id client-v2-reproduced
```

The runner is resumable: a trial is skipped only when its raw CSV already ends
in a completed state. Use `--force` to rerun an existing completed trial.

## Output layout

- `raw/<run-id>/<scheme>/<parameter-point>/trial-*.csv`: one file per trial;
- `summary/<run-id>.csv`: aggregates of valid measured trials only;
- `metadata/raw_schema.csv`: field description;
- `logs/`: adapter output generated locally and excluded from Git.

The four metrics are sender signaling generation, sender serialization, sender
total online work, and recipient processing, all recorded in integer
nanoseconds in raw files and milliseconds in summaries.
