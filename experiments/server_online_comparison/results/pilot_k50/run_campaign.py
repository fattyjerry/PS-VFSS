#!/usr/bin/env python3
"""K=50 comparison campaign, chained after Pilot V4. Generates data and two requested plots."""

import csv
import importlib.util
import json
import os
import subprocess
import time
from pathlib import Path
from statistics import median

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
V4_DIR = HERE.parent / "pilot_v4"
LOGS = HERE / "logs"
RAW = HERE / "raw_trials.jsonl"
TIMEOUTS = HERE / "timeout_trials.jsonl"
STATE = HERE / "runner_state.json"
MANIFEST = HERE / "workload_manifest.json"
HEARTBEAT = HERE / "heartbeat.json"
TIME_CSV = HERE / "server_time_vs_N.csv"
COMM_CSV = HERE / "response_bytes_vs_k.csv"
SUMMARY = HERE / "summary.csv"
NOTES = HERE / "RUN_NOTES.md"

NS = [256, 512, 1024, 2048, 4096]
KS = [4, 10, 20, 30, 40, 50]
SCHEMES = ["PSVFSS", "PPS-GC", "FMD", "OMR"]

spec = importlib.util.spec_from_file_location("pilot_v4_runner", V4_DIR / "run_campaign.py")
v4 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v4)
v4.HERE = HERE
v4.LOGS = LOGS
v4.RAW = RAW
v4.TIMEOUTS = TIMEOUTS
v4.STATE = STATE
v4.MANIFEST = MANIFEST
v4.HEARTBEAT = HEARTBEAT


def atomic_json(path, value):
    v4.atomic_json(path, value)


def key(task):
    return "|".join(str(task[x]) for x in
                    ("experiment_axis", "scheme", "N", "k_actual", "trial_type",
                     "trial_index", "measurement_mode", "binary_sha256"))


def hashes():
    return {s: v4.sha256(p) for s, p in v4.BINARIES.items()}


def quality(scheme):
    return {"PSVFSS": "component_composed_pilot",
            "OMR": "reduced_parameter_pilot"}.get(scheme, "end_to_end_pilot")


def tasks(binary_hashes):
    out = []
    # Server time as N changes, fixed k_actual=k_cap=50.
    for scheme in SCHEMES:
        for n in NS:
            common = {"experiment_axis": "server_time_vs_N", "scheme": scheme, "N": n,
                      "k_actual": 50, "k_cap": 50, "measurement_mode": quality(scheme),
                      "binary_sha256": binary_hashes[scheme], "trial_timeout_seconds": 9000}
            out.append({**common, "trial_type": "warmup", "trial_index": 0})
            out.extend({**common, "trial_type": "measured", "trial_index": i} for i in range(3))
    # Communication as k changes, fixed N=4096. k=50 reuses the above measured trials.
    for scheme in ("PSVFSS", "FMD", "OMR"):
        for k_actual in KS[:-1]:
            common = {"experiment_axis": "response_bytes_vs_k", "scheme": scheme, "N": 4096,
                      "k_actual": k_actual, "k_cap": k_actual,
                      "measurement_mode": quality(scheme), "binary_sha256": binary_hashes[scheme],
                      "trial_timeout_seconds": 9000}
            out.append({**common, "trial_type": "warmup", "trial_index": 0})
            out.extend({**common, "trial_type": "measured", "trial_index": i} for i in range(3))
    # PPS-GC communication curve uses the real row serializer on legal row shares.
    for k_cap in KS:
        common = {"experiment_axis": "response_bytes_vs_k", "scheme": "PPS-GC", "N": 4096,
                  "k_actual": k_cap, "k_cap": k_cap,
                  "measurement_mode": "response_only_component_pilot",
                  "binary_sha256": binary_hashes["PPS-GC"], "trial_timeout_seconds": 9000}
        out.append({**common, "trial_type": "warmup", "trial_index": 0})
        out.extend({**common, "trial_type": "measured", "trial_index": i} for i in range(3))
    return out


def append(path, row):
    v4.append_jsonl(path, row)


def wait_for_v4(state):
    while True:
        other = json.loads((V4_DIR / "runner_state.json").read_text())
        if other.get("status") == "complete":
            return
        hb = {"timestamp_utc": v4.utc_now(), "status": "waiting_for_pilot_v4",
              "pilot_v4_completed": len(other.get("completed", {})),
              "pilot_v4_current": other.get("current")}
        atomic_json(HEARTBEAT, hb)
        state["status"] = "waiting_for_pilot_v4"
        state["heartbeat"] = hb
        atomic_json(STATE, state)
        time.sleep(60)


