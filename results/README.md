# Evaluation data

`paper_main.csv` contains the common workload used for PVFSS, FMD, OMR,
and PPS-GC:

- `ell = 50`
- `N = 256, 512, 1024, 2048, 4096, 8192, 16384`
- PPS-GC hard limit: 300 seconds per value of `N`
- OMR hard limit: 300 seconds per value of `N`
- FMD repetitions: 5

Zero timing fields on a `timeout` row mean that the run did not complete
within the hard limit. They are not measured zero-cost operations.

`pvfss_breakdown.csv` reports one offline setup and the average of five
online repetitions. The component timings are in milliseconds.
