#!/usr/bin/env bash
set -u
OUT=results/sender_signaling_final
PS=pvfss/build-sender-smoke/src/ps_vfss_sender_smoke
FMD=/tmp/fmd_sender_scale
PPS=baselines/pps-gc/pps-garbled-circuits/target/release/examples/sender-smoke
OMR=baselines/omr/build-sender-smoke/omr_sender_smoke
run(){ name=$1; bin=$2; args=$3; file=$OUT/${name}_multiple_signal_raw.csv; for B in 10 50 100 500 1000; do taskset -c 0 $bin --mode scaling --signals $B --trials 100 --warmup 20 --output "$file" $args || exit 1; done; }
run psvfss "$PS" ""
run fmd "$FMD" ""
run ppsgc "$PPS" ""
run omr "$OMR" ""
