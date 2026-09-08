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
CSV = HERE / "psvfss_online_breakdown_eval_split_5trials.csv"
NOTES = HERE / "RUN_NOTES.md"
BINARY = ROOT / "pvfss/build-paillier3072/src/test"
SOURCE = ROOT / "pvfss/src/test.cc"
NS_VALUES = [256, 512, 1024, 2048, 4096, 8192, 16384]
K = 50
MEASURED_REPS = 5


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
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


def run_pair(n, kind, index, port, binary_hash, source_hash):
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
        deadline = time.monotonic() + 60
        while any(p.poll() is None for p in processes):
            if time.monotonic() >= deadline:
                for p in processes:
                    if p.poll() is None:
                        os.killpg(p.pid, 15)
                time.sleep(1)
                for p in processes:
                    if p.poll() is None:
                        os.killpg(p.pid, 9)
                raise RuntimeError(f"timeout N={n} {kind}={index}")
            time.sleep(0.1)
    finally:
        for handle in handles:
            handle.flush()
            os.fsync(handle.fileno())
            handle.close()

    codes = [p.wait() for p in processes]
    if codes != [0, 0]:
        raise RuntimeError(f"party failure N={n} {kind}={index}: {codes}")

    outputs = [(LOGS / name).read_text(encoding="utf-8") for name in names]
    pilots = [tagged_json(output, "[PILOT_JSON]") for output in outputs]
    client = tagged_json(outputs[0], "[CLIENT_BENCH_JSON]")
    critical_party = max(range(2), key=lambda i: pilots[i]["retrieval_online_ms"])
    critical = pilots[critical_party]
    signal_verification = max(p["admission_batch_online_ms"] for p in pilots)
    server_sum = (critical["retrieval_eval_core_ms"] +
                  critical["retrieval_proof_check_ms"] +
                  critical["retrieval_shuffle_ms"] +
                  critical["retrieval_compression_ms"] +
                  critical["response_serialization_ms"])
    return {
        "schema": "psvfss-online-breakdown-eval-split-v1",
        "Ns": n,
        "k": K,
        "trial_type": kind,
        "trial_index": index,
        "binary_sha256": binary_hash,
        "source_sha256": source_hash,
        "critical_server_party": "ALICE" if critical_party == 0 else "BOB",
        "VerGen_ms": float(client["sender_signaling_generation_ns"]) / 1_000_000.0,
        "Signal_Verification_ms": signal_verification,
        "Eval_ms": critical["retrieval_eval_core_ms"],
        "Retrieval_Proof_Check_ms": critical["retrieval_proof_check_ms"],
        "Eval_And_Proof_Check_ms": critical["retrieval_eval_ms"],
        "Shuffle_ms": critical["retrieval_shuffle_ms"],
        "Cprs_ms": critical["retrieval_compression_ms"],
        "Serialization_ms": critical["response_serialization_ms"],
        "Reconstruction_ms": float(pilots[1]["recipient_processing_ns"]) / 1_000_000.0,
        "Server_Retrieval_Total_ms": critical["retrieval_online_ms"],
        "Server_Component_Sum_ms": server_sum,
        "Server_Total_Error_ms": critical["retrieval_online_ms"] - server_sum,
        "response_bytes": critical["server_to_recipient_bytes"],
        "offline_included": False,
        "measurement_mode": "component_pilot",
        "preprocessing_mode": "local_trusted_pilot",
        "measurement_quality": critical["measurement_quality"],
        "end_to_end_correctness": critical["end_to_end_correctness"],
        "exit_codes": codes,
        "logs": names,
    }


def aggregate(rows, field):
    values = [float(row[field]) for row in rows]
    return statistics.median(values), min(values), max(values)


