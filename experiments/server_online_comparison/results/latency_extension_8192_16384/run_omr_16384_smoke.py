#!/usr/bin/env python3
import json, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
binary = ROOT / "baselines/omr/build-sender-smoke/OMRdemos"
cmd = [str(binary), "--bench", "omrp1", "--threads", "1", "--N", "16384",
       "--kbar", "50", "--poly-modulus-degree", "16384", "--recipient-bench-reps", "1"]
p = subprocess.run(cmd, cwd=ROOT / "baselines/omr", text=True, stdout=subprocess.PIPE,
                   stderr=subprocess.STDOUT, timeout=7200)
(HERE / "logs/omr_degree16384_N16384_smoke.log").write_text(p.stdout)
if p.returncode:
    raise SystemExit(p.returncode)
rows = [json.loads(line[len("[PILOT_JSON]"):].strip()) for line in p.stdout.splitlines()
        if line.startswith("[PILOT_JSON]")]
if not rows:
    raise RuntimeError("missing OMR PILOT_JSON")
(HERE / "omr_degree16384_N16384_smoke.json").write_text(
    json.dumps(rows[-1], indent=2, sort_keys=True) + "\n")
