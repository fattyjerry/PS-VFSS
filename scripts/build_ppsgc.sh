#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${root}/baselines/pps-gc/pps-garbled-circuits"
RUSTC_BOOTSTRAP=1 cargo +stable build --release --example unified-bench
