#!/usr/bin/env python3
import csv
import json
import os
import statistics
import subprocess
from pathlib import Path

ROOT = Path("/home/zjr/PS-VFSS")
HERE = Path(__file__).resolve().parent
BINARY = HERE / "fmdbench"
RAW = HERE / "raw_trials.jsonl"
SUMMARY = HERE / "summary.csv"
LOGS = HERE / "logs"
N = 4096
K = 50
GAMMA = 6
WARMUPS = 5
MEASURED = 30


def append_raw(row):
    with RAW.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(row, sort_keys=True) + "\n")
        handle.flush()
        os.fsync(handle.fileno())


def run_one(kind, index):
    command = [str(BINARY), "-N", str(N), "-ell", str(K),
               "-gamma", str(GAMMA), "-reps", "1"]
    completed = subprocess.run(command, cwd=ROOT / "baselines/fmd",
                               text=True, capture_output=True, timeout=120)
    log = LOGS / f"{kind}_{index}.log"
    log.write_text(completed.stdout + completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        raise RuntimeError(f"{kind} {index}: exit={completed.returncode}")
    tagged = [line for line in completed.stdout.splitlines()
              if line.startswith("[PILOT_JSON]")]
    if len(tagged) != 1:
        raise RuntimeError(f"{kind} {index}: missing/duplicate PILOT_JSON")
    value = json.loads(tagged[0].split("]", 1)[1].strip())
    if value["measurement_status"] != "completed":
        raise RuntimeError(f"{kind} {index}: {value['measurement_status']}")
    return {
        "scheme": "FMD",
        "N": N,
        "k_actual": K,
        "gamma": GAMMA,
        "trial_type": kind,
        "trial_index": index,
        "candidate_count": value["candidate_count"],
        "true_match_count": value["true_match_count"],
        "false_positive_count": value["false_positive_count"],
        "server_to_recipient_bytes": value["server_to_recipient_bytes"],
        "measurement_status": "ok",
        "response_encoding": "uint32_le_count_then_uint64_le_locations",
        "log": str(log.relative_to(HERE)),
    }


def main():
    HERE.mkdir(parents=True, exist_ok=True)
    LOGS.mkdir(parents=True, exist_ok=True)
    if RAW.exists() or SUMMARY.exists():
        raise RuntimeError("refusing to overwrite existing repeated gamma=6 results")
    rows = []
    for index in range(WARMUPS):
        row = run_one("warmup", index)
        append_raw(row)
    for index in range(MEASURED):
        row = run_one("measured", index)
        append_raw(row)
        rows.append(row)

    fields = ["N", "k_actual", "gamma", "trial_count",
              "response_median_bytes", "response_mean_bytes",
              "response_min_bytes", "response_max_bytes",
              "candidate_median", "candidate_mean", "candidate_min", "candidate_max",
              "false_positive_median", "false_positive_mean",
              "false_positive_min", "false_positive_max",
              "theoretical_expected_false_positives",
              "theoretical_expected_response_bytes"]
    response = [r["server_to_recipient_bytes"] for r in rows]
    candidates = [r["candidate_count"] for r in rows]
    fps = [r["false_positive_count"] for r in rows]
    expected_fp = (N - K) / (2 ** GAMMA)
    out = {
        "N": N, "k_actual": K, "gamma": GAMMA, "trial_count": len(rows),
        "response_median_bytes": statistics.median(response),
        "response_mean_bytes": statistics.mean(response),
        "response_min_bytes": min(response), "response_max_bytes": max(response),
        "candidate_median": statistics.median(candidates),
        "candidate_mean": statistics.mean(candidates),
        "candidate_min": min(candidates), "candidate_max": max(candidates),
        "false_positive_median": statistics.median(fps),
        "false_positive_mean": statistics.mean(fps),
        "false_positive_min": min(fps), "false_positive_max": max(fps),
        "theoretical_expected_false_positives": expected_fp,
        "theoretical_expected_response_bytes": 4 + 8 * (K + expected_fp),
    }
    with SUMMARY.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerow(out)
    print(json.dumps(out, sort_keys=True))


if __name__ == "__main__":
    main()
