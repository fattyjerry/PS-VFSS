#!/usr/bin/env python3
import importlib.util
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent / "pilot_fmd_gamma12" / "run_gamma12.py"
spec = importlib.util.spec_from_file_location("fmd_pilot_base", SOURCE)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

runner.HERE = HERE
runner.RAW = HERE / "raw_trials.jsonl"
runner.LOGS = HERE / "logs"
runner.GAMMA = 16

runner.main()

(HERE / "server_time_vs_N_gamma12.csv").replace(HERE / "server_time_vs_N_gamma16.csv")
(HERE / "response_bytes_vs_k_gamma12.csv").replace(HERE / "response_bytes_vs_k_gamma16.csv")
(HERE / "RUN_NOTES.md").write_text(
    "# FMD gamma=16 pilot\n\n"
    "One warmup and three measured process-level trials per unique (N,k) point. "
    "The N=4096,k=50 trials are reused by both summaries. Each response byte count "
    "comes from the constructed response buffer. The artifact currently uses unseeded "
    "crypto/rand; raw trials and min/median/max are retained. No plots were generated.\n"
)
