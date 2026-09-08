#!/usr/bin/env python3
import csv
import hashlib
import json
import os
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
LOGS = HERE / "logs"
RAW = HERE / "raw_trials.jsonl"
CSV = HERE / "psvfss_online_breakdown.csv"
NS = [256, 512, 1024, 2048, 4096]
K = 50
MEASURED_REPS = 3
BINARY = ROOT / "pvfss/build-paillier3072/src/test"


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_json_line(text, tag):
    values = []
    for line in text.splitlines():
        if line.startswith(tag):
            values.append(json.loads(line[len(tag):].strip()))
    if not values:
        raise RuntimeError(f"missing {tag}")
    return values[-1]


def append_raw(row):
    with RAW.open("a", encoding="utf-8") as f:
        f.write(json.dumps(row, sort_keys=True) + "\n")
        f.flush()
        os.fsync(f.fileno())


def run_pair(n, kind, index, port, binary_hash):
    common = [str(n), str(n), str(K), "1",
              "--preprocessing-mode=local-trusted",
              "--measurement-mode=component-pilot"]
    commands = [
        [str(BINARY), "1", str(port)] + common,
        [str(BINARY), "2", str(port)] + common,
    ]
    names = [f"N{n}_{kind}{index}_alice.log", f"N{n}_{kind}{index}_bob.log"]
    handles = [(LOGS / name).open("w", encoding="utf-8") for name in names]
    processes = []
    try:
        for party in range(2):
            processes.append(subprocess.Popen(
                commands[party], cwd=ROOT / "pvfss", stdout=handles[party],
                stderr=subprocess.STDOUT, start_new_session=True))
            if party == 0:
                time.sleep(0.2)
        deadline = time.monotonic() + 120
        while any(p.poll() is None for p in processes):
            if time.monotonic() >= deadline:
                for p in processes:
                    if p.poll() is None:
                        os.killpg(p.pid, 15)
                time.sleep(1)
                for p in processes:
                    if p.poll() is None:
                        os.killpg(p.pid, 9)
                raise RuntimeError(f"phase timeout N={n} {kind}={index}")
            time.sleep(0.1)
    finally:
        for h in handles:
            h.flush(); os.fsync(h.fileno()); h.close()
    codes = [p.wait() for p in processes]
    if codes != [0, 0]:
        raise RuntimeError(f"party failure N={n} {kind}={index} exit_codes={codes}")

    outputs = [(LOGS / name).read_text(encoding="utf-8") for name in names]
    pilots = [parse_json_line(out, "[PILOT_JSON]") for out in outputs]
    client = parse_json_line(outputs[0], "[CLIENT_BENCH_JSON]")

    # Critical-path accounting: choose the server whose complete retrieval
    # total is larger, then retain all components from that same server.
    critical_party = max(range(2), key=lambda i: pilots[i]["retrieval_online_ms"])
    critical = pilots[critical_party]
    reconstruction_ms = float(pilots[1]["recipient_processing_ns"]) / 1_000_000.0
    vergen_ms = float(client["sender_signaling_generation_ns"]) / 1_000_000.0
    component_sum = (critical["retrieval_eval_ms"] + critical["retrieval_shuffle_ms"] +
                     critical["retrieval_compression_ms"] + critical["response_serialization_ms"])
    server_total = critical["retrieval_online_ms"]
    row = {
        "schema": "psvfss-online-breakdown-v1",
        "Ns": n,
        "k": K,
        "trial_type": kind,
        "trial_index": index,
        "binary_sha256": binary_hash,
        "measurement_mode": "component_pilot",
        "preprocessing_mode": "local_trusted_pilot",
        "offline_included": False,
        "critical_server_party": "ALICE" if critical_party == 0 else "BOB",
        "VerGen_ms": vergen_ms,
        "VerEval_ms": critical["retrieval_eval_ms"],
        "Shuffle_ms": critical["retrieval_shuffle_ms"],
        "Cprs_ms": critical["retrieval_compression_ms"],
        "Serialization_ms": critical["response_serialization_ms"],
        "Reconstruction_ms": reconstruction_ms,
        "Server_Retrieval_Total_ms": server_total,
        "Server_Component_Sum_ms": component_sum,
        "Server_Total_Error_ms": server_total - component_sum,
        "End_to_End_Online_Total_ms": vergen_ms + server_total + reconstruction_ms,
        "response_bytes": critical["server_to_recipient_bytes"],
        "end_to_end_correctness": critical["end_to_end_correctness"],
        "measurement_quality": critical["measurement_quality"],
        "exit_codes": codes,
        "logs": names,
    }
    return row


def stat(rows, field, op):
    values = [float(r[field]) for r in rows]
    return {"median": statistics.median(values), "min": min(values), "max": max(values)}[op]


