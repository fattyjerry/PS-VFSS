#!/usr/bin/env python3
"""Eight-hour prioritized pilot. Real runs/buffers only; incomplete points stay empty."""

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
LOGS = HERE / "logs"
RAW = HERE / "raw_trials.jsonl"
TIMEOUTS = HERE / "timeout_trials.jsonl"
STATE = HERE / "runner_state.json"
MANIFEST = HERE / "workload_manifest.json"
HEARTBEAT = HERE / "heartbeat.json"
TIME_CSV = HERE / "server_time_vs_N.csv"
COMM_CSV = HERE / "response_bytes_vs_k.csv"
NOTES = HERE / "RUN_NOTES.md"

NS = [256, 512, 1024, 2048, 4096]
KS = [4, 10, 20, 30, 40, 50]
WINDOW_SECONDS = 8 * 3600
OUTPUT_RESERVE_SECONDS = 20 * 60

spec = importlib.util.spec_from_file_location("v4", ROOT / "experiments/server_online_comparison/results/pilot_v4/run_campaign.py")
v4 = importlib.util.module_from_spec(spec); spec.loader.exec_module(v4)
v4.HERE, v4.LOGS, v4.RAW, v4.TIMEOUTS, v4.STATE, v4.HEARTBEAT = HERE, LOGS, RAW, TIMEOUTS, STATE, HEARTBEAT


def atom(path, value): v4.atomic_json(path, value)
def append(path, value): v4.append_jsonl(path, value)


def mode(s):
    return {"PSVFSS": "component_composed_pilot", "OMR": "reduced_parameter_pilot"}.get(s, "end_to_end_pilot")


def make_task(axis, scheme, n, k, kind, idx, hashes, gamma=None, response_only=False):
    return {"experiment_axis": axis, "scheme": scheme, "N": n, "k_actual": k, "k_cap": k,
            "trial_type": kind, "trial_index": idx,
            "measurement_mode": "response_only_component_pilot" if response_only else mode(scheme),
            "binary_sha256": hashes[scheme], "fmd_gamma": gamma,
            "trial_timeout_seconds": 7200}


def task_key(t):
    return "|".join(str(t.get(x)) for x in ("experiment_axis", "scheme", "N", "k_actual",
                    "fmd_gamma", "trial_type", "trial_index", "measurement_mode", "binary_sha256"))


def build_tasks(hashes):
    out = []
    # First priority: complete response-vs-k data at N=4096.
    for scheme in ("PPS-GC", "PSVFSS", "FMD", "OMR"):
        gammas = (8, 6) if scheme == "FMD" else (None,)
        for gamma in gammas:
            for k in KS:
                response_only = scheme == "PPS-GC"
                out.append(make_task("response_bytes_vs_k", scheme, 4096, k, "warmup", 0, hashes, gamma, response_only))
                out.append(make_task("response_bytes_vs_k", scheme, 4096, k, "measured", 0, hashes, gamma, response_only))
    # Second priority: expensive PPS-GC admission, one real measured run per N.
    # Omitting a separate warmup preserves the power window and guarantees the
    # plotting dataset is attempted before supplementary repetitions.
    for n in NS[:3]:
        out.append(make_task("server_time_vs_N", "PPS-GC", n, 50, "measured", 0, hashes))
    # Third priority: faster schemes' server time vs N at k=50.
    for scheme in ("FMD", "PSVFSS", "OMR"):
        gammas = (8, 6) if scheme == "FMD" else (None,)
        for gamma in gammas:
            for n in NS:
                out.append(make_task("server_time_vs_N", scheme, n, 50, "warmup", 0, hashes, gamma))
                out.append(make_task("server_time_vs_N", scheme, n, 50, "measured", 0, hashes, gamma))
    # Only after the four curves have a usable common range, spend remaining
    # power-window time on the two expensive PPS-GC tail points.
    for n in NS[3:]:
        out.append(make_task("server_time_vs_N", "PPS-GC", n, 50, "measured", 0, hashes))
    return out


