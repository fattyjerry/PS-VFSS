#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
RUN_DIR="$ROOT/results/sender_scalability_logspace_run"
PS="$ROOT/pvfss/build-sender-smoke/src/ps_vfss_sender_smoke"
FMD="$RUN_DIR/fmd_sender_scale"
PPS="$ROOT/baselines/pps-gc/pps-garbled-circuits/target/release/examples/sender-smoke"
OMR="$ROOT/baselines/omr/build-sender-smoke/omr_sender_smoke"
B_VALUES=(10 32 100 316 1000)
TRIALS=100
WARMUPS=20
TIMEOUT_SECONDS=1800

mkdir -p "$RUN_DIR/logs"
if [[ -e "$ROOT/results/sender_scalability_logspace_raw.csv" ||
      -e "$ROOT/results/sender_scalability_logspace_summary.csv" ]]; then
  echo "refusing to overwrite completed logspace results" >&2
  exit 2
fi

(cd "$ROOT/baselines/fmd" && go build -buildvcs=false -o "$FMD" ./cmd/sender-smoke)
printf 'scheme,B,status,exit_code\n' > "$RUN_DIR/terminal_status.csv"

run_scheme() {
  local scheme="$1"
  local binary="$2"
  local raw="$RUN_DIR/${scheme}_raw.csv"
  local b
  for b in "${B_VALUES[@]}"; do
    local log="$RUN_DIR/logs/${scheme}_B${b}.log"
    echo "[$(date -Is)] start scheme=$scheme B=$b"
    timeout --signal=TERM --kill-after=60s "$TIMEOUT_SECONDS" \
      taskset -c 0 "$binary" --mode scaling --signals "$b" \
      --trials "$TRIALS" --warmup "$WARMUPS" --output "$raw" \
      > "$log" 2>&1
    local rc=$?
    if [[ $rc -eq 0 ]]; then
      printf '%s,%s,ok,%s\n' "$scheme" "$b" "$rc" >> "$RUN_DIR/terminal_status.csv"
      echo "[$(date -Is)] done scheme=$scheme B=$b"
    else
      local status=error
      if [[ $rc -eq 124 || $rc -eq 137 ]]; then status=timeout; fi
      printf '%s,%s,%s,%s\n' "$scheme" "$b" "$status" "$rc" >> "$RUN_DIR/terminal_status.csv"
      echo "[$(date -Is)] stop scheme=$scheme B=$b status=$status exit=$rc" >&2
      return "$rc"
    fi
  done
}

overall=0
run_scheme psvfss "$PS" || overall=1
run_scheme ppsgc "$PPS" || overall=1
run_scheme fmd "$FMD" || overall=1
run_scheme omr "$OMR" || overall=1

python3 "$ROOT/results/summarize_sender_scalability_logspace.py"
exit "$overall"
