#!/usr/bin/env python3
import csv, hashlib, json, os, statistics, subprocess, time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
LOGS = HERE / "logs"
RAW = HERE / "raw_trials.jsonl"
STATE = HERE / "runner_state.json"
HEARTBEAT = HERE / "heartbeat.json"
OUTPUT = ROOT / "results/omr_scalability_test.csv"
NOTES = HERE / "RUN_NOTES.md"
BINARY = ROOT / "baselines/omr/build-sender-smoke/OMRdemos"
NS = [256, 512, 1024, 2048, 4096, 8192, 16384]
DEGREE = 16384
K = 50
TIMEOUT = 7200


def now(): return datetime.now(timezone.utc).isoformat()


def sha(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda:f.read(1<<20),b""): h.update(b)
    return h.hexdigest()


def atomic(path, value):
    tmp=path.with_suffix(path.suffix+".tmp")
    tmp.write_text(json.dumps(value,indent=2,sort_keys=True)+"\n")
    os.replace(tmp,path)


def key(t): return f"{t['N']}:{t['trial_type']}:{t['trial_index']}:{t['binary_sha256']}"


def append(row):
    with RAW.open("a") as f:
        f.write(json.dumps(row,sort_keys=True)+"\n"); f.flush(); os.fsync(f.fileno())


def parse(path):
    vals=[]
    for line in path.read_text(errors="replace").splitlines():
        if line.startswith("[OMR_SCALABILITY_JSON]"):
            vals.append(json.loads(line[len("[OMR_SCALABILITY_JSON]"):].strip()))
    if not vals: raise RuntimeError("missing OMR_SCALABILITY_JSON")
    return vals[-1]


def run(t,state):
    name=f"N{t['N']}_{t['trial_type']}{t['trial_index']}.log"
    log=LOGS/name
    cmd=[str(BINARY),"--bench","omrp1","--threads","1","--N",str(t["N"]),
         "--kbar",str(K),"--poly-modulus-degree",str(DEGREE),"--recipient-bench-reps","1"]
    with log.open("w") as f:
        p=subprocess.Popen(cmd,cwd=ROOT/"baselines/omr",stdout=f,stderr=subprocess.STDOUT,
                           start_new_session=True)
        start=time.monotonic(); next_beat=start
        while p.poll() is None:
            elapsed=time.monotonic()-start
            if time.monotonic()>=next_beat:
                hb={"timestamp_utc":now(),"task_key":key(t),"N":t["N"],
                    "trial_type":t["trial_type"],"trial_index":t["trial_index"],
                    "elapsed_seconds":round(elapsed,3)}
                atomic(HEARTBEAT,hb); state["heartbeat"]=hb; atomic(STATE,state)
                next_beat=time.monotonic()+60
            if elapsed>=TIMEOUT:
                os.killpg(p.pid,15); time.sleep(10)
                if p.poll() is None: os.killpg(p.pid,9)
                p.wait()
                return {**t,"measurement_status":"timeout","timeout_seconds":TIMEOUT,
                        "completed_utc":now(),"log":str(log.relative_to(HERE))}
            time.sleep(2)
    if p.returncode!=0:
        return {**t,"measurement_status":"error","exit_code":p.returncode,
                "completed_utc":now(),"log":str(log.relative_to(HERE))}
    result=parse(log)
    return {**t,**result,"measurement_status":"ok","completed_utc":now(),
            "log":str(log.relative_to(HERE))}


def summarize(rows,binary_hash):
    measured=[r for r in rows if r["trial_type"]=="measured" and r["measurement_status"]=="ok"]
    fields=["N","poly_modulus_degree","slot_count","ciphertext_size","retrieval_ms",
            "load_ms","eval_ms","decrypt_ms","decode_ms","trial_count",
            "retrieval_min_ms","retrieval_max_ms","response_serialization_ms",
            "recipient_total_ms","measurement_status","correctness_status","binary_sha256"]
    OUTPUT.parent.mkdir(parents=True,exist_ok=True)
    with OUTPUT.open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=fields); w.writeheader()
        for n in NS:
            g=[r for r in measured if r["N"]==n]
            if len(g)!=3:
                w.writerow({"N":n,"poly_modulus_degree":DEGREE,"slot_count":DEGREE,
                            "trial_count":len(g),"measurement_status":"incomplete",
                            "binary_sha256":binary_hash})
                continue
            med=lambda x:statistics.median(float(r[x]) for r in g)
            w.writerow({"N":n,"poly_modulus_degree":DEGREE,"slot_count":DEGREE,
                "ciphertext_size":int(statistics.median(int(r["ciphertext_size"]) for r in g)),
                "retrieval_ms":med("retrieval_ms"),"load_ms":med("load_ms"),
                "eval_ms":med("eval_ms"),"decrypt_ms":med("decrypt_ms"),
                "decode_ms":med("decode_ms"),"trial_count":3,
                "retrieval_min_ms":min(float(r["retrieval_ms"]) for r in g),
                "retrieval_max_ms":max(float(r["retrieval_ms"]) for r in g),
                "response_serialization_ms":med("response_serialization_ms"),
                "recipient_total_ms":med("load_ms")+med("decrypt_ms")+med("decode_ms"),
                "measurement_status":"ok",
                "correctness_status":"passed" if all(r["correctness"] for r in g) else "failed",
                "binary_sha256":binary_hash})


def main():
    HERE.mkdir(parents=True,exist_ok=True); LOGS.mkdir(exist_ok=True)
    binary_hash=sha(BINARY)
    rows=[json.loads(x) for x in RAW.read_text().splitlines()] if RAW.exists() else []
    if any(r["binary_sha256"]!=binary_hash for r in rows):
        raise RuntimeError("binary hash changed; refusing to reuse trials")
    done={key(r) for r in rows}
    tasks=[]
    for n in NS:
        tasks.append({"N":n,"trial_type":"warmup","trial_index":0,"binary_sha256":binary_hash})
        tasks += [{"N":n,"trial_type":"measured","trial_index":i,"binary_sha256":binary_hash} for i in range(3)]
    state={"status":"running","started_or_resumed_utc":now(),"binary_sha256":binary_hash,
           "completed":len(done),"total":len(tasks)}; atomic(STATE,state)
    for t in tasks:
        if key(t) in done: continue
        row=run(t,state); append(row); rows.append(row); done.add(key(t))
        state.update(completed=len(done),last_terminal=row); atomic(STATE,state)
    summarize(rows,binary_hash)
    state.update(status="complete",completed=len(done),completed_utc=now()); atomic(STATE,state)
    NOTES.write_text(
        "# OMR fixed-parameter scalability campaign\n\n"
        f"- Binary SHA-256: `{binary_hash}`\n"
        "- Fixed HE configuration: poly_modulus_degree=slot_count=16384, plain modulus=65537, custom 789-bit coefficient modulus chain, sec_level_type::none.\n"
        "- Workloads: N={256,512,1024,2048,4096,8192,16384}, kbar=50.\n"
        "- One warmup and three measured trials per N.\n"
        "- retrieval_ms is server eval plus response serialization; file loading, setup, key generation, client load/decrypt/decode, and correctness checks are excluded.\n"
        "- load_ms/decrypt_ms/decode_ms are recipient-side response-consumption components and are not included in server retrieval_ms.\n"
        "- The implementation pads every benchmark workload to the full BatchEncoder slot count.\n")

if __name__=="__main__": main()
