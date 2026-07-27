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

## VerEval scaling

`vereval_scaling/formal/` contains the formal VerEval measurements for
the ordered registered-recipient set `X`. The experiment covers:

- `|X| = 10,000, 100,000, 1,000,000`;
- `threads = 1, 2, 4, 8, 16`;
- two warm-up runs followed by ten measured repetitions per configuration;
- a 50-bit VDPF input domain.

Each CSV row records the two local server times and
`protocol_wall_ms`, defined as the maximum of those two times to model
concurrent server execution. The paper reports the median
`protocol_wall_ms` over the ten measured repetitions.

The `proof_equal` and `outputs_correct` columns must both equal `1`.
All 150 formal measurement rows satisfy these checks. The host and
toolchain used for the formal run are recorded in
`vereval_scaling/formal/environment.txt`.

Reproduce the complete matrix with:

```bash
bash scripts/run_vereval_scaling.sh
```

By default, new output is written to
`results/vereval_scaling/reproduced/` so that the committed formal data
is not overwritten. Raw progress logs and build products are not
versioned.
