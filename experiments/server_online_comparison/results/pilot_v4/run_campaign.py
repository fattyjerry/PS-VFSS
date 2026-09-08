#!/usr/bin/env python3
"""Persistent, resumable data runner for Expanded Pilot V4. No plotting."""

import csv
import hashlib
import json
import os
import re
import signal
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
LOGS = HERE / "logs"
RAW = HERE / "raw_trials.jsonl"
TIMEOUTS = HERE / "timeout_trials.jsonl"
STATE = HERE / "runner_state.json"
MANIFEST = HERE / "workload_manifest.json"
HEARTBEAT = HERE / "heartbeat.json"
SUMMARY = HERE / "summary.csv"
MATLAB = HERE / "matlab_summary.csv"
NOTES = HERE / "RUN_NOTES.md"

NS = [256, 512, 1024, 2048, 4096]
TIMEOUT_SECONDS = 9000
TERM_GRACE_SECONDS = 60
K = 4

BINARIES = {
    "PSVFSS": ROOT / "pvfss/build-paillier3072/src/test",
    "PPS-GC": ROOT / "baselines/pps-gc/pps-garbled-circuits/target/release/examples/unified-bench",
    "FMD": ROOT / "baselines/fmd/fmdbench",
    "OMR": ROOT / "baselines/omr/build-sender-smoke/OMRdemos",
}


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def atomic_json(path, value):
    tmp = path.with_suffix(path.suffix + ".tmp")
    with tmp.open("w") as f:
        json.dump(value, f, indent=2, sort_keys=True)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def append_jsonl(path, value):
    with path.open("a") as f:
        f.write(json.dumps(value, separators=(",", ":")) + "\n")
        f.flush()
        os.fsync(f.fileno())


def task_key(task):
    fields = ["scheme", "N", "k_actual", "trial_type", "trial_index",
              "measurement_mode", "binary_sha256"]
    return "|".join(str(task[x]) for x in fields)


def create_manifest(hashes):
    tasks = []
    for scheme in ("FMD", "PSVFSS", "OMR", "PPS-GC"):
        for n in NS:
            common = {
                "scheme": scheme, "N": n, "k_actual": K,
                "measurement_mode": {
                    "PSVFSS": "component_composed_pilot",
                    "OMR": "reduced_parameter_pilot",
                }.get(scheme, "end_to_end_pilot"),
                "binary_sha256": hashes[scheme],
                "trial_timeout_seconds": TIMEOUT_SECONDS,
            }
            tasks.append({**common, "trial_type": "warmup", "trial_index": 0})
            for idx in range(3):
                tasks.append({**common, "trial_type": "measured", "trial_index": idx})
    return {
        "schema_version": "pilot-v4-manifest-1",
        "created_utc": utc_now(), "N_values": NS, "k_actual": K,
        "trial_timeout_seconds": TIMEOUT_SECONDS,
        "trial_timeout_hours": 2.5, "term_grace_seconds": TERM_GRACE_SECONDS,
        "binary_sha256": hashes, "tasks": tasks,
    }


def load_jsonl(path):
    if not path.exists():
        return []
    rows = []
    for line in path.read_text().splitlines():
        if line.strip():
            rows.append(json.loads(line))
    return rows


def last_stage(path):
    if not path.exists():
        return "launch"
    stage = "launch"
    for line in path.read_text(errors="replace").splitlines():
        if "[BENCH_STAGE]" in line or "[ONLINE_REP]" in line or "[VERIFICATION]" in line:
            stage = line[-500:]
    return stage


def parse_pilot(path):
    matches = re.findall(r"\[PILOT_JSON\]\s+(\{.*\})", path.read_text(errors="replace"))
    return json.loads(matches[-1]) if matches else None


