# PVFSS Artifact

This repository contains the PVFSS prototype, the PPS-GC baseline used in
the evaluation, benchmark drivers, and the data reported by the paper.
The code is a research prototype and is not intended for production use.

## Repository layout

- `pvfss/`: two-party PVFSS implementation and benchmark drivers
- `vdpf/`: bundled DPF/VDPF implementation used by PVFSS
- `baselines/fmd/`: Fuzzy Message Detection baseline
- `baselines/omr/`: Oblivious Message Retrieval baseline
- `baselines/pps-gc/`: PPS garbled-circuit baseline and its pinned dependencies
- `scripts/run_unified_bench.py`: common benchmark entry point
- `results/`: paper data and measurement notes

## Evaluation workload

The main comparison fixes `ell=50` and evaluates
`N={256,512,1024,2048,4096,8192,16384}`. All output uses this schema:

```text
scheme,N,ell,setup_ms,send_ms,server_ms,recipient_ms,comm_bytes,status,bottleneck
```

Rows with `status=timeout` preserve the experimental hard limit and do not
claim a completed runtime. See `results/README.md` for details.

## Build PVFSS

The tested environment is Linux x86-64 with GCC, CMake 3.21+, OpenSSL,
RELIC, EMP Toolkit, and cryptoTools. The processor must support AES-NI and
AVX2 because the prototype is compiled with `-maes -mavx2 -march=native`.
The preprocessing code requests a 3072-bit Paillier key. RELIC must therefore
use dynamic big-number allocation (`ALLOC=DYNAMIC`) or be built with
`BN_PRECI=3072`; a smaller static precision fails with `ERR_NO_PRECI`.

Set the dependency prefixes and build:

```bash
cmake -S pvfss -B pvfss/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/path/to/relic;/path/to/cryptoTools" \
  -DEMP_ROOT=/path/to/emp
cmake --build pvfss/build --parallel
```

`EMP_ROOT` must contain `include/emp-tool` and `include/emp-ot`. The bundled
VDPF library is built automatically. Use `-DVDPF_ROOT=/other/path` only when
testing another VDPF checkout.

Run a small two-party smoke test:

```bash
python3 pvfss/run_psvfss_unified.py \
  --binary pvfss/build/src/test --ns 16 --T 4 --ell 3 --online-reps 1
```

Run the admission-time VDPF verification tests:

```bash
pvfss/build/src/admission_test
```

The test simulates both non-colluding servers on the same ordered registered
identifier sequence. It checks that an honest key pair is accepted and stored,
while inconsistent key shares, tampered proofs, and duplicate signal
identifiers are rejected without changing either verified signal-key database.

## Build baselines

Build FMD:

```bash
scripts/build_fmd.sh
```

OMR requires PALISADE, Microsoft SEAL 3.6, NTL, and GMP discoverable by
CMake:

```bash
scripts/build_omr.sh
```

Build PPS-GC:

The baseline is pinned to its original 2021 Rust toolchain. On newer stable
Rust, the benchmark can also be built with `RUSTC_BOOTSTRAP=1`:

```bash
cd baselines/pps-gc/pps-garbled-circuits
RUSTC_BOOTSTRAP=1 cargo +stable build --release --example unified-bench
cd ../../..
```

Run the small end-to-end validation:

```bash
python3 baselines/pps-gc/run_ppsgc_unified.py \
  --ns 4 --ell 2 --per-n-limit-sec 300
```

## Reproduce the common benchmark

After building both binaries:

```bash
python3 scripts/run_unified_bench.py
```

The command writes `results/latest.csv`. OMR and PPS-GC use a 300-second
per-`N` limit by default. A timeout is a valid experimental outcome and the
runner continues with the remaining values of `N`.

To run only one implementation or a reduced workload:

```bash
python3 scripts/run_unified_bench.py pvfss --ns 256,512
python3 scripts/run_unified_bench.py fmd --ns 256,512
python3 scripts/run_unified_bench.py omr --ns 256 --omr-timeout-sec 300
python3 scripts/run_unified_bench.py pps-gc --ns 256 --pps-timeout-sec 300
```

## Provenance

This artifact contains modified research code from VDPF, Secret-Shared
Shuffle, PPS-GC, and Swanky. See `THIRD_PARTY.md` for source links and the
scope of the benchmark modifications.
