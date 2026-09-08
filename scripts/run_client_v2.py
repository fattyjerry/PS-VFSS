#!/usr/bin/env python3
import argparse
import csv
import json
import math
import os
import socket
import statistics
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESULT_ROOT = ROOT / "results" / "client_v2"
SCHEMES = ("psvfss", "fmd", "pps-gc", "omr")
METRICS = (
    "sender_signaling_generation_ns",
    "sender_serialization_ns",
    "sender_total_online_ns",
    "recipient_processing_ns",
)
HEADER = [
    "run_id", "experiment_id", "parameter_point_id", "trial_id", "trial_kind",
    "scheme", "phase", "protocol_role", "operation", "host_id", "host_class",
    "Ns", "Nr", "actual_k", "observed_result_count", "false_positive_count", "scheme_specific_parameters",
    "metric_name", "value", "unit", "status", "correctness", "output_type",
    "error_class", "timestamp_utc",
]


def parse_args():
    parser = argparse.ArgumentParser(description="Run resumable client-side v2 benchmark trials.")
    parser.add_argument("schemes", nargs="*", choices=SCHEMES, default=list(SCHEMES))
    parser.add_argument("--Ns", type=int, default=16)
    parser.add_argument("--Nr", type=int, default=2)
    parser.add_argument("--k", type=int, default=1)
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--trial-kind", choices=["calibration", "warmup", "measured"], default="measured")
    parser.add_argument("--run-id", default="client-v2-smoke")
    parser.add_argument("--experiment-id", default="client-side-smoke")
    parser.add_argument("--host-class", choices=["client", "server", "local-development"], default="local-development")
    parser.add_argument("--timeout-sec", type=int, default=300)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--fmd-gamma", type=int, default=8)
    parser.add_argument("--psvfss-T", type=int, default=4)
    parser.add_argument("--omr-threads", type=int, default=1)
    return parser.parse_args()


def command_for(scheme, args, trial):
    if scheme == "psvfss":
        return ([
            sys.executable, "run_psvfss_unified.py", "--binary", "build/src/test",
            "--ns", str(args.Ns), "--ell", str(args.k), "--T", str(args.psvfss_T),
            "--online-reps", "1", "--reps", "1", "--base-port", str(15400 + trial * 10),
            "--case-timeout-sec", str(args.timeout_sec),
        ], ROOT / "pvfss")
    if scheme == "fmd":
        return (["go", "run", "./cmd/clientbench", "--Ns", str(args.Ns), "--k", str(args.k), "--gamma", str(args.fmd_gamma)], ROOT / "baselines" / "fmd")
    if scheme == "pps-gc":
        return (["pps-garbled-circuits/target/release/examples/client-bench", "--Ns", str(args.Ns), "--Nr", str(args.Nr), "--k", str(args.k)], ROOT / "baselines" / "pps-gc")
    return (["build/OMRdemos", "--bench", "omrp1", "--threads", str(args.omr_threads), "--N", str(args.Ns), "--kbar", str(args.k)], ROOT / "baselines" / "omr")


def terminal_completed(path):
    if not path.is_file():
        return False
    with path.open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    return bool(rows) and all(row["status"] == "completed" for row in rows)


def atomic_write(path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", newline="", dir=path.parent, delete=False) as handle:
        writer = csv.DictWriter(handle, fieldnames=HEADER)
        writer.writeheader()
        writer.writerows(rows)
        handle.flush()
        os.fsync(handle.fileno())
        temp_name = handle.name
    os.replace(temp_name, path)


def base_row(args, scheme, trial, status, correctness, payload, error_class):
    point = f"Ns={args.Ns}_Nr={args.Nr}_k={args.k}"
    return {
        "run_id": args.run_id, "experiment_id": args.experiment_id,
        "parameter_point_id": point, "trial_id": trial, "trial_kind": args.trial_kind,
        "scheme": payload.get("scheme", scheme.upper()), "host_id": socket.gethostname(),
        "host_class": args.host_class, "Ns": args.Ns,
        "Nr": "" if payload.get("Nr") is None else payload.get("Nr"),
        "actual_k": payload.get("actual_k", args.k),
        "observed_result_count": payload.get("observed_result_count", ""),
        "false_positive_count": payload.get("false_positive_count", ""),
        "scheme_specific_parameters": payload.get("scheme_specific_parameters", ""),
        "status": status, "correctness": str(correctness).lower() if correctness is not None else "",
        "output_type": payload.get("output_type", ""), "error_class": error_class,
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
    }


def failure_rows(args, scheme, trial, status, error_class):
    base = base_row(args, scheme, trial, status, None, {}, error_class)
    base.update({"phase": "client", "protocol_role": "", "operation": "trial", "metric_name": "", "value": "", "unit": "ns"})
    return [base]


def payload_rows(args, scheme, trial, payload):
    correct = payload.get("correctness") is True
    status = "completed" if correct else "invalid"
    error_class = payload.get("error_class", "") if not correct else ""
    rows = []
    for metric in METRICS:
        value = payload.get(metric)
        if not isinstance(value, int) or value < 0:
            status = "invalid"
            correct = False
            error_class = error_class or f"missing_or_invalid_{metric}"
            value = ""
        if not correct:
            value = ""
        sender = metric.startswith("sender_")
        row = base_row(args, scheme, trial, status, correct, payload, error_class)
        row.update({
            "phase": "sending" if sender else "retrieval",
            "protocol_role": "sender" if sender else "recipient",
            "operation": metric.removesuffix("_ns"), "metric_name": metric,
            "value": value, "unit": "ns",
        })
        rows.append(row)
    return rows


def run_trial(args, scheme, trial, raw_path, log_path):
    command, cwd = command_for(scheme, args, trial)
    try:
        proc = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=args.timeout_sec, check=False)
        output = proc.stdout
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(output, encoding="utf-8")
        payload = None
        for line in output.splitlines():
            if line.startswith("[CLIENT_BENCH_JSON] "):
                payload = json.loads(line.removeprefix("[CLIENT_BENCH_JSON] "))
        if payload is None:
            rows = failure_rows(args, scheme, trial, "failed", f"runner_exit_{proc.returncode}_missing_payload")
        else:
            rows = payload_rows(args, scheme, trial, payload)
            if proc.returncode != 0 and all(row["status"] == "completed" for row in rows):
                rows = failure_rows(args, scheme, trial, "failed", f"runner_exit_{proc.returncode}")
    except subprocess.TimeoutExpired as exc:
        def decoded(value):
            if value is None:
                return ""
            return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value
        output = decoded(exc.stdout) + decoded(exc.stderr)
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(output, encoding="utf-8")
        rows = failure_rows(args, scheme, trial, "timeout", "trial_timeout")
    except Exception as exc:
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text(str(exc), encoding="utf-8")
        rows = failure_rows(args, scheme, trial, "failed", type(exc).__name__)
    atomic_write(raw_path, rows)
    return rows[0]["status"]