def run_pps_response(task, attempt):
    stem = f"ppsgc_response_N4096_k{task['k_actual']}_{task['trial_type']}{task['trial_index']}_a{attempt}"
    log = LOGS / f"{stem}.log"
    start = time.monotonic()
    with log.open("w") as out:
        proc = subprocess.run([
            str(v4.BINARIES["PPS-GC"]), "--N", "4096", "--ell", str(task["k_cap"]),
            "--response-only-component-pilot"],
            cwd=v4.BINARIES["PPS-GC"].parents[3], stdout=out, stderr=subprocess.STDOUT,
            timeout=300)
    result = v4.parse_pilot(log)
    elapsed = time.monotonic() - start
    if proc.returncode == 0 and result:
        result.update({"correctness_ok": None, "correctness_status": "not_applicable",
                       "measurement_quality": "response_only_component_pilot",
                       "admission_batch_online_ms": None, "admission_per_signal_ms": None})
        return "ok", elapsed, [proc.returncode], [str(log.relative_to(HERE))], result
    return "error", elapsed, [proc.returncode], [str(log.relative_to(HERE))], None


def terminal(task, status, attempt, elapsed, exits, logs, result):
    row = {"result_tier": "pilot_k50", **task,
           "is_warmup": task["trial_type"] == "warmup", "terminal_status": status,
           "measurement_status": status, "completed_utc": v4.utc_now(),
           "elapsed_process_wall_seconds": round(elapsed, 3), "exit_codes": exits,
           "logs": logs, "attempt": attempt,
           "timeout_seconds": 9000 if status == "timeout" else None,
           "latency_lower_bound_ms": 9000000 if status == "timeout" else None,
           "measured_latency_ms": None}
    if result:
        row.update(result)
        row["measurement_status"] = "ok"
        row["terminal_status"] = "ok"
        row["measured_latency_ms"] = row.get("retrieval_online_ms")
    return row


def values(rows, field):
    return [r[field] for r in rows if isinstance(r.get(field), (int, float))]


def triple(vals):
    return (median(vals), min(vals), max(vals)) if vals else ("", "", "")


def measured(rows, axis, scheme, n=None, k=None):
    return [r for r in rows if r.get("experiment_axis") == axis and r.get("scheme") == scheme
            and r.get("trial_type") == "measured" and r.get("measurement_status") == "ok"
            and (n is None or r.get("N") == n) and (k is None or r.get("k_actual") == k)]


