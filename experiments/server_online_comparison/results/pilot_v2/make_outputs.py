import csv, json
from pathlib import Path

HERE = Path(__file__).resolve().parent
OLD = HERE.parent / "pilot"

rows = []
for line in (OLD / "raw_trials.jsonl").read_text().splitlines():
    r = json.loads(line)
    r["source"] = "results/pilot/raw_trials.jsonl"
    r["N_logical"] = r.pop("N")
    r["N_physical"] = r["N_logical"]
    if r["scheme"] == "PPS-GC":
        r["admission_scope"] = "batch_all_N_signals"
        r["admission_batch_online_ms"] = r.pop("admission_online_ms")
        r["admission_per_signal_ms"] = r["admission_batch_online_ms"] / r["N_logical"]
        r["preprocessing_mode"] = "artifact_native"
    else:
        r["admission_scope"] = "unsupported"
        r["admission_batch_online_ms"] = None
        r["admission_per_signal_ms"] = None
        r["preprocessing_mode"] = "artifact_native"
        r.pop("admission_online_ms", None)
    rows.append(r)

for scheme, note, physical in [
    ("PSVFSS", "field61 response share conversion failed after trusted preprocessing", None),
    ("OMR", "physical-slot homomorphic warmup timed out before final ciphertexts", 32768),
]:
    for n in (32, 64, 128):
        rows.append({
            "result_tier": "pilot_v2", "scheme": scheme, "N_logical": n,
            "N_physical": physical if physical else n, "k_actual": 4,
            "trial_index": None, "admission_scope": "not_measured",
            "admission_batch_online_ms": None, "admission_per_signal_ms": None,
            "retrieval_online_ms": None, "critical_path_wall_ms": None,
            "server_to_recipient_bytes": None,
            "preprocessing_mode": "local_trusted_pilot" if scheme == "PSVFSS" else "artifact_native",
            "security_equivalent": False if scheme == "PSVFSS" else None,
            "measurement_status": "artifact_limited", "notes": note,
        })

with (HERE / "raw_trials.jsonl").open("w") as f:
    for r in rows:
        f.write(json.dumps(r, separators=(",", ":")) + "\n")

def median(v):
    v = sorted(v); return v[len(v)//2]

summary = []
for scheme in ("PSVFSS", "PPS-GC", "FMD", "OMR"):
    for n in (32, 64, 128):
        ok = [r for r in rows if r["scheme"] == scheme and r["N_logical"] == n and r["measurement_status"] == "ok"]
        if ok:
            adm = [r["admission_batch_online_ms"] for r in ok if r.get("admission_batch_online_ms") is not None]
            ret = [r["retrieval_online_ms"] for r in ok]
            crit = [r["critical_path_wall_ms"] for r in ok]
            byt = sorted(set(r["server_to_recipient_bytes"] for r in ok))
            summary.append([scheme,n,n,4,ok[0]["admission_scope"],median(adm) if adm else "",
                median(adm)/n if adm else "",median(ret),median(crit),
                str(byt[0]) if len(byt)==1 else f"{byt[0]}-{byt[-1]}",ok[0]["preprocessing_mode"],"ok","pilot; source=pilot/raw_trials.jsonl"])
        else:
            physical = 32768 if scheme == "OMR" else n
            note = "field share-conversion blocker" if scheme == "PSVFSS" else "timeout before final ciphertext response"
            summary.append([scheme,n,physical,4,"not_measured","","","","","",
                "local_trusted_pilot" if scheme=="PSVFSS" else "artifact_native","artifact_limited",note])

header = ["scheme","N_logical","N_physical","k_actual","admission_scope",
          "admission_batch_online_ms","admission_per_signal_ms","retrieval_online_ms",
          "critical_path_wall_ms","server_to_recipient_bytes","preprocessing_mode",
          "measurement_status","notes"]
with (HERE / "summary.csv").open("w", newline="") as f:
    w=csv.writer(f); w.writerow(header); w.writerows(summary)