def main():
    HERE.mkdir(parents=True, exist_ok=True)
    LOGS.mkdir(parents=True, exist_ok=True)
    if CSV.exists():
        raise RuntimeError("refusing to overwrite a completed breakdown campaign")
    binary_hash = digest(BINARY)
    existing = []
    if RAW.exists():
        existing = [json.loads(line) for line in RAW.read_text(encoding="utf-8").splitlines() if line]
        if any(row["binary_sha256"] != binary_hash for row in existing):
            raise RuntimeError("cannot resume: binary hash changed")
    measured = [row for row in existing if row["trial_type"] == "measured"]
    completed = {(row["Ns"], row["trial_type"], row["trial_index"]) for row in existing}
    port = 35100
    for n_index, n in enumerate(NS):
        if (n, "warmup", 0) not in completed:
            warmup = run_pair(n, "warmup", 0, port + n_index * 20, binary_hash)
            append_raw(warmup)
        for trial in range(MEASURED_REPS):
            if (n, "measured", trial) in completed:
                continue
            last_error = None
            for attempt in range(2):
                try:
                    row = run_pair(n, "measured", trial,
                                   port + n_index * 20 + trial + 1 + attempt * 1000,
                                   binary_hash)
                    append_raw(row)
                    measured.append(row)
                    break
                except RuntimeError as error:
                    last_error = error
            else:
                raise RuntimeError(f"trial failed after retry: {last_error}")

    base_fields = ["VerGen_ms", "VerEval_ms", "Shuffle_ms", "Cprs_ms",
                   "Serialization_ms", "Reconstruction_ms",
                   "Server_Retrieval_Total_ms", "End_to_End_Online_Total_ms"]
    fields = ["Ns", "k", "trial_count"]
    for field in base_fields:
        fields.extend([field, field.replace("_ms", "_min_ms"), field.replace("_ms", "_max_ms")])
    fields.extend(["Server_Component_Sum_Of_Medians_ms", "Server_Total_Error_Of_Medians_ms",
                   "Max_Abs_Per_Trial_Server_Total_Error_ms", "offline_included",
                   "measurement_mode", "preprocessing_mode", "binary_sha256",
                   "measurement_quality", "correctness_status"])

    with CSV.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for n in NS:
            rows = [r for r in measured if r["Ns"] == n]
            out = {"Ns": n, "k": K, "trial_count": len(rows)}
            for field in base_fields:
                out[field] = f"{stat(rows, field, 'median'):.9f}"
                out[field.replace("_ms", "_min_ms")] = f"{stat(rows, field, 'min'):.9f}"
                out[field.replace("_ms", "_max_ms")] = f"{stat(rows, field, 'max'):.9f}"
            component_medians = sum(stat(rows, x, "median") for x in
                                    ["VerEval_ms", "Shuffle_ms", "Cprs_ms", "Serialization_ms"])
            total_median = stat(rows, "Server_Retrieval_Total_ms", "median")
            out.update({
                "Server_Component_Sum_Of_Medians_ms": f"{component_medians:.9f}",
                "Server_Total_Error_Of_Medians_ms": f"{total_median - component_medians:.9f}",
                "Max_Abs_Per_Trial_Server_Total_Error_ms": f"{max(abs(r['Server_Total_Error_ms']) for r in rows):.9f}",
                "offline_included": False,
                "measurement_mode": "component_pilot",
                "preprocessing_mode": "local_trusted_pilot",
                "binary_sha256": binary_hash,
                "measurement_quality": "component_composed_pilot",
                "correctness_status": "failed" if not all(r["end_to_end_correctness"] for r in rows) else "passed",
            })
            writer.writerow(out)

    notes = (
        "# PSVFSS current-path online breakdown\n\n"
        f"- Binary: `{BINARY.relative_to(ROOT)}`\n"
        f"- Binary SHA-256: `{binary_hash}`\n"
        "- Parameters: Ns={256,512,1024,2048,4096}, k=50; one warmup and three measured trials.\n"
        "- Same command path as the current latency runner: local trusted preprocessing and component pilot.\n"
        "- Offline preprocessing, connection setup, diagnostics, and correctness checks are excluded.\n"
        "- Critical server components all come from the same slower party for each trial.\n"
        "- The current binary differs from the archived pilot_fast8h binary because recipient timing instrumentation was added; update the PSVFSS latency curve from this campaign's Server_Retrieval_Total_ms.\n"
        "- End-to-end correctness remains failed in component-composed pilot mode; results are preliminary.\n"
    )
    (HERE / "RUN_NOTES.md").write_text(notes, encoding="utf-8")


if __name__ == "__main__":
    main()
