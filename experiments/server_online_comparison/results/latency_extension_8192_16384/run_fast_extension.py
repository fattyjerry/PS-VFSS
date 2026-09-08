#!/usr/bin/env python3
import csv, hashlib, json, os, statistics, subprocess, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
LOGS = HERE / "logs"
RAW = HERE / "raw_trials.jsonl"
SUMMARY = HERE / "psvfss_fmd_extension.csv"
NS = [8192, 16384]
K = 50
REPS = 3
PS = ROOT / "pvfss/build-paillier3072/src/test"
FMD = ROOT / "baselines/fmd/fmdbench"


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""): h.update(b)
    return h.hexdigest()


def tagged(text, tag):
    vals = [json.loads(x[len(tag):].strip()) for x in text.splitlines() if x.startswith(tag)]
    if not vals: raise RuntimeError(f"missing {tag}")
    return vals[-1]


def save(row):
    with RAW.open("a", encoding="utf-8") as f:
        f.write(json.dumps(row, sort_keys=True) + "\n"); f.flush(); os.fsync(f.fileno())


def ps_pair(n, kind, trial, port, binary_hash):
    tail = [str(n), str(n), str(K), "1", "--preprocessing-mode=local-trusted",
            "--measurement-mode=component-pilot"]
    names = [f"ps_N{n}_{kind}{trial}_alice.log", f"ps_N{n}_{kind}{trial}_bob.log"]
    for attempt in range(2):
        handles = [(LOGS / name).open("w") for name in names]
        procs = []
        try:
            for party in [1, 2]:
                procs.append(subprocess.Popen([str(PS), str(party), str(port + attempt * 1000)] + tail,
                    cwd=ROOT / "pvfss", stdout=handles[party-1], stderr=subprocess.STDOUT,
                    start_new_session=True))
                if party == 1: time.sleep(.2)
            deadline = time.monotonic() + 180
            while any(p.poll() is None for p in procs):
                if time.monotonic() >= deadline:
                    for p in procs:
                        if p.poll() is None: os.killpg(p.pid, 15)
                    time.sleep(1)
                    for p in procs:
                        if p.poll() is None: os.killpg(p.pid, 9)
                    raise RuntimeError("phase timeout")
                time.sleep(.1)
        except RuntimeError:
            if attempt == 1: raise
            continue
        finally:
            for h in handles: h.flush(); os.fsync(h.fileno()); h.close()
        if [p.wait() for p in procs] != [0, 0]:
            if attempt == 1: raise RuntimeError("PSVFSS party failure")
            continue
        outs = [(LOGS / name).read_text() for name in names]
        pilots = [tagged(x, "[PILOT_JSON]") for x in outs]
        critical = max(pilots, key=lambda x: x["retrieval_online_ms"])
        admission = max(x["admission_batch_online_ms"] for x in pilots)
        return {
            "scheme":"PSVFSS", "N":n, "k_actual":K, "trial_type":kind,
            "trial_index":trial, "admission_batch_online_ms":admission,
            "retrieval_online_ms":critical["retrieval_online_ms"],
            "server_online_total_ms":admission + critical["retrieval_online_ms"],
            "binary_sha256":binary_hash, "measurement_quality":"component_composed_pilot",
            "correctness_status":"failed", "preprocessing_mode":"local_trusted_pilot",
        }
    raise RuntimeError("unreachable")


def fmd_run(n, kind, trial, binary_hash):
    name = f"fmd_N{n}_{kind}{trial}.log"
    p = subprocess.run([str(FMD), "-N", str(n), "-ell", str(K), "-gamma", "16", "-reps", "1"],
        cwd=ROOT / "baselines/fmd", text=True, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=300)
    (LOGS / name).write_text(p.stdout)
    if p.returncode: raise RuntimeError(f"FMD failed rc={p.returncode}")
    x = tagged(p.stdout, "[PILOT_JSON]")
    return {"scheme":"FMD", "N":n, "k_actual":K, "trial_type":kind,
        "trial_index":trial, "admission_batch_online_ms":None,
        "retrieval_online_ms":x["retrieval_online_ms"],
        "server_online_total_ms":x["retrieval_online_ms"],
        "server_to_recipient_bytes":x["server_to_recipient_bytes"],
        "candidate_count":x["candidate_count"], "false_positive_count":x["false_positive_count"],
        "fmd_gamma":16, "binary_sha256":binary_hash,
        "measurement_quality":"end_to_end_pilot", "correctness_status":"passed"}


def main():
    HERE.mkdir(parents=True, exist_ok=True); LOGS.mkdir(exist_ok=True)
    if RAW.exists() or SUMMARY.exists(): raise RuntimeError("refusing to overwrite campaign")
    hashes = {"PSVFSS":sha(PS), "FMD":sha(FMD)}
    rows=[]
    for si, scheme in enumerate(["PSVFSS", "FMD"]):
        for ni,n in enumerate(NS):
            tasks=[("warmup",0)]+[("measured",i) for i in range(REPS)]
            for kind,trial in tasks:
                row = (ps_pair(n,kind,trial,36100+ni*20+trial+si*100,hashes[scheme])
                       if scheme=="PSVFSS" else fmd_run(n,kind,trial,hashes[scheme]))
                save(row); rows.append(row)
    fields=["scheme","N","k_actual","trial_count","admission_batch_median_ms",
            "retrieval_median_ms","retrieval_min_ms","retrieval_max_ms",
            "server_online_total_median_ms","response_median_bytes","candidate_median",
            "false_positive_median","measurement_quality","correctness_status"]
    with SUMMARY.open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=fields); w.writeheader()
        for scheme in ["PSVFSS","FMD"]:
            for n in NS:
                g=[r for r in rows if r["scheme"]==scheme and r["N"]==n and r["trial_type"]=="measured"]
                med=lambda key: statistics.median(r[key] for r in g)
                w.writerow({"scheme":scheme,"N":n,"k_actual":K,"trial_count":len(g),
                    "admission_batch_median_ms":"" if scheme=="FMD" else med("admission_batch_online_ms"),
                    "retrieval_median_ms":med("retrieval_online_ms"),
                    "retrieval_min_ms":min(r["retrieval_online_ms"] for r in g),
                    "retrieval_max_ms":max(r["retrieval_online_ms"] for r in g),
                    "server_online_total_median_ms":med("server_online_total_ms"),
                    "response_median_bytes":"" if scheme=="PSVFSS" else med("server_to_recipient_bytes"),
                    "candidate_median":"" if scheme=="PSVFSS" else med("candidate_count"),
                    "false_positive_median":"" if scheme=="PSVFSS" else med("false_positive_count"),
                    "measurement_quality":g[0]["measurement_quality"],
                    "correctness_status":g[0]["correctness_status"]})

if __name__ == "__main__": main()
