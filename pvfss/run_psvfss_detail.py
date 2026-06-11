#!/usr/bin/env python3
import argparse
import csv
import subprocess
import sys
import tempfile
import time
from pathlib import Path


DEFAULT_NS = [256, 512, 1024, 2048, 4096, 8192, 16384]
DETAIL_PREFIX = "[BENCH_DETAIL_CSV] "


def parse_args():
    parser = argparse.ArgumentParser(description="Run PSVFSS internal cost breakdown benchmark.")
    parser.add_argument("--binary", default="build-local/src/test")
    parser.add_argument("--ell", type=int, default=50)
    parser.add_argument("--T", type=int, default=512)
    parser.add_argument("--online-reps", type=int, default=5)
    parser.add_argument("--base-port", type=int, default=12500)
    parser.add_argument("--case-timeout-sec", type=int, default=3600)
    parser.add_argument("--ns", default=",".join(str(n) for n in DEFAULT_NS))
    return parser.parse_args()


def parse_detail_row(output):
    rows = []
    for line in output.splitlines():
        if line.startswith(DETAIL_PREFIX):
            rows.append(next(csv.reader([line[len(DETAIL_PREFIX):]])))
    if not rows:
        raise RuntimeError("missing BENCH_DETAIL_CSV row")
    row = rows[-1]
    if len(row) != 11:
        raise RuntimeError(f"unexpected BENCH_DETAIL_CSV column count: {len(row)} row={row}")
    return {
        "N": int(row[0]),
        "ell": int(row[1]),
        "offline_setup_ms": row[2],
        "keygen_ms": row[3],
        "eval_ms": row[4],
        "shuffle_ms": row[5],
        "compress_ms": row[6],
        "reconstruct_ms": row[7],
        "comm_bytes": int(row[8]),
        "status": row[9],
        "bottleneck": row[10],
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
    with tempfile.NamedTemporaryFile("w+", delete=False) as f1, tempfile.NamedTemporaryFile("w+", delete=False) as f2:
        p1 = subprocess.Popen(cmd1, stdout=f1, stderr=subprocess.STDOUT, text=True)
        time.sleep(1)
        p2 = subprocess.Popen(cmd2, stdout=f2, stderr=subprocess.STDOUT, text=True)
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
        rc1 = p1.wait()
        rc2 = p2.wait()
        f1.flush()
        f2.flush()
        out1 = Path(f1.name).read_text()
        out2 = Path(f2.name).read_text()
    if rc1 != 0 or rc2 != 0:
        raise RuntimeError(
            f"party failure rc1={rc1} rc2={rc2}\n"
            f"--- party1 ---\n{out1}\n--- party2 ---\n{out2}"
        )
    row = parse_detail_row(out1)
    expected_comm = 2 * ell * 8
    if row["status"] != "completed":
        raise RuntimeError(f"detail benchmark did not complete correctly\n{out1}\n{out2}")
    if row["comm_bytes"] != expected_comm:
        raise RuntimeError(f"unexpected comm_bytes={row['comm_bytes']} expected={expected_comm}")
    return row


def print_row(row):
    print(
        DETAIL_PREFIX
        + f"{row['N']},{row['ell']},{row['offline_setup_ms']},"
        + f"{row['keygen_ms']},{row['eval_ms']},{row['shuffle_ms']},"
        + f"{row['compress_ms']},{row['reconstruct_ms']},"
        + f"{row['comm_bytes']},{row['status']},{row['bottleneck']}",
        flush=True,
    )


def main():
    args = parse_args()
    binary = str(Path(args.binary))
    ns = [int(x) for x in args.ns.split(",") if x.strip()]
    print(DETAIL_PREFIX + "N,ell,offline_setup_ms,keygen_ms,eval_ms,shuffle_ms,compress_ms,reconstruct_ms,comm_bytes,status,bottleneck")
    for idx, n in enumerate(ns):
        t_eff = min(args.T, n)
        if n % t_eff != 0:
            raise SystemExit(f"N={n} is not divisible by T_eff={t_eff}")
        port = args.base_port + idx * 100
        print(
            f"[BENCH_STAGE] PSVFSS_DETAIL start N={n} T_eff={t_eff} ell={args.ell} "
            f"setup_reps=1 online_reps={args.online_reps}",
            file=sys.stderr,
            flush=True,
        )
        row = run_one(
            binary, n, t_eff, args.ell, args.online_reps, port,
            args.case_timeout_sec,
        )
        print_row(row)
        print(
            f"[BENCH_STAGE] PSVFSS_DETAIL done N={n} T_eff={t_eff} ell={args.ell}",
            file=sys.stderr,
            flush=True,
        )


if __name__ == "__main__":
    main()