def generate_outputs(started):
    rows = v4.load_jsonl(RAW)
    time_header = ["scheme", "N", "N_physical", "k_actual", "trial_count",
                   "admission_median_ms", "admission_min_ms", "admission_max_ms",
                   "retrieval_median_ms", "retrieval_min_ms", "retrieval_max_ms",
                   "server_online_total_median_ms", "measurement_status", "measurement_quality",
                   "correctness_status"]
    time_rows = []
    for scheme in SCHEMES:
        for n in NS:
            group = measured(rows, "server_time_vs_N", scheme, n=n)
            adm = triple(values(group, "admission_batch_online_ms"))
            ret = triple(values(group, "retrieval_online_ms"))
            totals = [(r.get("admission_batch_online_ms") or 0) + r["retrieval_online_ms"] for r in group]
            status = "ok" if group else "timeout" if any(r.get("scheme") == scheme and r.get("N") == n and r.get("measurement_status") == "timeout" for r in rows) else "error"
            time_rows.append([scheme, n, 4096 if scheme == "OMR" else n, 50, len(group),
                              *adm, *ret, median(totals) if totals else "", status,
                              quality(scheme), group[0].get("correctness_status", "not_measured") if group else "not_measured"])
    with TIME_CSV.open("w", newline="") as f:
        w = csv.writer(f); w.writerow(time_header); w.writerows(time_rows)

    comm_header = ["scheme", "N", "k_actual", "k_cap", "trial_count",
                   "response_median_bytes", "response_min_bytes", "response_max_bytes",
                   "candidate_median", "false_positive_median", "false_positive_min",
                   "false_positive_max", "measurement_status", "measurement_quality",
                   "correctness_status"]
    comm_rows = []
    for scheme in SCHEMES:
        for kval in KS:
            axis = "server_time_vs_N" if kval == 50 and scheme != "PPS-GC" else "response_bytes_vs_k"
            group = measured(rows, axis, scheme, n=4096, k=kval)
            resp = triple(values(group, "server_to_recipient_bytes"))
            fp = triple(values(group, "false_positive_count"))
            candidates = values(group, "candidate_count")
            comm_rows.append([scheme, 4096, kval, kval, len(group), *resp,
                              median(candidates) if candidates else "", *fp,
                              "ok" if group else "error", group[0].get("measurement_quality", quality(scheme)) if group else quality(scheme),
                              group[0].get("correctness_status", "not_measured") if group else "not_measured"])
    with COMM_CSV.open("w", newline="") as f:
        w = csv.writer(f); w.writerow(comm_header); w.writerows(comm_rows)
    with SUMMARY.open("w", newline="") as f:
        w = csv.writer(f); w.writerow(["dataset", "path"]); w.writerow(["server_time_vs_N", TIME_CSV.name]); w.writerow(["response_bytes_vs_k", COMM_CSV.name])

    # The user explicitly requested two figures in this phase.
    try:
        import matplotlib.pyplot as plt
        by_scheme = {s: [r for r in time_rows if r[0] == s] for s in SCHEMES}
        fig, ax = plt.subplots(figsize=(8, 5))
        for s, rr in by_scheme.items():
            x = [r[1] for r in rr if isinstance(r[11], (int, float))]
            y = [r[11] for r in rr if isinstance(r[11], (int, float))]
            ax.plot(x, y, marker="o", label=s)
        ax.set_xscale("log", base=2); ax.set_yscale("log")
        ax.set_xlabel("Stored messages N"); ax.set_ylabel("Server online total (ms)")
        ax.set_title("Preliminary pilot: server computation vs N (k=50)")
        ax.legend(); fig.tight_layout(); fig.savefig(HERE / "server_online_time_vs_N.png", dpi=180); plt.close(fig)

        fig, ax = plt.subplots(figsize=(8, 5))
        for s in SCHEMES:
            rr = [r for r in comm_rows if r[0] == s and isinstance(r[5], (int, float))]
            ax.plot([r[2] for r in rr], [r[5] for r in rr], marker="o", label=s)
        ax.set_yscale("log"); ax.set_xlabel("k_actual = k_cap"); ax.set_ylabel("Actual response bytes")
        ax.set_title("Preliminary pilot: server-to-recipient communication vs k (N=4096)")
        ax.legend(); fig.tight_layout(); fig.savefig(HERE / "response_bytes_vs_k.png", dpi=180); plt.close(fig)
    except Exception as exc:
        (HERE / "plot_error.txt").write_text(str(exc) + "\n")

    NOTES.write_text(
        "# K=50 and k-scaling campaign\n\n"
        "FMD uses the artifact-default gamma=8 (per-non-target false-positive probability 2^-8). "
        "Every trial retains the observed candidate and false-positive counts; no best-case filtering is applied.\n\n"
        "Server-time data varies N at fixed k_actual=k_cap=50. Communication data varies "
        "k_actual=k_cap over 4,10,20,30,40,50 at fixed N=4096. PPS-GC communication-only "
        "rows invoke its real benchmark serializer over legal 32-byte row shares.\n\n"
        f"Campaign elapsed seconds: {time.time()-started:.3f}. PSVFSS remains component-composed; "
        "OMR remains reduced-parameter and non-security-equivalent. Results, not parameter tuning, determine ranking.\n"
    )


def main():
    LOGS.mkdir(parents=True, exist_ok=True)
    bh = hashes()
    manifest = {"schema_version": "pilot-k50-1", "created_utc": v4.utc_now(),
                "server_time_N": NS, "server_time_k": 50, "communication_N": 4096,
                "communication_k": KS, "fmd_gamma": 8, "fmd_false_positive_rate": 1/256,
                "tasks": tasks(bh), "binary_sha256": bh}
    if not MANIFEST.exists(): atomic_json(MANIFEST, manifest)
    if STATE.exists(): state = json.loads(STATE.read_text())
    else:
        state = {"status": "created", "completed": {}, "current": None,
                 "campaign_start_epoch": time.time()}
        atomic_json(STATE, state)
    wait_for_v4(state)
    state["status"] = "running"; atomic_json(STATE, state)

    for task in manifest["tasks"]:
        tkey = key(task)
        if tkey in state["completed"]: continue
        state["current"] = task; atomic_json(STATE, state)
        final = None
        for attempt in (0, 1):
            if task["measurement_mode"] == "response_only_component_pilot":
                status, elapsed, exits, logs, result = run_pps_response(task, attempt)
            else:
                status, elapsed, exits, logs, result = v4.run_attempt(task, attempt, state)
            if status == "ok":
                final = terminal(task, "ok", attempt, elapsed, exits, logs, result); break
            if status == "timeout":
                final = terminal(task, "timeout", attempt, elapsed, exits, logs, None); break
            if attempt == 1:
                final = terminal(task, "error_after_retry", attempt, elapsed, exits, logs, None)
        append(RAW, final)
        if final["measurement_status"] == "timeout": append(TIMEOUTS, final)
        state["completed"][tkey] = final["measurement_status"]
        state["current"] = None; atomic_json(STATE, state)
    generate_outputs(state["campaign_start_epoch"])
    state["status"] = "complete"; state["completed_utc"] = v4.utc_now(); atomic_json(STATE, state)
    atomic_json(HEARTBEAT, {"timestamp_utc": v4.utc_now(), "status": "complete"})


if __name__ == "__main__":
    main()
