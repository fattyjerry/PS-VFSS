import csv
import json
import re
from pathlib import Path
from statistics import median

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
OLD_RAW = HERE.parent / "pilot" / "raw_trials.jsonl"


def pilot_json(path):
    text = path.read_text(errors="replace")
    matches = re.findall(r"\[PILOT_JSON\]\s+(\{.*\})", text)
    if not matches:
        raise RuntimeError(f"missing PILOT_JSON in {path}")
    return json.loads(matches[-1])


rows = []
for line in OLD_RAW.read_text().splitlines():
    old = json.loads(line)
    n = old.pop("N")
    admission = old.pop("admission_online_ms", None)
    old.update({
        "result_tier": "pilot_v3",
        "source": "results/pilot/raw_trials.jsonl",
        "N_logical": n,
        "N_physical": n,
        "admission_scope": "batch_all_N_signals" if old["scheme"] == "PPS-GC" else "unsupported",
        "admission_batch_online_ms": admission,
        "admission_per_signal_ms": admission / n if admission is not None else None,
        "measurement_quality": "end_to_end_pilot",
        "correctness_status": "not_recorded",
        "correctness_ok": None,
        "parameter_or_preprocessing_mode": "artifact_native",
    })
    rows.append(old)

# rep=0 is the warmup. Each measured PSVFSS row combines the two synchronized
# server observations using the phase critical path (maximum wall time).
for n in (32, 64, 128):
    for rep in (1, 2, 3):
        alice = pilot_json(HERE / "logs" / f"psvfss_N{n}_r{rep}_alice.log")
        bob = pilot_json(HERE / "logs" / f"psvfss_N{n}_r{rep}_bob.log")
        row = dict(alice)
        row.update({
            "result_tier": "pilot_v3", "trial_index": rep - 1, "is_warmup": False,
            "N_logical": n, "N_physical": n, "admission_scope": "batch_all_N_signals",
            "admission_batch_online_ms": max(alice["admission_batch_online_ms"], bob["admission_batch_online_ms"]),
            "admission_per_signal_ms": max(alice["admission_batch_online_ms"], bob["admission_batch_online_ms"]) / n,
            "retrieval_online_ms": max(alice["retrieval_online_ms"], bob["retrieval_online_ms"]),
            "critical_path_wall_ms": max(alice["retrieval_online_ms"], bob["retrieval_online_ms"]),
            "measurement_quality": "component_composed_pilot", "correctness_status": "failed",
            "correctness_ok": False, "parameter_or_preprocessing_mode": "local_trusted_pilot",
            "source": f"pilot_v3/logs/psvfss_N{n}_r{rep}_[alice|bob].log",
            "notes": "real VDPF admission/eval plus real Shuffle/Mult/Cprs; composed field input; end-to-end field conversion unresolved",
        })
        rows.append(row)

omr_reps = {32: (1, 2, 3), 64: (2, 3, 4), 128: (1, 2, 3)}
for n, reps in omr_reps.items():
    for trial_index, rep in enumerate(reps):
        row = pilot_json(HERE / "logs" / f"omr_N{n}_r{rep}.log")
        row.update({
            "result_tier": "pilot_v3", "trial_index": trial_index, "is_warmup": False,
            "N_logical": n, "N_physical": row["slot_count"], "admission_scope": "unsupported",
            "admission_batch_online_ms": None, "admission_per_signal_ms": None,
            "critical_path_wall_ms": row["retrieval_online_ms"],
            "measurement_quality": "reduced_parameter_pilot", "correctness_ok": False,
            "parameter_or_preprocessing_mode": "bfv_degree_4096_sec_level_none",
            "source": f"pilot_v3/logs/omr_N{n}_r{rep}.log",
            "notes": "full homomorphic sequence and native ciphertext serialization; reduced parameters; recipient check failed",
        })
        rows.append(row)

with (HERE / "raw_trials.jsonl").open("w") as out:
    for row in rows:
        out.write(json.dumps(row, separators=(",", ":")) + "\n")

header = [
    "scheme", "N_logical", "N_physical", "k_actual", "trial_count",
    "admission_batch_online_ms", "admission_per_signal_ms", "retrieval_online_ms",
    "server_to_recipient_bytes", "measurement_quality", "correctness_status",
    "parameter_or_preprocessing_mode", "notes",
]
summary = []
for scheme in ("PSVFSS", "PPS-GC", "FMD", "OMR"):
    for n in (32, 64, 128):
        group = [r for r in rows if r["scheme"] == scheme and r["N_logical"] == n and r["measurement_status"] == "ok"]
        admissions = [r["admission_batch_online_ms"] for r in group if r.get("admission_batch_online_ms") is not None]
        per_signal = [r["admission_per_signal_ms"] for r in group if r.get("admission_per_signal_ms") is not None]
        retrievals = [r["retrieval_online_ms"] for r in group]
        response = [r["server_to_recipient_bytes"] for r in group]
        quality = group[0]["measurement_quality"]
        correctness_values = [r.get("correctness_ok") for r in group]
        correctness = ("failed" if any(v is False for v in correctness_values)
                       else "passed" if all(v is True for v in correctness_values)
                       else "not_recorded")
        notes = f"actual response range={min(response)}-{max(response)} bytes"
        if scheme == "PSVFSS":
            notes += "; component-composed, not end-to-end correct"
        if scheme == "OMR":
            notes += "; BFV degree/slots=4096, sec_level=none, recipient check failed"
        summary.append([
            scheme, n, group[0]["N_physical"], 4, len(group),
            median(admissions) if admissions else "", median(per_signal) if per_signal else "",
            median(retrievals), median(response), quality, correctness,
            group[0]["parameter_or_preprocessing_mode"], notes,
        ])

with (HERE / "summary.csv").open("w", newline="") as out:
    writer = csv.writer(out)
    writer.writerow(header)
    writer.writerows(summary)
