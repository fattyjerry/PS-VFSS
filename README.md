# PSVFSS Artifact

C++ research prototype of PVFSS with FMD, OMR, and PPS-GC baselines. The
repository contains the protocol implementation, benchmark drivers, and
evaluation data.

## Features

- Two-party PSVFSS implementation with admission-time VDPF verification
- FMD, OMR, and PPS-GC comparison implementations
- Unified end-to-end benchmark driver
- VerEval, sender/client cost, and server-online component experiments
- Raw measurements and reproducible summaries

## Parameters

| Item | Setting |
|---|---|
| Security parameter | 128 bits in the PSVFSS implementation |
| VDPF value field | `2^61-1` |
| Main workload | `ell=50`, `N=256 ... 16384` |
| OMR/PPS-GC per-point limit | 9000 seconds |

The baselines retain their native parameters; the repository does not claim
that every scheme has an identical formal security level.

## Requirements

- Ubuntu 22.04 or compatible Linux x86-64
- CMake 3.21+, GCC, Python 3, Go, and Rust
- OpenSSL, RELIC, EMP Toolkit, cryptoTools, PALISADE, SEAL, NTL, and GMP
- AES-NI, PCLMULQDQ, and AVX2

PSVFSS uses 3072-bit Paillier preprocessing. RELIC must use dynamic big-number
allocation or support at least 3072-bit precision.

## Build

```bash
cmake -S psvfss -B pvfss/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/path/to/relic;/path/to/cryptoTools" \
  -DEMP_ROOT=/path/to/emp
cmake --build pvfss/build --parallel

scripts/build_fmd.sh
scripts/build_omr.sh
scripts/build_ppsgc.sh
```

Run the admission regression test:

```bash
pvfss/build/src/admission_test
```

## Experiments

Run the common four-scheme workload:

```bash
python3 scripts/run_unified_bench.py \
  --fmd-gamma 8 --omr-timeout-sec 9000 --pps-timeout-sec 9000
```

Additional entry points:

- `scripts/run_vereval_scaling.sh`: admission-time VerEval scaling
- `results/run_sender_scalability_logspace.sh`: sender signaling scaling
- `scripts/run_client_v2.py`: sender/recipient client smoke benchmark
- `experiments/server_online_comparison`: server-online component experiments

See `results/README.md` for the data index and experiment tiers.

## Layout

- `psvfss`: PVFSS implementation
- `vdpf`: bundled DPF/VDPF implementation
- `baselines`: FMD, OMR, and PPS-GC
- `scripts`: build and benchmark drivers
- `experiments`: extended experiments and notes
- `results`: measurements and summaries

## Notes

This is an experimental research prototype. It has not been independently
audited and should not be used in production.
