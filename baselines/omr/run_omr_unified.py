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
    parser = argparse.ArgumentParser(description="Run OMR unified benchmark with per-N hard limit.")
    parser.add_argument("--binary", default="build/OMRdemos")
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--ell", type=int, default=50)
    parser.add_argument("--ns", default=",".join(str(n) for n in DEFAULT_NS))
    parser.add_argument("--per-n-limit-sec", type=int, default=int(2.5 * 60 * 60))
    return parser.parse_args()


def parse_completed_row(line):
    row = next(csv.reader([line[len(CSV_PREFIX):]]))
    if len(row) != 10 or row[0] == "scheme":
        return None
    return row


def csv_status(n, ell, status, bottleneck):
    print(f"{CSV_PREFIX}OMR,{n},{ell},,,,,,{status},{bottleneck}", flush=True)


def run_one(binary, n, ell, threads, limit_sec):
    started = time.monotonic()
    stage = "launch"
    root = Path(__file__).resolve().parent
    binary_path = Path(binary)
    if not binary_path.is_absolute():
        binary_path = root / binary_path
    child_binary = str(binary_path)
    child_cwd = str(root)
    proc = subprocess.Popen(
        [child_binary, "--bench", "omrp1", "--threads", str(threads), "--N", str(n), "--kbar", str(ell)],
        cwd=child_cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )
    sel = selectors.DefaultSelector()
    sel.register(proc.stdout, selectors.EVENT_READ, "stdout")
    sel.register(proc.stderr, selectors.EVENT_READ, "stderr")
    completed_row = None
    captured = []

    while proc.poll() is None:
        runtime_ms = int((time.monotonic() - started) * 1000)
        if runtime_ms > limit_sec * 1000:
            proc.kill()
            proc.wait()
            csv_status(n, ell, "timeout", f"{stage}_runtime_so_far_ms={runtime_ms}")
            return "timeout"
        for key, _ in sel.select(timeout=1.0):
            line = key.fileobj.readline()
            if not line:
                continue
            line = line.rstrip("\n")
            captured.append(line)
            print(line, file=sys.stderr if key.data == "stderr" else sys.stdout, flush=True)
            if line.startswith("[BENCH_STAGE] OMR "):
                stage = line.removeprefix("[BENCH_STAGE] OMR ").replace(" ", "_")
            if line.startswith(CSV_PREFIX) and not line.startswith(CSV_PREFIX + "scheme,"):
                completed_row = parse_completed_row(line)

    for key in list(sel.get_map().values()):
        for line in key.fileobj:
            line = line.rstrip("\n")
            captured.append(line)
            print(line, file=sys.stderr if key.data == "stderr" else sys.stdout, flush=True)
            if line.startswith(CSV_PREFIX) and not line.startswith(CSV_PREFIX + "scheme,"):
                completed_row = parse_completed_row(line)

    if proc.returncode == 0 and completed_row is not None:
        return completed_row[8]
    if proc.returncode == -9:
        csv_status(n, ell, "oom", stage)
        return "oom"

    joined = "\n".join(captured)
    bottleneck = stage
    if "result ciphertext is transparent" in joined:
        bottleneck = "seal_transparent_ciphertext"
    csv_status(n, ell, "crashed", f"{bottleneck}_returncode={proc.returncode}")
    return "crashed"


def main():
    args = parse_args()
    ns = [int(x) for x in args.ns.split(",") if x.strip()]
    binary = str(Path(args.binary))
    print(CSV_PREFIX + "scheme,N,ell,setup_ms,send_ms,server_ms,recipient_ms,comm_bytes,status,bottleneck")
    for n in ns:
        run_one(binary, n, args.ell, args.threads, args.per_n_limit_sec)


if __name__ == "__main__":
    main()
