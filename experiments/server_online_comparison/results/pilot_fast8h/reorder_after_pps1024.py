#!/usr/bin/env python3
"""One-shot supervisor: reload task priority after PPS-GC N=1024 terminates."""
import json, os, signal, subprocess, time
from pathlib import Path

ROOT=Path('/home/zjr/PS-VFSS')
HERE=ROOT/'experiments/server_online_comparison/results/pilot_fast8h'
RAW=HERE/'raw_trials.jsonl'

def pps1024_terminal():
    if not RAW.exists(): return False
    for line in RAW.read_text().splitlines():
        r=json.loads(line)
        if (r.get('experiment_axis')=='server_time_vs_N' and r.get('scheme')=='PPS-GC'
                and r.get('N')==1024 and r.get('trial_type')=='measured'):
            return r.get('measurement_status') in ('ok','timeout','error_after_retry')
    return False

while not pps1024_terminal(): time.sleep(5)
pane=subprocess.check_output(['tmux','list-panes','-t','server-online-fast8h','-F','#{pane_pid}'],text=True).strip()
children=subprocess.run(['pgrep','-P',pane],text=True,capture_output=True).stdout.split()
subprocess.run(['tmux','kill-session','-t','server-online-fast8h'])
for value in children:
    try: os.killpg(int(value),signal.SIGTERM)
    except ProcessLookupError: pass
deadline=time.time()+60
while time.time()<deadline and any(Path('/proc',v).exists() for v in children): time.sleep(1)
for value in children:
    if Path('/proc',value).exists():
        try: os.killpg(int(value),signal.SIGKILL)
        except ProcessLookupError: pass
subprocess.run(['tmux','new-session','-d','-s','server-online-fast8h',
    "cd /home/zjr/PS-VFSS && exec python3 experiments/server_online_comparison/results/pilot_fast8h/run_campaign.py >> experiments/server_online_comparison/results/pilot_fast8h/runner.log 2>&1"],check=True)
(HERE/'reorder_completed.json').write_text(json.dumps({'completed_epoch':time.time(),'trigger':'PPS-GC N=1024 terminal'},indent=2)+'\n')
