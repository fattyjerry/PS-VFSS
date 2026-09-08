#!/usr/bin/env python3
import csv
import math
import statistics
from pathlib import Path

ROOT = Path("/home/zjr/PS-VFSS")
RUN_DIR = ROOT / "results/sender_scalability_logspace_run"
RAW_OUT = ROOT / "results/sender_scalability_logspace_raw.csv"
SUMMARY_OUT = ROOT / "results/sender_scalability_logspace_summary.csv"
B_VALUES = [10, 32, 100, 316, 1000]
SCHEMES = ["PSVFSS", "PPS-GC", "FMD", "OMR"]
SOURCE_NAMES = {
    "PSVFSS": "psvfss_raw.csv",
    "PPS-GC": "ppsgc_raw.csv",
    "FMD": "fmd_raw.csv",
    "OMR": "omr_raw.csv",
}


def read_measured(scheme):
    path = RUN_DIR / SOURCE_NAMES[scheme]
    rows = []
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row["warmup"].lower() == "true":
                continue
            rows.append({
                "scheme": scheme,
                "B": int(row["B"]),
                "trial": int(row["trial"]),
                "time_ns": int(row["total_time_ns"]),
                "status": row["status"],
            })
    return rows


def linear_fit(points):
    xs = [float(x) for x, _ in points]
    ys = [float(y) for _, y in points]
    xbar = statistics.mean(xs)
    ybar = statistics.mean(ys)
    denom = sum((x - xbar) ** 2 for x in xs)
    slope = sum((x - xbar) * (y - ybar) for x, y in zip(xs, ys)) / denom
    intercept = ybar - slope * xbar
    ss_res = sum((y - (slope * x + intercept)) ** 2 for x, y in zip(xs, ys))
    ss_tot = sum((y - ybar) ** 2 for y in ys)
    r_squared = 1.0 - ss_res / ss_tot
    return slope, intercept, r_squared


def main():
    all_rows = []
    for scheme in SCHEMES:
        all_rows.extend(read_measured(scheme))

    expected_pairs = {(scheme, b) for scheme in SCHEMES for b in B_VALUES}
    actual_pairs = {(row["scheme"], row["B"]) for row in all_rows}
    if actual_pairs != expected_pairs:
        raise RuntimeError(f"incomplete scheme/B grid: missing={sorted(expected_pairs-actual_pairs)}")
    for scheme, b in sorted(expected_pairs):
        rows = [row for row in all_rows if row["scheme"] == scheme and row["B"] == b]
        if len(rows) != 100:
            raise RuntimeError(f"{scheme} B={b}: expected 100 measured trials, got {len(rows)}")
        if sorted(row["trial"] for row in rows) != list(range(100)):
            raise RuntimeError(f"{scheme} B={b}: trial indices are incomplete or duplicated")
        if any(row["status"] != "ok" for row in rows):
            raise RuntimeError(f"{scheme} B={b}: non-ok source trial")
        if any(row["time_ns"] <= 0 or not math.isfinite(row["time_ns"]) for row in rows):
            raise RuntimeError(f"{scheme} B={b}: invalid timing value")

    with RAW_OUT.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=["scheme", "B", "trial", "time_ns"])
        writer.writeheader()
        writer.writerows({key: row[key] for key in writer.fieldnames} for row in all_rows)

    summary_rows = []
    fits = {}
    for scheme in SCHEMES:
        fit_points = []
        for b in B_VALUES:
            values = [row["time_ns"] for row in all_rows if row["scheme"] == scheme and row["B"] == b]
            mean = statistics.mean(values)
            median = statistics.median(values)
            std = statistics.stdev(values)
            half_width = 1.96 * std / math.sqrt(len(values))
            fit_points.append((b, mean))
            summary_rows.append({
                "scheme": scheme,
                "B": b,
                "trials": len(values),
                "mean_ns": f"{mean:.9f}",
                "median_ns": f"{median:.9f}",
                "std_ns": f"{std:.9f}",
                "ci95_low_ns": f"{mean-half_width:.9f}",
                "ci95_high_ns": f"{mean+half_width:.9f}",
                "mean_ms": f"{mean/1e6:.9f}",
                "median_ms": f"{median/1e6:.9f}",
                "ci95_low_ms": f"{(mean-half_width)/1e6:.9f}",
                "ci95_high_ms": f"{(mean+half_width)/1e6:.9f}",
            })
        fits[scheme] = linear_fit(fit_points)

    fields = ["scheme", "B", "trials", "mean_ns", "median_ns", "std_ns",
              "ci95_low_ns", "ci95_high_ns", "mean_ms", "median_ms",
              "ci95_low_ms", "ci95_high_ms"]
    with SUMMARY_OUT.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(summary_rows)

    print(f"wrote {len(all_rows)} measured trials to {RAW_OUT}")
    print(f"wrote {len(summary_rows)} summary rows to {SUMMARY_OUT}")
    for scheme in SCHEMES:
        slope, intercept, r_squared = fits[scheme]
        print(f"FIT {scheme}: slope_ns_per_signal={slope:.9f} "
              f"intercept_ns={intercept:.9f} r_squared={r_squared:.12f}")


if __name__ == "__main__":
    main()
