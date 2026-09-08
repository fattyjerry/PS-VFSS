#!/usr/bin/env python3
import csv
import hashlib
import json
import os
import re
import statistics
import subprocess
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
BINARY = ROOT / "baselines/fmd/fmdbench"
RAW = HERE / "raw_trials.jsonl"
LOGS = HERE / "logs"
NS = (256, 512, 1024, 2048, 4096)
KS = (4, 10, 20, 30, 40, 50)
GAMMA = 12


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def load_rows():
    if not RAW.exists():
        return []
    rows = []
    for line in RAW.read_text().splitlines():
        if line.strip():
            rows.append(json.loads(line))
    return rows


def append_row(row):
    with RAW.open("a") as f:
        f.write(json.dumps(row, separators=(",", ":")) + "\n")
        f.flush()
        os.fsync(f.fileno())


def key(n, k, trial_type, trial_index, binary_hash):
    return (n, k, trial_type, trial_index, binary_hash)


def run_one(n, k, trial_type, trial_index, binary_hash):
    log = LOGS / f"fmd_gamma12_N{n}_k{k}_{trial_type}{trial_index}.log"
    cmd = [str(BINARY), "-N", str(n), "-ell", str(k), "-gamma", str(GAMMA), "-reps", "1"]
    with log.open("w") as out:
        proc = subprocess.run(cmd, cwd=ROOT / "baselines/fmd", stdout=out,
                              stderr=subprocess.STDOUT, timeout=300)
        out.flush()
        os.fsync(out.fileno())
    text = log.read_text(errors="replace")
    matches = re.findall(r"\[PILOT_JSON\]\s+(\{.*\})", text)
    result = json.loads(matches[-1]) if matches else None
    row = {
        "result_tier": "pilot_fmd_gamma12",
        "scheme": "FMD",
        "N": n,
        "k_actual": k,
        "k_cap": k,
        "fmd_gamma": GAMMA,
        "fmd_false_positive_rate": 2 ** (-GAMMA),
        "trial_type": trial_type,
        "trial_index": trial_index,
        "is_warmup": trial_type == "warmup",
        "binary_sha256": binary_hash,
        "randomness_source": "crypto/rand_unseeded",
        "measurement_quality": "end_to_end_pilot",
        "completed_utc": datetime.now(timezone.utc).isoformat(),
        "exit_code": proc.returncode,
        "log": str(log.relative_to(HERE)),
        "measurement_status": "ok" if proc.returncode == 0 and result else "error",
    }
    if result:
        row.update(result)
        row["measurement_status"] = "ok" if result.get("measurement_status") == "completed" else "error"
    append_row(row)


def stats(rows, field):
    values = [r[field] for r in rows if isinstance(r.get(field), (int, float))]
    if not values:
        return ("", "", "")
    return (statistics.median(values), min(values), max(values))


def write_summaries(rows):
    measured = [r for r in rows if not r["is_warmup"] and r["measurement_status"] == "ok"]
    with (HERE / "server_time_vs_N_gamma12.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["scheme", "fmd_gamma", "N", "k_actual", "trial_count",
                    "retrieval_median_ms", "retrieval_min_ms", "retrieval_max_ms",
                    "response_median_bytes", "response_min_bytes", "response_max_bytes",
                    "candidate_median", "false_positive_median", "measurement_quality"])
        for n in NS:
            group = [r for r in measured if r["N"] == n and r["k_actual"] == 50]
            w.writerow(["FMD", GAMMA, n, 50, len(group), *stats(group, "retrieval_online_ms"),
                        *stats(group, "server_to_recipient_bytes"),
                        stats(group, "candidate_count")[0], stats(group, "false_positive_count")[0],
                        "end_to_end_pilot"])
    with (HERE / "response_bytes_vs_k_gamma12.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["scheme", "fmd_gamma", "N", "k_actual", "trial_count",
                    "response_median_bytes", "response_min_bytes", "response_max_bytes",
                    "candidate_median", "candidate_min", "candidate_max",
                    "false_positive_median", "false_positive_min", "false_positive_max",
                    "measurement_quality"])
        for k in KS:
            group = [r for r in measured if r["N"] == 4096 and r["k_actual"] == k]
            w.writerow(["FMD", GAMMA, 4096, k, len(group),
                        *stats(group, "server_to_recipient_bytes"), *stats(group, "candidate_count"),
                        *stats(group, "false_positive_count"), "end_to_end_pilot"])
    (HERE / "RUN_NOTES.md").write_text(
        "# FMD gamma=12 pilot\n\n"
        "One warmup and three measured process-level trials per unique (N,k) point. "
        "The N=4096,k=50 trials are reused by both summaries. Each response byte count "
        "comes from the constructed response buffer. The artifact currently uses unseeded "
        "crypto/rand; raw trials and min/median/max are retained. No plots were generated.\n"
    )


def main():
    LOGS.mkdir(parents=True, exist_ok=True)
    binary_hash = sha256(BINARY)
    points = [(n, 50) for n in NS] + [(4096, k) for k in KS if k != 50]
    rows = load_rows()
    completed = {key(r["N"], r["k_actual"], r["trial_type"], r["trial_index"], r["binary_sha256"])
                 for r in rows if r.get("measurement_status") == "ok"}
    for n, k in points:
        trials = [("warmup", 0)] + [("measured", i) for i in range(3)]
        for trial_type, trial_index in trials:
            if key(n, k, trial_type, trial_index, binary_hash) not in completed:
                run_one(n, k, trial_type, trial_index, binary_hash)
    write_summaries(load_rows())


if __name__ == "__main__":
    main()