def main():
    HERE.mkdir(parents=True, exist_ok=True)
    LOGS.mkdir(parents=True, exist_ok=True)
    if CSV.exists():
        raise RuntimeError("refusing to overwrite a completed split campaign")
    binary_hash = digest(BINARY)
    source_hash = digest(SOURCE)
    existing = []
    if RAW.exists():
        existing = [json.loads(line) for line in RAW.read_text(encoding="utf-8").splitlines()
                    if line]
        if any(row["binary_sha256"] != binary_hash or
               row["source_sha256"] != source_hash for row in existing):
            raise RuntimeError("cannot resume with changed source or binary")
    rows = [row for row in existing if row["trial_type"] == "measured"]
    completed = {(row["Ns"], row["trial_type"], row["trial_index"])
                 for row in existing}
    port_base = 36100
    for n_index, n in enumerate(NS_VALUES):
        if (n, "warmup", 0) not in completed:
            last_error = None
            for attempt in range(2):
                try:
                    row = run_pair(n, "warmup", 0,
                                   port_base + n_index * 20 + attempt * 1000,
                                   binary_hash, source_hash)
                    append_raw(row)
                    break
                except RuntimeError as error:
                    last_error = error
            else:
                raise RuntimeError(f"warmup failed after retry: {last_error}")
        for trial in range(MEASURED_REPS):
            if (n, "measured", trial) in completed:
                continue
            last_error = None
            for attempt in range(2):
                try:
                    row = run_pair(n, "measured", trial,
                                   port_base + n_index * 20 + trial + 1 + attempt * 1000,
                                   binary_hash, source_hash)
                    append_raw(row)
                    rows.append(row)
                    break
                except RuntimeError as error:
                    last_error = error
            else:
                raise RuntimeError(f"trial failed after retry: {last_error}")

    metrics = ["VerGen_ms", "Signal_Verification_ms", "Eval_ms",
               "Retrieval_Proof_Check_ms", "Eval_And_Proof_Check_ms",
               "Shuffle_ms", "Cprs_ms", "Serialization_ms",
               "Reconstruction_ms", "Server_Retrieval_Total_ms"]
    fields = ["Ns", "k", "trial_count"]
    for metric in metrics:
        fields += [metric, metric.replace("_ms", "_min_ms"),
                   metric.replace("_ms", "_max_ms")]
    fields += ["Eval_Percent_Of_Server_Retrieval",
               "Proof_Check_Percent_Of_Server_Retrieval",
               "Cprs_Percent_Of_Server_Retrieval", "binary_sha256",
               "source_sha256", "offline_included", "measurement_mode",
               "preprocessing_mode", "measurement_quality", "correctness_status"]
    with CSV.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for n in NS_VALUES:
            selected = [row for row in rows if row["Ns"] == n]
            output = {"Ns": n, "k": K, "trial_count": len(selected)}
            medians = {}
            for metric in metrics:
                median, minimum, maximum = aggregate(selected, metric)
                medians[metric] = median
                output[metric] = f"{median:.9f}"
                output[metric.replace("_ms", "_min_ms")] = f"{minimum:.9f}"
                output[metric.replace("_ms", "_max_ms")] = f"{maximum:.9f}"
            total = medians["Server_Retrieval_Total_ms"]
            output.update({
                "Eval_Percent_Of_Server_Retrieval": f"{100 * medians['Eval_ms'] / total:.9f}",
                "Proof_Check_Percent_Of_Server_Retrieval": f"{100 * medians['Retrieval_Proof_Check_ms'] / total:.9f}",
                "Cprs_Percent_Of_Server_Retrieval": f"{100 * medians['Cprs_ms'] / total:.9f}",
                "binary_sha256": binary_hash,
                "source_sha256": source_hash,
                "offline_included": False,
                "measurement_mode": "component_pilot",
                "preprocessing_mode": "local_trusted_pilot",
                "measurement_quality": "component_composed_pilot",
                "correctness_status": "passed" if all(r["end_to_end_correctness"] for r in selected) else "failed",
            })
            writer.writerow(output)

    NOTES.write_text(
        "# PSVFSS Eval/proof-check split pilot\n\n"
        f"- Ns: {NS_VALUES}; k={K}; one warmup and {MEASURED_REPS} measured trials.\n"
        "- Signal_Verification is the phase-critical admission timer.\n"
        "- Eval is only the retrieval EvalWithProof computation loop.\n"
        "- Retrieval_Proof_Check is proof exchange and comparison after Eval.\n"
        "- Offline preprocessing, setup, diagnostics, and reconstruction are excluded from server retrieval.\n"
        "- Protocol operations and message order were not changed; only timer boundaries and JSON fields were added.\n"
        "- Results remain component-composed/local-trusted pilot and are not security-equivalent end-to-end results.\n"
        f"- Source SHA-256: `{source_hash}`\n- Binary SHA-256: `{binary_hash}`\n",
        encoding="utf-8")


if __name__ == "__main__":
    main()
