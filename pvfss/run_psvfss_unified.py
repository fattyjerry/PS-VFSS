#!/usr/bin/env python3
import argparse
import csv
import subprocess
import sys
import time
from pathlib import Path


DEFAULT_NS = [256, 512, 1024, 2048, 4096, 8192, 16384]
CSV_PREFIX = "[BENCH_CSV] "


def parse_args():
    parser = argparse.ArgumentParser(description="Run PSVFSS two-party unified benchmark.")
    parser.add_argument("--binary", default="build-local/src/test")
    parser.add_argument("--ell", type=int, default=50)
    parser.add_argument("--T", type=int, default=512)
    parser.add_argument("--reps", type=int, default=1)
    parser.add_argument("--online-reps", type=int, default=5)
    parser.add_argument("--base-port", type=int, default=12400)
    parser.add_argument("--case-timeout-sec", type=int, default=3600)
    parser.add_argument("--ns", default=",".join(str(n) for n in DEFAULT_NS))
    return parser.parse_args()


def parse_csv_row(output):
    rows = []
    for line in output.splitlines():
        if line.startswith(CSV_PREFIX) and not line.startswith(CSV_PREFIX + "scheme,"):
            rows.append(next(csv.reader([line[len(CSV_PREFIX):]])))
    if not rows:
        raise RuntimeError("missing BENCH_CSV row")
    row = rows[-1]
    if len(row) != 10:
        raise RuntimeError(f"unexpected BENCH_CSV column count: {len(row)} row={row}")
    return {
        "scheme": row[0],
        "N": int(row[1]),
        "ell": int(row[2]),
        "setup_ms": int(row[3]),
        "send_ms": int(row[4]),
        "server_ms": int(row[5]),
        "recipient_ms": int(row[6]),
        "comm_bytes": int(row[7]),
        "status": row[8],
        "bottleneck": row[9],
    }


def stop_process(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


def run_one(binary, n, t, ell, online_reps, port, timeout_sec):
    cmd1 = [binary, "1", str(port), str(n), str(t), str(ell), str(online_reps)]
    cmd2 = [binary, "2", str(port), str(n), str(t), str(ell), str(online_reps)]
    p1 = subprocess.Popen(cmd1, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(1)
    p2 = subprocess.Popen(cmd2, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    deadline = time.monotonic() + timeout_sec
    while p1.poll() is None and p2.poll() is None:
        if time.monotonic() >= deadline:
            stop_process(p1)
            stop_process(p2)
            raise RuntimeError(f"benchmark timed out after {timeout_sec}s")
        time.sleep(0.1)

    if p1.poll() is not None and p2.poll() is None:
        try:
            p2.wait(timeout=5)
        except subprocess.TimeoutExpired:
            stop_process(p2)
    elif p2.poll() is not None and p1.poll() is None:
        try:
            p1.wait(timeout=5)
        except subprocess.TimeoutExpired:
            stop_process(p1)

    out1, _ = p1.communicate()
    out2, _ = p2.communicate()
    if p1.returncode != 0 or p2.returncode != 0:
        raise RuntimeError(
            f"party failure rc1={p1.returncode} rc2={p2.returncode}\n"
            f"--- party1 ---\n{out1}\n--- party2 ---\n{out2}"
        )
    row = parse_csv_row(out1)
    if row["status"] != "completed":
        raise RuntimeError(f"benchmark did not complete correctly\n{out1}\n{out2}")
    if row["comm_bytes"] != 2 * ell * 8:
        raise RuntimeError(f"unexpected comm_bytes={row['comm_bytes']} expected={2 * ell * 8}")
    return row


def slowest(row):
    values = {
        "setup": row["setup_ms"],
        "send": row["send_ms"],
        "server": row["server_ms"],
        "recipient": row["recipient_ms"],
    }
    return max(values, key=values.get)


def average_rows(rows, reps, online_reps, t_eff, requested_t):
    avg = dict(rows[-1])
    for key in ["setup_ms", "send_ms", "server_ms", "recipient_ms", "comm_bytes"]:
        avg[key] = round(sum(row[key] for row in rows) / len(rows))
    avg["status"] = "completed"
    suffix = f"setup_reps=1_online_avg_reps={online_reps}"
    if reps != 1:
        suffix += f"_process_reps={reps}"
    if t_eff != requested_t:
        suffix += f"_T_eff={t_eff}"
    avg["bottleneck"] = suffix
    return avg


def print_row(row):
    print(
        CSV_PREFIX
        + f"{row['scheme']},{row['N']},{row['ell']},{row['setup_ms']},"
        + f"{row['send_ms']},{row['server_ms']},{row['recipient_ms']},"
        + f"{row['comm_bytes']},{row['status']},{row['bottleneck']}",
        flush=True,
    )


def main():
    args = parse_args()
    binary = str(Path(args.binary))
    ns = [int(x) for x in args.ns.split(",") if x.strip()]
    if args.reps <= 0:
        raise SystemExit("--reps must be positive")

    print(CSV_PREFIX + "scheme,N,ell,setup_ms,send_ms,server_ms,recipient_ms,comm_bytes,status,bottleneck")
    for idx, n in enumerate(ns):
        t_eff = min(args.T, n)
        if n % t_eff != 0:
            raise SystemExit(f"N={n} is not divisible by T_eff={t_eff}")
        rows = []
        for rep in range(args.reps):
            port = args.base_port + idx * 100 + rep * 2
            print(f"[BENCH_STAGE] PSVFSS start N={n} T={t_eff} ell={args.ell} setup_reps=1 online_reps={args.online_reps} process_rep={rep + 1}/{args.reps}", file=sys.stderr, flush=True)
            rows.append(run_one(
                binary, n, t_eff, args.ell, args.online_reps, port,
                args.case_timeout_sec,
            ))
            print(f"[BENCH_STAGE] PSVFSS done N={n} T={t_eff} ell={args.ell} setup_reps=1 online_reps={args.online_reps} process_rep={rep + 1}/{args.reps}", file=sys.stderr, flush=True)
        print_row(average_rows(rows, args.reps, args.online_reps, t_eff, args.T))


if __name__ == "__main__":
    main()
