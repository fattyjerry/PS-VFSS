#!/usr/bin/env python3
import csv
import json
import math
import os
import statistics
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path("/home/zjr/PS-VFSS")
HERE = Path(__file__).resolve().parent
BINARY = ROOT / "experiments/server_online_comparison/results/fmd_gamma6_comm_repeated/fmdbench"
RAW = HERE / "raw_trials.jsonl"
SUMMARY = HERE / "summary.csv"
STATE = HERE / "runner_state.json"
LOGS = HERE / "logs"
N, K, GAMMA = 4096, 50, 6
WARMUPS, MEASURED = 5, 1000


def atomic_state(value):
    temporary = STATE.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, STATE)


def append_raw(row):
    with RAW.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(row, sort_keys=True) + "\n")
        handle.flush()
        os.fsync(handle.fileno())


def run_one(kind, index):
    completed = subprocess.run(
        [str(BINARY), "-N", str(N), "-ell", str(K),
         "-gamma", str(GAMMA), "-reps", "1"],
        cwd=ROOT / "baselines/fmd", text=True, capture_output=True, timeout=120)
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
        "scheme": "FMD", "N": N, "k_actual": K, "gamma": GAMMA,
        "trial_type": kind, "trial_index": index,
        "candidate_count": value["candidate_count"],
        "true_match_count": value["true_match_count"],
        "false_positive_count": value["false_positive_count"],
        "server_to_recipient_bytes": value["server_to_recipient_bytes"],
        "measurement_status": "ok",
        "response_encoding": "uint32_le_count_then_uint64_le_locations",
        "completed_utc": datetime.now(timezone.utc).isoformat(),
        "log": str(log.relative_to(HERE)),
    }


def summarize(rows):
    response = [r["server_to_recipient_bytes"] for r in rows]
    candidates = [r["candidate_count"] for r in rows]
    fps = [r["false_positive_count"] for r in rows]
    response_std = statistics.stdev(response)
    response_mean = statistics.mean(response)
    half_width = 1.96 * response_std / math.sqrt(len(response))
    expected_fp = (N - K) / (2 ** GAMMA)
    out = {
        "N": N, "k_actual": K, "gamma": GAMMA, "trial_count": len(rows),
        "response_median_bytes": statistics.median(response),
        "response_mean_bytes": response_mean,
        "response_std_bytes": response_std,
        "response_mean_ci95_low_bytes": response_mean - half_width,
        "response_mean_ci95_high_bytes": response_mean + half_width,
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
        writer = csv.DictWriter(handle, fieldnames=list(out))
        writer.writeheader()
        writer.writerow(out)
    return out


def main():
    HERE.mkdir(parents=True, exist_ok=True)
    LOGS.mkdir(parents=True, exist_ok=True)
    existing = []
    if RAW.exists():
        existing = [json.loads(line) for line in RAW.read_text(encoding="utf-8").splitlines() if line]
    completed = {(r["trial_type"], r["trial_index"]) for r in existing
                 if r.get("measurement_status") == "ok"}
    start = datetime.now(timezone.utc).isoformat()
    try:
        for kind, count in (("warmup", WARMUPS), ("measured", MEASURED)):
            for index in range(count):
                if (kind, index) in completed:
                    continue
                row = run_one(kind, index)
                append_raw(row)
                existing.append(row)
                completed.add((kind, index))
                measured_done = sum(r["trial_type"] == "measured" for r in existing)
                warmup_done = sum(r["trial_type"] == "warmup" for r in existing)
                atomic_state({"status": "running", "started_utc": start,
                              "updated_utc": row["completed_utc"],
                              "warmup_completed": warmup_done,
                              "warmup_target": WARMUPS,
                              "measured_completed": measured_done,
                              "measured_target": MEASURED})
        measured = [r for r in existing if r["trial_type"] == "measured"]
        out = summarize(measured)
        atomic_state({"status": "complete", "started_utc": start,
                      "completed_utc": datetime.now(timezone.utc).isoformat(),
                      "warmup_completed": WARMUPS, "warmup_target": WARMUPS,
                      "measured_completed": len(measured), "measured_target": MEASURED,
                      "summary": out})
    except Exception as error:
        atomic_state({"status": "error", "started_utc": start,
                      "updated_utc": datetime.now(timezone.utc).isoformat(),
                      "error": str(error)})
        raise


if __name__ == "__main__":
    main()
