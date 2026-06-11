#!/usr/bin/env python3
import argparse
import csv
import selectors
import subprocess
import sys
import time
from pathlib import Path


DEFAULT_NS = [256, 512, 1024, 2048, 4096, 8192, 16384]
CSV_PREFIX = "[BENCH_CSV] "


def parse_args():
    parser = argparse.ArgumentParser(description="Run PPS-GC unified benchmark with per-N hard limit.")
    parser.add_argument("--binary", default="pps-garbled-circuits/target/release/examples/unified-bench")
    parser.add_argument("--ell", type=int, default=50)
    parser.add_argument("--ns", default=",".join(str(n) for n in DEFAULT_NS))
    parser.add_argument("--per-n-limit-sec", type=int, default=2 * 60 * 60)
    return parser.parse_args()


def parse_completed_row(line):
    row = next(csv.reader([line[len(CSV_PREFIX):]]))
    if len(row) != 10 or row[0] == "scheme":
        return None
    return row


def timeout_row(n, ell, comm_bytes, stage, started):
    runtime_ms = int((time.monotonic() - started) * 1000)
    return (
        f"{CSV_PREFIX}PPS-GC,{n},{ell},,,,,{comm_bytes},timeout,"
        f"{stage}_runtime_so_far_ms={runtime_ms}"
    )


def run_one(binary, n, ell, limit_sec):
    started = time.monotonic()
    stage = "launch"
    comm_bytes = 2 * ell * 32
    proc = subprocess.Popen(
        [binary, "--N", str(n), "--ell", str(ell)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )
    sel = selectors.DefaultSelector()
    sel.register(proc.stdout, selectors.EVENT_READ, "stdout")
    sel.register(proc.stderr, selectors.EVENT_READ, "stderr")
    completed_row = None

    while proc.poll() is None:
        if time.monotonic() - started > limit_sec:
            proc.kill()
            proc.wait()
            print(timeout_row(n, ell, comm_bytes, stage, started), flush=True)
            return "timeout"

        for key, _ in sel.select(timeout=1.0):
            line = key.fileobj.readline()
            if not line:
                continue
            line = line.rstrip("\n")
            is_header = line.startswith(CSV_PREFIX + "scheme,")
            if not is_header:
                print(line, file=sys.stderr if key.data == "stderr" else sys.stdout, flush=True)
            if line.startswith("[BENCH_STAGE] PPS-GC "):
                stage = line.removeprefix("[BENCH_STAGE] PPS-GC ").replace(" ", "_")
            if line.startswith(CSV_PREFIX) and not line.startswith(CSV_PREFIX + "scheme,"):
                completed_row = parse_completed_row(line)

    for key in list(sel.get_map().values()):
        for line in key.fileobj:
            line = line.rstrip("\n")
            is_header = line.startswith(CSV_PREFIX + "scheme,")
            if not is_header:
                print(line, file=sys.stderr if key.data == "stderr" else sys.stdout, flush=True)
            if line.startswith(CSV_PREFIX) and not line.startswith(CSV_PREFIX + "scheme,"):
                completed_row = parse_completed_row(line)

    if proc.returncode == 0 and completed_row is not None:
        return completed_row[8]
    if proc.returncode == -9:
        print(f"{CSV_PREFIX}PPS-GC,{n},{ell},,,,,{comm_bytes},oom,{stage}", flush=True)
        return "oom"
    print(f"{CSV_PREFIX}PPS-GC,{n},{ell},,,,,{comm_bytes},crashed,{stage}_returncode={proc.returncode}", flush=True)
    return "crashed"


def main():
    args = parse_args()
    ns = [int(x) for x in args.ns.split(",") if x.strip()]
    binary = str(Path(args.binary))
    print(CSV_PREFIX + "scheme,N,ell,setup_ms,send_ms,server_ms,recipient_ms,comm_bytes,status,bottleneck")
    for n in ns:
        run_one(binary, n, args.ell, args.per_n_limit_sec)


if __name__ == "__main__":
    main()
