# Client benchmark v2

This directory is separate from the frozen legacy results. `raw/` contains one
CSV file per scheme, experiment, parameter point, and trial. A completed trial
contains one row per client metric. Invalid, failed, and timeout trials have no
timing value and are excluded from `summary/`.

Timing values in raw files are integer nanoseconds. Summary timing values are
floating-point milliseconds. `logs/` retains the complete adapter output.

The benchmark measures protocol roles (`sender`, `recipient`) independently of
the physical host. It does not include network RTT or server timing.

