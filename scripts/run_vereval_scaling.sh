#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/.." && pwd)"
bench_dir="$repo_root/vdpf/src"
output_dir="${1:-$repo_root/results/vereval_scaling/reproduced}"

bits="${VEREVAL_BITS:-50}"
repetitions="${VEREVAL_REPETITIONS:-10}"
warmups="${VEREVAL_WARMUPS:-2}"
read -r -a user_sizes <<< "${VEREVAL_USERS:-10000 100000 1000000}"
read -r -a thread_counts <<< "${VEREVAL_THREADS:-1 2 4 8 16}"

mkdir -p -- "$output_dir"
make -C "$bench_dir" bench_vereval

{
    date -R
    git -C "$repo_root" rev-parse HEAD
    uname -a
    nproc
    lscpu
    free -h
    gcc --version
    openssl version
    printf 'bits=%s\n' "$bits"
    printf 'repetitions=%s\n' "$repetitions"
    printf 'warmups=%s\n' "$warmups"
    printf 'users=%s\n' "${user_sizes[*]}"
    printf 'threads=%s\n' "${thread_counts[*]}"
} > "$output_dir/environment.txt"

for users in "${user_sizes[@]}"; do
    for threads in "${thread_counts[@]}"; do
        csv_file="$output_dir/n${users}_t${threads}.csv"
        log_file="$output_dir/n${users}_t${threads}.log"

        command=(
            "$bench_dir/bench_vereval"
            --users "$users"
            --bits "$bits"
            --repetitions "$repetitions"
            --warmups "$warmups"
            --threads "$threads"
        )
        if (( threads > 1 )); then
            command+=(--compare-serial)
        fi

        "${command[@]}" > "$csv_file" 2> "$log_file"

        awk -F, -v expected="$repetitions" '
            NR == 1 { next }
            {
                rows++
                if ($14 != 1 || $15 != 1)
                    failures++
            }
            END {
                if (rows != expected || failures != 0) {
                    printf "invalid VerEval result: rows=%d expected=%d failures=%d\n", rows, expected, failures > "/dev/stderr"
                    exit 1
                }
            }
        ' "$csv_file"

        printf 'completed users=%s threads=%s\n' "$users" "$threads"
    done
done

printf 'VerEval results written to %s\n' "$output_dir"
