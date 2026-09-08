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
SUMMARY = HERE / "summary.csv"
N = 4096
K = 50

BINARIES = {
    "PSVFSS": ROOT / "pvfss/build-paillier3072/src/test",
    "PPS-GC": ROOT / "baselines/pps-gc/pps-garbled-circuits/target/release/examples/unified-bench",
    "FMD": ROOT / "baselines/fmd/fmdbench",
    "OMR": ROOT / "baselines/omr/build-sender-smoke/OMRdemos",
}


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def parse_tag(text, tag):
    found = None
    for line in text.splitlines():
        if line.startswith(tag):
            found = json.loads(line[len(tag):].strip())
    if found is None:
        raise RuntimeError(f"missing {tag}")
    return found


def save_row(row):
    with RAW.open("a", encoding="utf-8") as f:
        f.write(json.dumps(row, sort_keys=True) + "\n")
        f.flush()
        os.fsync(f.fileno())


def run_logged(cmd, cwd, log_name, timeout=1800):
    completed = subprocess.run(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=timeout, check=False)
    (LOGS / log_name).write_text(completed.stdout, encoding="utf-8")
    if completed.returncode != 0:
        raise RuntimeError(f"command failed rc={completed.returncode}; log={log_name}")
    return completed.stdout


def run_psvfss(trial):
    port = 34100 + trial
    common = [str(N), str(N), str(K), "1", "--preprocessing-mode=local-trusted",
              "--measurement-mode=component-pilot"]
    alice_cmd = [str(BINARIES["PSVFSS"]), "1", str(port)] + common
    bob_cmd = [str(BINARIES["PSVFSS"]), "2", str(port)] + common
    alice = subprocess.Popen(alice_cmd, cwd=ROOT / "pvfss", text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             start_new_session=True)
    time.sleep(0.2)
    bob = subprocess.Popen(bob_cmd, cwd=ROOT / "pvfss", text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           start_new_session=True)
    try:
        alice_out, _ = alice.communicate(timeout=1800)
        bob_out, _ = bob.communicate(timeout=30)
    except subprocess.TimeoutExpired:
        for proc in (alice, bob):
            if proc.poll() is None:
                os.killpg(proc.pid, 15)
        raise
    (LOGS / f"psvfss_trial{trial}_alice.log").write_text(alice_out, encoding="utf-8")
    (LOGS / f"psvfss_trial{trial}_bob.log").write_text(bob_out, encoding="utf-8")
    if alice.returncode != 0 or bob.returncode != 0:
        raise RuntimeError(f"PSVFSS processes failed: alice={alice.returncode}, bob={bob.returncode}")
    alice_pilot = parse_tag(alice_out, "[PILOT_JSON]")
    bob_recipient = parse_tag(bob_out, "[PILOT_JSON]")
    return {
        "scheme": "PSVFSS", "N": N, "k_actual": K, "trial_index": trial,
        "recipient_processing_ns": bob_recipient["recipient_processing_ns"],
        "recipient_min_ns": None, "recipient_max_ns": None,
        "recipient_inner_ops": bob_recipient["recipient_inner_ops"],
        "server_to_recipient_bytes": alice_pilot["server_to_recipient_bytes"],
        "correctness": alice_pilot["end_to_end_correctness"],
        "measurement_quality": "component_composed_pilot",
        "consumer_operations": "deserialize_two_share_vectors_field61_add_identity_LocDec",
    }