def write_summary(args):
    grouped = {}
    outcomes = {}
    for path in sorted((RESULT_ROOT / "raw").glob("**/*.csv")):
        with path.open(newline="") as handle:
            rows = list(csv.DictReader(handle))
            if not rows or rows[0]["run_id"] != args.run_id or rows[0]["trial_kind"] != "measured":
                continue
            first = rows[0]
            outcome_key = (first["scheme"], first["parameter_point_id"])
            outcomes.setdefault(outcome_key, {})[first["trial_id"]] = first["status"]
            for row in rows:
                if row["status"] != "completed" or not row["metric_name"]:
                    continue
                grouped.setdefault((row["scheme"], row["parameter_point_id"], row["metric_name"]), []).append(int(row["value"]))
    out = RESULT_ROOT / "summary" / f"{args.run_id}.csv"
    out.parent.mkdir(parents=True, exist_ok=True)
    fields = ["run_id", "scheme", "parameter_point_id", "metric_name", "attempted_trials", "valid_trials", "invalid_trials", "failed_trials", "timeout_trials", "mean_ms", "median_ms", "std_ms", "cv", "ci95_low_ms", "ci95_high_ms", "min_ms", "max_ms"]
    with out.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        summarized_points = set()
        for (scheme, point, metric), values in sorted(grouped.items()):
            summarized_points.add((scheme, point))
            n = len(values); mean = statistics.mean(values); median = statistics.median(values)
            std = statistics.stdev(values) if n > 1 else 0.0
            margin = 1.96 * std / math.sqrt(n) if n > 1 else 0.0
            statuses = list(outcomes.get((scheme, point), {}).values())
            writer.writerow({"run_id": args.run_id, "scheme": scheme, "parameter_point_id": point, "metric_name": metric,
                             "attempted_trials": len(statuses), "valid_trials": n,
                             "invalid_trials": statuses.count("invalid"), "failed_trials": statuses.count("failed"), "timeout_trials": statuses.count("timeout"),
                             "mean_ms": mean / 1e6, "median_ms": median / 1e6, "std_ms": std / 1e6,
                             "cv": std / mean if mean else "", "ci95_low_ms": (mean - margin) / 1e6,
                             "ci95_high_ms": (mean + margin) / 1e6, "min_ms": min(values) / 1e6, "max_ms": max(values) / 1e6})
        for (scheme, point), trial_statuses in sorted(outcomes.items()):
            if (scheme, point) in summarized_points:
                continue
            statuses = list(trial_statuses.values())
            writer.writerow({"run_id": args.run_id, "scheme": scheme, "parameter_point_id": point, "metric_name": "",
                             "attempted_trials": len(statuses), "valid_trials": 0,
                             "invalid_trials": statuses.count("invalid"), "failed_trials": statuses.count("failed"),
                             "timeout_trials": statuses.count("timeout")})


def main():
    args = parse_args()
    if args.trials <= 0 or args.timeout_sec <= 0:
        raise SystemExit("--trials and --timeout-sec must be positive")
    point = f"Ns={args.Ns}_Nr={args.Nr}_k={args.k}"
    for scheme in args.schemes:
        for trial in range(1, args.trials + 1):
            raw = RESULT_ROOT / "raw" / args.run_id / scheme / point / f"trial-{trial:05d}.csv"
            log = RESULT_ROOT / "logs" / args.run_id / scheme / point / f"trial-{trial:05d}.log"
            if not args.force and terminal_completed(raw):
                print(f"skip completed {scheme} {point} trial={trial}")
                continue
            status = run_trial(args, scheme, trial, raw, log)
            print(f"{scheme} {point} trial={trial} status={status}")
    write_summary(args)


if __name__ == "__main__":
    main()
