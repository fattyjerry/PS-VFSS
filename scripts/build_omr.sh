#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "${root}/baselines/omr" -B "${root}/baselines/omr/build" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "${root}/baselines/omr/build" --parallel
