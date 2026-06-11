# Third-party code and provenance

## VDPF

`vdpf/` is the "Verifiable Distributed Point Functions" implementation
associated with <https://eprint.iacr.org/2021/580>. Its MIT license text is
included in `vdpf/README.md`.

## Secret-Shared Shuffle

The PVFSS prototype started from an unofficial implementation of
Secret-Shared Shuffle (<https://eprint.iacr.org/2019/1340>). The artifact
adds the PVFSS protocol integration, correctness checks, benchmark output,
and the paper workload driver.

## PPS-GC

`baselines/pps-gc/pps-garbled-circuits/` is derived from the PPS-GC code
published by ZenGo-X:
<https://github.com/ZenGo-X/pps-gc>.

The artifact adds `examples/unified-bench.rs`, which maps the paper workload
to the baseline table parameters, processes updates in native batches, checks
the reconstructed count, and emits the common CSV schema.

## Swanky

`baselines/pps-gc/vendor/swanky/` contains the subset of Swanky required by
PPS-GC. The upstream license is retained at
`baselines/pps-gc/vendor/swanky/LICENSE`. Cargo path dependencies are used so
the baseline remains pinned and does not silently move to newer Git commits.

## Fuzzy Message Detection

`baselines/fmd/` is derived from the Fuzzy Message Detection research code:
<https://github.com/becgabri/fuzzycrypto>. The artifact adds
`cmd/fmdbench`, which runs the common `N`/`ell` workload, checks true
positives, averages repetitions, and emits the common CSV schema.

## Oblivious Message Retrieval

`baselines/omr/` is derived from the Oblivious Message Retrieval research
code. The artifact adds parameterized benchmark mode, stage markers,
correctness reporting, and `run_omr_unified.py`. OMR depends on PALISADE and
Microsoft SEAL 3.6; these large third-party dependency trees and generated
datasets are not copied into this repository.
