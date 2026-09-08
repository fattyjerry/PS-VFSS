#!/usr/bin/env python3
import csv
import hashlib
import json
import os
import signal
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
LOGS = HERE / "logs"
RAW = HERE / "raw_trials.jsonl"
SUMMARY = HERE / "psvfss_breakdown_nr10000.csv"
BINARY = ROOT / "pvfss/build-paillier3072/src/test"
SOURCE_FILES = [ROOT / "pvfss/src/test.cc", ROOT / "pvfss/src/dpf.cc", ROOT / "pvfss/src/dpf.h"]
NS_VALUES = [256, 512, 1024, 2048, 4096, 8192, 16384]
K = 50
NR = 10000
MEASURED_REPS = 3
TRIAL_TIMEOUT_SECONDS = 900


def digest(paths):
    h = hashlib.sha256()
    for path in paths:
        h.update(path.read_bytes())
    return h.hexdigest()


def tagged_json(text, tag):
    rows = [json.loads(line[len(tag):].strip()) for line in text.splitlines()
            if line.startswith(tag)]
    if not rows:
        raise RuntimeError(f"missing {tag}")
    return rows[-1]


def append_raw(row):
    with RAW.open("a", encoding="utf-8") as f:
        f.write(json.dumps(row, sort_keys=True) + "\n")
        f.flush()
        os.fsync(f.fileno())


def terminate(processes):
    for process in processes:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline and any(p.poll() is None for p in processes):
        time.sleep(0.1)
    for process in processes:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)


def run_pair(n, trial_type, trial_index, port, binary_hash, source_hash):
    common = [str(n), str(n), str(K), "1",
              "--preprocessing-mode=local-trusted",
              "--measurement-mode=component-pilot", str(NR)]
    commands = [
        [str(BINARY), "1", str(port)] + common,
        [str(BINARY), "2", str(port)] + common,
    ]
    names = [f"N{n}_{trial_type}{trial_index}_alice.log",
             f"N{n}_{trial_type}{trial_index}_bob.log"]
    handles = [(LOGS / name).open("w", encoding="utf-8") for name in names]
    processes = []
    try:
        for party in range(2):
            processes.append(subprocess.Popen(
                commands[party], cwd=ROOT / "pvfss", stdout=handles[party],
                stderr=subprocess.STDOUT, start_new_session=True))
            if party == 0:
                time.sleep(0.2)
        deadline = time.monotonic() + TRIAL_TIMEOUT_SECONDS
        while any(p.poll() is None for p in processes):
            if time.monotonic() >= deadline:
                terminate(processes)
                raise RuntimeError(f"timeout N={n} {trial_type}={trial_index}")
            time.sleep(0.2)
    finally:
        for handle in handles:
            handle.flush()
            os.fsync(handle.fileno())
            handle.close()

    codes = [p.wait() for p in processes]
    if codes != [0, 0]:
        raise RuntimeError(f"party failure N={n} {trial_type}={trial_index}: {codes}")
    outputs = [(LOGS / name).read_text(encoding="utf-8") for name in names]
    pilots = [tagged_json(output, "[PILOT_JSON]") for output in outputs]
    client = tagged_json(outputs[0], "[CLIENT_BENCH_JSON]")
    critical_party = max(range(2), key=lambda i: pilots[i]["retrieval_online_ms"])
    critical = pilots[critical_party]
    vereval = max(p["admission_batch_online_ms"] for p in pilots)
    eval_ms = critical["retrieval_eval_ms"]
    cprs_grouped = (critical["retrieval_shuffle_ms"] +
                    critical["retrieval_compression_ms"] +
                    critical["response_serialization_ms"])
    server_online = vereval + critical["retrieval_online_ms"]
    return {
        "schema": "psvfss-breakdown-nr10000-v1",
        "Ns": n,
        "Nr": NR,
        "k": K,
        "trial_type": trial_type,
        "trial_index": trial_index,
        "binary_sha256": binary_hash,
        "source_sha256": source_hash,
        "VerGen_ms": float(client["sender_signaling_generation_ns"]) / 1_000_000.0,
        "VerEval_ms": vereval,
        "Eval_ms": eval_ms,
        "Cprs_ms": cprs_grouped,
        "Reconstruction_ms": float(pilots[1]["recipient_processing_ns"]) / 1_000_000.0,
        "Server_Retrieval_ms": critical["retrieval_online_ms"],
        "Server_Online_ms": server_online,
        "Component_Sum_ms": vereval + eval_ms + cprs_grouped,
        "Component_Sum_Error_ms": server_online - (vereval + eval_ms + cprs_grouped),
        "response_bytes": critical["server_to_recipient_bytes"],
        "offline_included": False,
        "measurement_mode": "component_pilot",
        "preprocessing_mode": "local_trusted_pilot",
        "exit_codes": codes,
        "logs": names,
    }


def main():
    HERE.mkdir(parents=True, exist_ok=True)
    LOGS.mkdir(parents=True, exist_ok=True)
    binary_hash = digest([BINARY])
    source_hash = digest(SOURCE_FILES)
    existing = []
    if RAW.exists():
        existing = [json.loads(line) for line in RAW.read_text(encoding="utf-8").splitlines() if line]
        if any(row["binary_sha256"] != binary_hash or row["source_sha256"] != source_hash
               for row in existing):
            raise RuntimeError("cannot resume after binary/source change")
    completed = {(row["Ns"], row["trial_type"], row["trial_index"]) for row in existing}
    port_base = 38100
    for ni, n in enumerate(NS_VALUES):
        tasks = [("warmup", 0)] + [("measured", i) for i in range(MEASURED_REPS)]
        for trial_type, trial_index in tasks:
            if (n, trial_type, trial_index) in completed:
                continue
            row = run_pair(n, trial_type, trial_index,
                           port_base + ni * 20 + trial_index + (0 if trial_type == "warmup" else 1),
                           binary_hash, source_hash)
            append_raw(row)

    rows = [row for row in [json.loads(line) for line in RAW.read_text(encoding="utf-8").splitlines()]
            if row["trial_type"] == "measured"]
    metrics = ["VerGen_ms", "VerEval_ms", "Eval_ms", "Cprs_ms", "Reconstruction_ms",
               "Server_Retrieval_ms", "Server_Online_ms", "Component_Sum_ms",
               "Component_Sum_Error_ms"]
    fields = ["Ns", "Nr", "k", "trial_count"]
    for metric in metrics:
        fields += [metric.replace("_ms", "_median_ms"), metric.replace("_ms", "_min_ms"),
                   metric.replace("_ms", "_max_ms")]
    fields += ["response_bytes", "offline_included", "measurement_mode",
               "preprocessing_mode", "binary_sha256", "source_sha256"]
    with SUMMARY.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for n in NS_VALUES:
            selected = [row for row in rows if row["Ns"] == n]
            output = {"Ns": n, "Nr": NR, "k": K, "trial_count": len(selected)}
            for metric in metrics:
                values = [float(row[metric]) for row in selected]
                output[metric.replace("_ms", "_median_ms")] = f"{statistics.median(values):.9f}"
                output[metric.replace("_ms", "_min_ms")] = f"{min(values):.9f}"
                output[metric.replace("_ms", "_max_ms")] = f"{max(values):.9f}"
            output.update({
                "response_bytes": int(statistics.median(row["response_bytes"] for row in selected)),
                "offline_included": False,
                "measurement_mode": "component_pilot",
                "preprocessing_mode": "local_trusted_pilot",
                "binary_sha256": binary_hash,
                "source_sha256": source_hash,
            })
            writer.writerow(output)


if __name__ == "__main__":
    main()