def pps_response(t, attempt):
    log = LOGS / f"ppsgc_response_k{t['k_actual']}_{t['trial_type']}{t['trial_index']}_a{attempt}.log"
    started = time.monotonic()
    with log.open("w") as f:
        p = subprocess.run([str(v4.BINARIES["PPS-GC"]), "--N", "4096", "--ell", str(t["k_cap"]),
                            "--response-only-component-pilot"], cwd=v4.BINARIES["PPS-GC"].parents[3],
                           stdout=f, stderr=subprocess.STDOUT, timeout=300)
    result = v4.parse_pilot(log)
    if p.returncode == 0 and result:
        result.update(measurement_quality="response_only_component_pilot", correctness_ok=None,
                      correctness_status="not_applicable", admission_batch_online_ms=None,
                      admission_per_signal_ms=None)
        return "ok", time.monotonic()-started, [p.returncode], [str(log.relative_to(HERE))], result
    return "error", time.monotonic()-started, [p.returncode], [str(log.relative_to(HERE))], None


def terminal(t, status, attempt, elapsed, exits, logs, result=None):
    row = {"result_tier": "pilot_fast8h", **t, "is_warmup": t["trial_type"] == "warmup",
           "measurement_status": status, "terminal_status": status, "completed_utc": v4.utc_now(),
           "elapsed_process_wall_seconds": round(elapsed, 3), "exit_codes": exits, "logs": logs,
           "attempt": attempt, "timeout_seconds": t["trial_timeout_seconds"] if status == "timeout" else None,
           "latency_lower_bound_ms": t["trial_timeout_seconds"]*1000 if status == "timeout" else None}
    if result:
        row.update(result); row["measurement_status"] = row["terminal_status"] = "ok"
    return row


def measured(rows, axis, scheme, n=None, k=None, gamma=None):
    return [r for r in rows if r.get("experiment_axis") == axis and r.get("scheme") == scheme
            and r.get("trial_type") == "measured" and r.get("measurement_status") == "ok"
            and (n is None or r.get("N") == n) and (k is None or r.get("k_actual") == k)
            and (scheme != "FMD" or r.get("fmd_gamma") == gamma)]


def med(group, field):
    vals = [r[field] for r in group if isinstance(r.get(field), (int, float))]
    return median(vals) if vals else ""


def generate_outputs(started):
    rows = v4.load_jsonl(RAW)
    time_header = ["series", "scheme", "fmd_gamma", "N", "N_physical", "k_actual", "trial_count",
                   "admission_batch_ms", "admission_per_signal_ms", "retrieval_online_ms",
                   "server_online_total_ms", "measurement_status", "measurement_quality", "correctness_status"]
    time_rows = []
    series_defs = [("PSVFSS", None, "PSVFSS"), ("PPS-GC", None, "PPS-GC"),
                   ("FMD", 8, "FMD gamma=8"), ("FMD", 6, "FMD gamma=6"), ("OMR", None, "OMR")]
    for scheme, gamma, label in series_defs:
        for n in NS:
            g = measured(rows, "server_time_vs_N", scheme, n=n, gamma=gamma)
            adm, per, ret = med(g, "admission_batch_online_ms"), med(g, "admission_per_signal_ms"), med(g, "retrieval_online_ms")
            total = (adm if isinstance(adm, (int,float)) else 0) + ret if isinstance(ret, (int,float)) else ""
            time_rows.append([label, scheme, gamma if gamma else "", n, 4096 if scheme == "OMR" else n, 50,
                              len(g), adm, per, ret, total, "ok" if g else "not_completed",
                              g[0].get("measurement_quality", mode(scheme)) if g else mode(scheme),
                              g[0].get("correctness_status", "not_measured") if g else "not_measured"])
    with TIME_CSV.open("w", newline="") as f:
        w=csv.writer(f); w.writerow(time_header); w.writerows(time_rows)

    comm_header = ["series", "scheme", "fmd_gamma", "N", "k_actual", "trial_count",
                   "response_bytes", "candidate_count", "false_positive_count", "measurement_status",
                   "measurement_quality", "correctness_status"]
    comm_rows=[]
    for scheme, gamma, label in series_defs:
        for k in KS:
            g=measured(rows,"response_bytes_vs_k",scheme,n=4096,k=k,gamma=gamma)
            comm_rows.append([label,scheme,gamma if gamma else "",4096,k,len(g),med(g,"server_to_recipient_bytes"),
                              med(g,"candidate_count"),med(g,"false_positive_count"),"ok" if g else "not_completed",
                              g[0].get("measurement_quality",mode(scheme)) if g else mode(scheme),
                              g[0].get("correctness_status","not_measured") if g else "not_measured"])
    with COMM_CSV.open("w",newline="") as f:
        w=csv.writer(f); w.writerow(comm_header); w.writerows(comm_rows)

    NOTES.write_text(f"# Eight-hour prioritized pilot\n\nElapsed seconds: {time.time()-started:.3f}. "
                     "One warmup plus one measured trial was scheduled per point. Communication data ran first. "
                     "FMD gamma=8 is the artifact-default primary series; gamma=6 is a labeled high-false-positive sensitivity. "
                     "Missing points were not predicted or filled. PSVFSS is component-composed; OMR uses degree/slots 4096. "
                     "Data only: no plots were generated.\n")