def main():
    HERE.mkdir(parents=True, exist_ok=True)
    LOGS.mkdir(parents=True, exist_ok=True)
    if RAW.exists():
        raise RuntimeError(f"refusing to overwrite {RAW}")
    metadata = {name: sha256(path) for name, path in BINARIES.items()}
    rows = []

    for trial in range(3):
        output = run_logged(
            [str(BINARIES["PPS-GC"]), "--N", str(N), "--ell", str(K),
             "--recipient-only-component-pilot"],
            ROOT / "baselines/pps-gc/pps-garbled-circuits", f"ppsgc_trial{trial}.log")
        row = parse_tag(output, "[RECIPIENT_JSON]")
        row.update(trial_index=trial, measurement_quality="response_only_component_pilot")
        rows.append(row); save_row(row)

    for trial in range(3):
        output = run_logged(
            [str(BINARIES["FMD"]), "-N", str(N), "-ell", str(K),
             "-gamma", "16", "-reps", "1"],
            ROOT / "baselines/fmd", f"fmd_trial{trial}.log")
        row = parse_tag(output, "[RECIPIENT_JSON]")
        row.update(trial_index=trial, measurement_quality="end_to_end_pilot", fmd_gamma=16)
        rows.append(row); save_row(row)

    for trial in range(3):
        row = run_psvfss(trial)
        rows.append(row); save_row(row)

    output = run_logged(
        [str(BINARIES["OMR"]), "--bench", "omrp1", "--threads", "1",
         "--N", str(N), "--kbar", str(K), "--poly-modulus-degree", "4096",
         "--recipient-bench-reps", "3"],
        ROOT / "baselines/omr", "omr_trial0.log", timeout=1800)
    row = parse_tag(output, "[RECIPIENT_JSON]")
    row.update(trial_index=0, measurement_quality="reduced_parameter_pilot",
               N_physical=4096, security_comparable=False)
    rows.append(row); save_row(row)

    fields = ["scheme", "N", "k_actual", "independent_trials", "inner_ops_per_trial",
              "recipient_processing_median_ms", "recipient_processing_min_ms",
              "recipient_processing_max_ms", "server_to_recipient_bytes",
              "correctness", "measurement_quality", "notes"]
    with SUMMARY.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for scheme in ["PSVFSS", "PPS-GC", "FMD", "OMR"]:
            selected = [r for r in rows if r["scheme"] == scheme]
            medians = [float(r["recipient_processing_ns"]) / 1e6 for r in selected]
            mins = [float(r.get("recipient_min_ns") if r.get("recipient_min_ns") is not None else r["recipient_processing_ns"]) / 1e6 for r in selected]
            maxs = [float(r.get("recipient_max_ns") if r.get("recipient_max_ns") is not None else r["recipient_processing_ns"]) / 1e6 for r in selected]
            correctness_values = [bool(r["correctness"]) for r in selected]
            writer.writerow({
                "scheme": scheme, "N": N, "k_actual": K,
                "independent_trials": len(selected),
                "inner_ops_per_trial": selected[0]["recipient_inner_ops"],
                "recipient_processing_median_ms": f"{statistics.median(medians):.9f}",
                "recipient_processing_min_ms": f"{min(mins):.9f}",
                "recipient_processing_max_ms": f"{max(maxs):.9f}",
                "server_to_recipient_bytes": int(statistics.median([r["response_bytes"] if "response_bytes" in r else r["server_to_recipient_bytes"] for r in selected])),
                "correctness": all(correctness_values),
                "measurement_quality": selected[0]["measurement_quality"],
                "notes": "actual serialized buffer consumption; OMR includes Ciphertext::load/decrypt/decode" if scheme == "OMR" else "actual serialized buffer consumption",
            })

    notes = [
        "# Recipient response consumption experiment",
        "",
        f"- Fixed workload: N={N}, k_actual=k_cap={K}.",
        "- Response construction and server computation are outside recipient timers.",
        "- PSVFSS: deserialize both share vectors, field61 addition, identity LocDec used by the pilot location-handle encoding.",
        "- PPS-GC: deserialize both 32-byte row-share vectors and XOR reconstruct every entry.",
        "- FMD: deserialize count and every candidate uint64 handle; gamma=16; false positives remain in the buffer.",
        "- OMR: parse framing, native SEAL Ciphertext::load twice, decrypt, and decode; degree/slots=4096 reduced-parameter pilot.",
        "- OMR and PSVFSS correctness flags are preserved as observed and are not rewritten to true.",
        "- Binary SHA-256: `" + json.dumps(metadata, sort_keys=True) + "`",
    ]
    (HERE / "RUN_NOTES.md").write_text("\n".join(notes) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
