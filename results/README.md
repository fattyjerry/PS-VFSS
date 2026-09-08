# Evaluation data

## Data sets

- `paper_main.csv`: recovered four-scheme paper workload
- `pvfss_breakdown.csv`: recovered PVFSS component breakdown
- `vereval_scaling/formal`: completed VerEval scaling matrix
- `sender_scalability_logspace_summary.csv`: sender scaling summary
- `sender_signaling_final`: sender single/repeated-signal measurements
- `client_v2`: client benchmark smoke data
- `../experiments/server_online_comparison/results`: server-online pilots

Raw files are retained beside summaries. Failed, invalid, timed-out, or
failed-correctness rows must not enter aggregate comparisons. Blank values mean
unavailable, not zero.

## Reproduction

```bash
# Main workload
python3 scripts/run_unified_bench.py \
  --fmd-gamma 8 --omr-timeout-sec 9000 --pps-timeout-sec 9000

# VerEval scaling
bash scripts/run_vereval_scaling.sh

# Sender scaling
bash results/run_sender_scalability_logspace.sh
```

The formal VerEval matrix covers `|X|={10000,100000,1000000}` and
`threads={1,2,4,8,16}`. All 150 measured rows have `proof_equal=1` and
`outputs_correct=1`.