def main():
    LOGS.mkdir(parents=True,exist_ok=True)
    hashes={s:v4.sha256(p) for s,p in v4.BINARIES.items()}
    tasks=build_tasks(hashes)
    manifest={"schema_version":"fast8h-1","created_utc":v4.utc_now(),"deadline_seconds":WINDOW_SECONDS,
              "output_reserve_seconds":OUTPUT_RESERVE_SECONDS,"fmd_primary_gamma":8,"fmd_sensitivity_gamma":6,
              "tasks":tasks,"binary_sha256":hashes}
    atom(MANIFEST,manifest)
    if STATE.exists():
        state=json.loads(STATE.read_text())
        state["status"]="running"
        state["current"]=None
    else:
        state={"status":"running","campaign_start_epoch":time.time(),"deadline_epoch":time.time()+WINDOW_SECONDS,
               "completed":{},"current":None}
    atom(STATE,state)
    for t in tasks:
        if task_key(t) in state["completed"]:
            continue
        remaining=state["deadline_epoch"]-time.time()
        if remaining<=OUTPUT_RESERVE_SECONDS:
            row=terminal(t,"not_run_power_window",0,0,[],[],None); append(RAW,row)
            state["completed"][task_key(t)]=row["measurement_status"]; atom(STATE,state); continue
        t["trial_timeout_seconds"]=int(min(7200,max(60,remaining-OUTPUT_RESERVE_SECONDS)))
        v4.TIMEOUT_SECONDS=t["trial_timeout_seconds"]
        state["current"]=t; atom(STATE,state)
        final=None
        for attempt in (0,1):
            status,elapsed,exits,logs,result = pps_response(t,attempt) if t["measurement_mode"]=="response_only_component_pilot" else v4.run_attempt(t,attempt,state)
            if status in ("ok","timeout"):
                final=terminal(t,status,attempt,elapsed,exits,logs,result); break
            if attempt==1: final=terminal(t,"error_after_retry",attempt,elapsed,exits,logs,None)
        append(RAW,final)
        if final["measurement_status"]=="timeout": append(TIMEOUTS,final)
        state["completed"][task_key(t)]=final["measurement_status"]; state["current"]=None; atom(STATE,state)
    generate_outputs(state["campaign_start_epoch"])
    state["status"]="complete"; state["completed_utc"]=v4.utc_now(); atom(STATE,state)
    atom(HEARTBEAT,{"status":"complete","timestamp_utc":v4.utc_now()})


if __name__=='__main__': main()
