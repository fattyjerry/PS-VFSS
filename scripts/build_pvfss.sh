#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

: "${EMP_ROOT:?set EMP_ROOT to the EMP installation prefix}"
: "${CMAKE_PREFIX_PATH:?set CMAKE_PREFIX_PATH for RELIC and cryptoTools}"

cmake -S "${root}/pvfss" -B "${root}/pvfss/build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}" \
  -DEMP_ROOT="${EMP_ROOT}"
cmake --build "${root}/pvfss/build" --parallel
