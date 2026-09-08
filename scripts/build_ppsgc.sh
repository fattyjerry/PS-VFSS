#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${root}/baselines/pps-gc/pps-garbled-circuits"
cargo +nightly-2021-05-06 build --locked --release --example unified-bench
