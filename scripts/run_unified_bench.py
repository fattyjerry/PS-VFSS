#!/usr/bin/env python3
import argparse
import csv
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = [
    "scheme", "N", "ell", "setup_ms", "send_ms", "server_ms",
    "recipient_ms", "comm_bytes", "status", "bottleneck",
]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run the PVFSS, FMD, OMR, and PPS-GC paper benchmarks."
    )
    parser.add_argument(
        "schemes", nargs="*", choices=["pvfss", "fmd", "omr", "pps-gc"],
        default=["pvfss", "fmd", "omr", "pps-gc"],
    )
    parser.add_argument("--ell", type=int, default=50)
    parser.add_argument(
        "--ns", default="256,512,1024,2048,4096,8192,16384"
    )
    parser.add_argument("--fmd-reps", type=int, default=5)
    parser.add_argument("--omr-threads", type=int, default=4)
    parser.add_argument("--omr-timeout-sec", type=int, default=300)
    parser.add_argument("--pps-timeout-sec", type=int, default=300)
    parser.add_argument("--output", default=str(ROOT / "results" / "latest.csv"))
    return parser.parse_args()


def collect(command, cwd):
    proc = subprocess.run(
        command, cwd=cwd, text=True, stdout=subprocess.PIPE,
        stderr=sys.stderr, check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"benchmark failed with exit code {proc.returncode}: {' '.join(command)}"
        )
    rows = []
    for line in proc.stdout.splitlines():
        if not line.startswith("[BENCH_CSV] "):
            continue
        payload = line.removeprefix("[BENCH_CSV] ")
        row = next(csv.reader([payload]))
        if row and row[0] != "scheme":
            if len(row) != len(HEADER):
                raise RuntimeError(f"unexpected benchmark row: {payload}")
            rows.append(row)
    return rows


def main():
    args = parse_args()
    ns = args.ns
    rows = []
    if "pvfss" in args.schemes:
        rows.extend(collect(
            [
                sys.executable, "run_psvfss_unified.py",
                "--binary", "build/src/test", "--ell", str(args.ell), "--ns", ns,
            ],
            ROOT / "pvfss",
        ))
    if "fmd" in args.schemes:
        fmd_ns = [value for value in ns.split(",") if value.strip()]
        for n in fmd_ns:
            rows.extend(collect(
                [
                    "go", "run", "./cmd/fmdbench",
                    "--N", n, "--ell", str(args.ell),
                    "--gamma", "24", "--reps", str(args.fmd_reps),
                ],
                ROOT / "baselines" / "fmd",
            ))
    if "omr" in args.schemes:
        rows.extend(collect(
            [
                sys.executable, "run_omr_unified.py",
                "--ell", str(args.ell), "--ns", ns,
                "--threads", str(args.omr_threads),
                "--per-n-limit-sec", str(args.omr_timeout_sec),
            ],
            ROOT / "baselines" / "omr",
        ))
    if "pps-gc" in args.schemes:
        rows.extend(collect(
            [
                sys.executable, "run_ppsgc_unified.py",
                "--ell", str(args.ell), "--ns", ns,
                "--per-n-limit-sec", str(args.pps_timeout_sec),
            ],
            ROOT / "baselines" / "pps-gc",
        ))

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(HEADER)
        writer.writerows(rows)
    print(f"wrote {len(rows)} rows to {output}")


if __name__ == "__main__":
    main()