def terminate_processes(procs):
    for proc in procs:
        if proc.poll() is None:
            try:
                os.killpg(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
    deadline = time.monotonic() + TERM_GRACE_SECONDS
    while time.monotonic() < deadline and any(p.poll() is None for p in procs):
        time.sleep(1)
    for proc in procs:
        if proc.poll() is None:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
    for proc in procs:
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            pass


def wait_bounded(procs, task, state):
    start = time.monotonic()
    next_heartbeat = start
    while any(p.poll() is None for p in procs):
        now = time.monotonic()
        if now >= next_heartbeat:
            heartbeat = {
                "timestamp_utc": utc_now(), "task_key": task_key(task),
                "scheme": task["scheme"], "N": task["N"],
                "trial_type": task["trial_type"], "trial_index": task["trial_index"],
                "elapsed_seconds": round(now - start, 3),
            }
            atomic_json(HEARTBEAT, heartbeat)
            state["heartbeat"] = heartbeat
            atomic_json(STATE, state)
            next_heartbeat = now + 60
        if now - start >= TIMEOUT_SECONDS:
            terminate_processes(procs)
            return "timeout", now - start
        time.sleep(2)
    return "exited", time.monotonic() - start


def commands(task, attempt):
    scheme, n = task["scheme"], task["N"]
    stem = f"{scheme.lower().replace('-', '')}_N{n}_{task['trial_type']}{task['trial_index']}_a{attempt}"
    if scheme == "PSVFSS":
        port = 31000 + NS.index(n) * 100 + (0 if task["trial_type"] == "warmup" else 10 + task["trial_index"]) + attempt * 20
        a = LOGS / f"{stem}_alice.log"
        b = LOGS / f"{stem}_bob.log"
        base = [str(BINARIES[scheme])]
        tail = [str(n), str(n), str(task["k_actual"]), "1", "--preprocessing-mode=local-trusted",
                "--measurement-mode=component-pilot"]
        return [(base + ["1", str(port)] + tail, ROOT / "pvfss", a),
                (base + ["2", str(port)] + tail, ROOT / "pvfss", b)]
    log = LOGS / f"{stem}.log"
    if scheme == "FMD":
        cmd = [str(BINARIES[scheme]), "-N", str(n), "-ell", str(task["k_actual"]),
               "-gamma", str(task.get("fmd_gamma", 8)), "-reps", "1"]
        cwd = ROOT / "baselines/fmd"
    elif scheme == "PPS-GC":
        cmd = [str(BINARIES[scheme]), "--N", str(n), "--ell", str(task["k_actual"])]
        cwd = ROOT / "baselines/pps-gc/pps-garbled-circuits"
    else:
        cmd = [str(BINARIES[scheme]), "--bench", "omrp1", "--threads", "1",
               "--N", str(n), "--kbar", str(task["k_actual"]),
               "--poly-modulus-degree", "4096"]
        cwd = ROOT / "baselines/omr"
    return [(cmd, cwd, log)]


def run_attempt(task, attempt, state):
    specs = commands(task, attempt)
    procs, handles = [], []
    try:
        for idx, (cmd, cwd, log) in enumerate(specs):
            handle = log.open("w")
            handles.append(handle)
            proc = subprocess.Popen(cmd, cwd=cwd, stdout=handle, stderr=subprocess.STDOUT,
                                    start_new_session=True)
            procs.append(proc)
            if len(specs) == 2 and idx == 0:
                time.sleep(0.2)
        status, elapsed = wait_bounded(procs, task, state)
    finally:
        for handle in handles:
            handle.flush()
            os.fsync(handle.fileno())
            handle.close()
    exit_codes = [p.returncode for p in procs]
    logs = [str(spec[2].relative_to(HERE)) for spec in specs]
    if status == "timeout":
        return "timeout", elapsed, exit_codes, logs, None
    parsed = [parse_pilot(spec[2]) for spec in specs]
    if all(code == 0 for code in exit_codes) and all(item is not None for item in parsed):
        if task["scheme"] == "PSVFSS":
            a, b = parsed
            result = dict(a)
            result["admission_batch_online_ms"] = max(a["admission_batch_online_ms"], b["admission_batch_online_ms"])
            result["admission_per_signal_ms"] = result["admission_batch_online_ms"] / task["N"]
            result["retrieval_online_ms"] = max(a["retrieval_online_ms"], b["retrieval_online_ms"])
            result["critical_path_wall_ms"] = result["retrieval_online_ms"]
            result["correctness_ok"] = False
            result["correctness_status"] = "failed"
        else:
            result = parsed[0]
            result["admission_batch_online_ms"] = result.pop("admission_online_ms", None)
            result["admission_per_signal_ms"] = (result["admission_batch_online_ms"] / task["N"]
                                                  if result["admission_batch_online_ms"] is not None else None)
            result["correctness_status"] = "failed" if task["scheme"] == "OMR" else "not_recorded"
            result["correctness_ok"] = False if task["scheme"] == "OMR" else None
        return "ok", elapsed, exit_codes, logs, result
    return "error", elapsed, exit_codes, logs, None


def terminal_row(task, status, attempt, elapsed, exit_codes, logs, result=None):
    row = {
        "result_tier": "pilot_v4", **task, "is_warmup": task["trial_type"] == "warmup",
        "terminal_status": status, "measurement_status": status,
        "started_or_completed_utc": utc_now(), "elapsed_process_wall_seconds": round(elapsed, 3),
        "exit_codes": exit_codes, "logs": logs,
        "timeout_seconds": TIMEOUT_SECONDS if status == "timeout" else None,
        "latency_lower_bound_ms": TIMEOUT_SECONDS * 1000 if status == "timeout" else None,
        "measured_latency_ms": None if status != "ok" else result.get("retrieval_online_ms"),
        "last_stage": last_stage(HERE / logs[0]), "attempt": attempt,
    }
    if result:
        row.update(result)
        row["measurement_status"] = "ok"
    return row


def run_response_only(n, binary_hash, state):
    task = {
        "scheme": "PPS-GC", "N": n, "k_actual": K, "trial_type": "response_only",
        "trial_index": 0, "measurement_mode": "response_only_component_pilot",
        "binary_sha256": binary_hash,
    }
    key = task_key(task)
    if key in state["completed"]:
        return
    log = LOGS / f"ppsgc_N{n}_response_only.log"
    with log.open("w") as out:
        proc = subprocess.run([str(BINARIES["PPS-GC"]), "--N", str(n), "--ell", "4",
                               "--response-only-component-pilot"], cwd=BINARIES["PPS-GC"].parents[3],
                              stdout=out, stderr=subprocess.STDOUT, timeout=300)
    result = parse_pilot(log)
    status = "ok" if proc.returncode == 0 and result else "error_after_retry"
    row = terminal_row(task, status, 0, 0, [proc.returncode], [str(log.relative_to(HERE))], result)
    append_jsonl(RAW, row)
    state["completed"][key] = status
    atomic_json(STATE, state)


def mark_conditional_skips(manifest, n, state):
    for task in manifest["tasks"]:
        if task["scheme"] == "PPS-GC" and task["N"] == n and task["trial_type"] == "measured":
            key = task_key(task)
            if key in state["completed"]:
                continue
            row = terminal_row(task, "unsupported", 0, 0, [], [], None)
            row["reason"] = "warmup_timeout_per_campaign_policy"
            append_jsonl(RAW, row)
            state["completed"][key] = "unsupported"
    atomic_json(STATE, state)


def numeric(values):
    return [v for v in values if isinstance(v, (int, float))]


def stats(values):
    values = numeric(values)
    return (statistics.median(values), min(values), max(values)) if values else ("", "", "")


MATLAB_HEADER = [
    "scheme", "N", "N_physical", "k_actual", "trial_count",
    "admission_batch_median_ms", "admission_batch_min_ms", "admission_batch_max_ms",
    "admission_per_signal_median_ms", "admission_per_signal_min_ms", "admission_per_signal_max_ms",
    "retrieval_median_ms", "retrieval_min_ms", "retrieval_max_ms", "retrieval_below_resolution",
    "response_median_bytes", "response_min_bytes", "response_max_bytes", "measurement_status",
    "timeout_seconds", "latency_lower_bound_ms", "measurement_quality", "correctness_status",
]


def summarize_group(scheme, n, rows, n_physical=None):
    measured = [r for r in rows if r.get("scheme") == scheme and int(r.get("N_logical", r.get("N", -1))) == n
                and not r.get("is_warmup", False) and r.get("trial_type", "measured") == "measured"
                and r.get("measurement_status") == "ok"]
    timeouts = [r for r in rows if r.get("scheme") == scheme and int(r.get("N", -1)) == n
                and r.get("measurement_status") == "timeout"]
    response_only = [r for r in rows if r.get("scheme") == scheme and int(r.get("N", -1)) == n
                     and r.get("trial_type") == "response_only" and r.get("measurement_status") == "ok"]
    adm = stats([r.get("admission_batch_online_ms") for r in measured])
    per = stats([r.get("admission_per_signal_ms") for r in measured])
    ret = stats([r.get("retrieval_online_ms") for r in measured])
    response_source = measured if measured else response_only
    resp = stats([r.get("server_to_recipient_bytes") for r in response_source])
    status = "ok" if measured else "timeout" if timeouts else "error_after_retry"
    quality = (measured[0].get("measurement_quality", measured[0].get("measurement_mode")) if measured
               else "response_only_component_pilot" if response_only else "artifact_limited")
    correctness = measured[0].get("correctness_status", "not_recorded") if measured else "not_measured"
    return [scheme, n, n_physical or n, K, len(measured), *adm, *per, *ret,
            "true" if ret[0] == 0 else "false", *resp, status,
            TIMEOUT_SECONDS if status == "timeout" else "",
            TIMEOUT_SECONDS * 1000 if status == "timeout" else "", quality, correctness]


def generate_summaries(campaign_start):
    v4 = load_jsonl(RAW)
    rows = [summarize_group(s, n, v4, 4096 if s == "OMR" else n)
            for s in ("PSVFSS", "PPS-GC", "FMD", "OMR") for n in NS]
    with SUMMARY.open("w", newline="") as f:
        writer = csv.writer(f); writer.writerow(MATLAB_HEADER); writer.writerows(rows)

    v3_summary = list(csv.DictReader((HERE.parent / "pilot_v3" / "summary.csv").open()))
    combined = []
    for r in v3_summary:
        n = int(r["N_logical"])
        adm = float(r["admission_batch_online_ms"]) if r["admission_batch_online_ms"] else ""
        per = float(r["admission_per_signal_ms"]) if r["admission_per_signal_ms"] else ""
        ret = float(r["retrieval_online_ms"])
        resp = float(r["server_to_recipient_bytes"])
        combined.append([r["scheme"], n, int(r["N_physical"]), 4, int(r["trial_count"]),
                         adm, adm, adm, per, per, per, ret, ret, ret,
                         "true" if ret == 0 else "false", resp, resp, resp, "ok", "", "",
                         r["measurement_quality"], r["correctness_status"]])
    combined.extend(rows)
    combined.sort(key=lambda r: (r[0], int(r[1])))
    with MATLAB.open("w", newline="") as f:
        writer = csv.writer(f); writer.writerow(MATLAB_HEADER); writer.writerows(combined)

    elapsed = time.time() - campaign_start
    NOTES.write_text(
        "# Expanded Pilot V4 long run\n\n"
        "Data only; no plotting files or plotting code were generated. Each child trial used "
        f"a {TIMEOUT_SECONDS}-second limit, TERM then a {TERM_GRACE_SECONDS}-second grace before KILL.\n\n"
        f"Campaign wall time: {elapsed:.3f} seconds. FMD/PPS-GC use end-to-end pilot paths; "
        "PSVFSS is component-composed with local trusted preprocessing; OMR uses BFV degree/slots "
        "4096 with sec_level_type::none. Correctness labels are preserved exactly.\n"
    )


def main():
    LOGS.mkdir(parents=True, exist_ok=True)
    hashes = {scheme: sha256(path) for scheme, path in BINARIES.items()}
    if MANIFEST.exists():
        manifest = json.loads(MANIFEST.read_text())
        if manifest["binary_sha256"] != hashes:
            manifest = create_manifest(hashes)
            atomic_json(MANIFEST, manifest)
    else:
        manifest = create_manifest(hashes)
        atomic_json(MANIFEST, manifest)

    if STATE.exists():
        state = json.loads(STATE.read_text())
    else:
        state = {"status": "running", "created_utc": utc_now(), "completed": {}, "current": None}
        atomic_json(STATE, state)
    campaign_start = state.get("campaign_start_epoch", time.time())
    state["campaign_start_epoch"] = campaign_start
    state["status"] = "running"
    atomic_json(STATE, state)

    for task in manifest["tasks"]:
        key = task_key(task)
        if key in state["completed"]:
            continue
        if (task["scheme"] == "PPS-GC" and task["N"] in (2048, 4096)
                and task["trial_type"] == "measured"):
            warm = dict(task); warm.update(trial_type="warmup", trial_index=0)
            if state["completed"].get(task_key(warm)) == "timeout":
                mark_conditional_skips(manifest, task["N"], state)
                continue
        state["current"] = task
        atomic_json(STATE, state)
        final_row = None
        for attempt in (0, 1):
            status, elapsed, exits, logs, result = run_attempt(task, attempt, state)
            if status == "timeout":
                final_row = terminal_row(task, "timeout", attempt, elapsed, exits, logs)
                break
            if status == "ok":
                final_row = terminal_row(task, "ok", attempt, elapsed, exits, logs, result)
                break
            if attempt == 1:
                final_row = terminal_row(task, "error_after_retry", attempt, elapsed, exits, logs)
        append_jsonl(RAW, final_row)
        if final_row["measurement_status"] == "timeout":
            append_jsonl(TIMEOUTS, final_row)
        state["completed"][key] = final_row["measurement_status"]
        state["current"] = None
        atomic_json(STATE, state)

        if task["scheme"] == "PPS-GC" and final_row["measurement_status"] == "timeout":
            run_response_only(task["N"], hashes["PPS-GC"], state)
            if task["trial_type"] == "warmup" and task["N"] in (2048, 4096):
                mark_conditional_skips(manifest, task["N"], state)

    generate_summaries(campaign_start)
    state["status"] = "complete"
    state["completed_utc"] = utc_now()
    state["current"] = None
    atomic_json(STATE, state)
    atomic_json(HEARTBEAT, {"timestamp_utc": utc_now(), "status": "complete"})
    return 0


if __name__ == "__main__":
    sys.exit(main())
