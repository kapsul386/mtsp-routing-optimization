"""
Parameter-based ablation of v21_minsum on 3 flagship instances.

Variants (using existing CLI flags, no C++ rebuild):
  - default                : full v21 (baseline)
  - no_region_reopt        : --region-reopt-every 0 (disable route-pair reopt phase)
  - no_region_granular     : --granular-every 99999 (disable region-granular ops)
  - no_classic_seeds       : --classic-seeds 0 (disable classic seed sources)
  - no_rebalance           : --rebalance-empty-routes 0 (already default; sanity)

Instances: 3 flagship × 3 seeds × 5 variants = 45 runs.
Per-instance budget matches the variance audit (60s / 180s / 350s).

Output: experiments/review_fixes/ablation_results.csv
"""
from __future__ import annotations
import csv
import json
import math
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MTSP_EXE = ROOT / "build" / "src" / "Release" / "mtsp.exe"
INSTANCES_DIR = ROOT / "data" / "mtsp" / "generated_multifamily"
OUT_PATH = ROOT / "experiments" / "review_fixes" / "ablation_results.csv"

INSTANCES = [
    ("uniform_n10000_m5_r01.txt", 60_000),
    ("uniform_n50000_m5_r01.txt", 180_000),
    ("uniform_n100000_m5_r01.txt", 350_000),
]

VARIANTS = [
    ("default",                {}),
    ("no_region_reopt",        {"region-reopt-every": "0"}),
    ("no_region_granular",     {"granular-every": "99999"}),
    ("no_classic_seeds",       {"classic-seeds": "0"}),
    ("no_rebalance",           {"rebalance-empty-routes": "0"}),
]

SEEDS = [101, 102, 103]


def euclidean(p, q):
    return math.hypot(p[0] - q[0], p[1] - q[1])


def gini(values):
    n = len(values)
    if n == 0:
        return 0.0
    mean = sum(values) / n
    if mean <= 0:
        return 0.0
    s = 0.0
    for x in values:
        for y in values:
            s += abs(x - y)
    return s / (2 * n * n * mean)


def load_instance(path):
    with open(path) as f:
        first = f.readline().split()
        n, m = int(first[0]), int(first[1])
        coords = [tuple(map(float, f.readline().split())) for _ in range(n)]
    return coords, n, m


def compute_metrics(coords, routes):
    if not routes:
        return None
    lens = []
    for r in routes:
        if not r:
            lens.append(0.0)
            continue
        L = 0.0
        for i in range(len(r) - 1):
            L += euclidean(coords[r[i]], coords[r[i + 1]])
        lens.append(L)
    return {
        "sum": sum(lens),
        "makespan": max(lens),
        "min_route": min(lens),
        "gini": gini(lens),
        "balance_ratio": max(lens) / (sum(lens) / len(lens)) if sum(lens) > 0 else 0,
    }


def run_one(inst_filename, time_budget_ms, variant_name, opts, seed):
    inst_path = INSTANCES_DIR / inst_filename
    cmd = [
        str(MTSP_EXE),
        "--input-file", str(inst_path),
        "--step", "lkh_v21_minsum",
        "--time-budget-ms", str(time_budget_ms),
        "--seed", str(seed),
    ]
    for k, v in opts.items():
        cmd.extend([f"--{k}", str(v)])

    t0 = time.time()
    try:
        proc = subprocess.run(
            cmd, capture_output=True, text=True,
            timeout=max(time_budget_ms // 1000 + 60, 120),
        )
    except subprocess.TimeoutExpired:
        return {"timed_out": True}

    wall = time.time() - t0
    if proc.returncode != 0:
        return {"error": f"rc={proc.returncode}", "stderr_tail": proc.stderr[-200:] if proc.stderr else ""}

    try:
        d = json.loads(proc.stdout)
    except Exception:
        return {"error": "json_parse_failed", "stdout_tail": proc.stdout[-200:]}

    coords, n, m = load_instance(inst_path)
    metrics = compute_metrics(coords, d.get("routes"))
    if metrics is None:
        return {"error": "no_routes"}

    return {
        "valid": d.get("valid"),
        "objective": d.get("objective"),
        "wall_seconds": wall,
        "solver_time_seconds": d.get("time"),
        **metrics,
    }


def main():
    fields = [
        "instance", "n", "m", "variant", "seed", "valid",
        "objective", "sum", "makespan", "min_route", "gini",
        "balance_ratio", "wall_seconds", "solver_time_seconds",
        "timed_out", "error",
    ]

    done = set()
    if OUT_PATH.exists():
        with open(OUT_PATH) as f:
            for r in csv.DictReader(f):
                done.add((r["instance"], r["variant"], int(r["seed"])))

    write_header = not OUT_PATH.exists()
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)

    total = len(INSTANCES) * len(VARIANTS) * len(SEEDS)
    print(f"Will run {total} runs ({len(INSTANCES)} instances × {len(VARIANTS)} variants × {len(SEEDS)} seeds)")
    sys.stdout.flush()

    idx = 0
    with open(OUT_PATH, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        if write_header:
            w.writeheader()

        for inst_filename, time_budget_ms in INSTANCES:
            inst_path = INSTANCES_DIR / inst_filename
            if not inst_path.exists():
                continue
            with open(inst_path) as fh:
                first = fh.readline().split()
                n, m = int(first[0]), int(first[1])

            for variant_name, opts in VARIANTS:
                for seed in SEEDS:
                    idx += 1
                    if (inst_filename, variant_name, seed) in done:
                        print(f"[{idx}/{total}] SKIP {inst_filename} {variant_name} seed={seed}")
                        continue

                    print(f"[{idx}/{total}] {inst_filename} {variant_name} seed={seed} (budget={time_budget_ms/1000:.0f}s)...",
                          flush=True)
                    t_start = time.time()
                    result = run_one(inst_filename, time_budget_ms, variant_name, opts, seed)
                    t_wall = time.time() - t_start

                    row = {k: None for k in fields}
                    row.update({
                        "instance": inst_filename, "n": n, "m": m,
                        "variant": variant_name, "seed": seed,
                        "wall_seconds": t_wall,
                    })
                    if result.get("timed_out"):
                        row["timed_out"] = True
                        print(f"   TIMED OUT after {t_wall:.0f}s")
                    elif result.get("error"):
                        row["error"] = result["error"]
                        print(f"   ERROR: {result['error']}")
                    else:
                        row.update({k: result.get(k) for k in fields if k in result})
                        print(f"   obj={result['objective']:.0f} gini={result['gini']:.4f} "
                              f"bal={result['balance_ratio']:.3f} t={t_wall:.0f}s",
                              flush=True)
                    w.writerow(row)
                    f.flush()

    print("\nDone.")


if __name__ == "__main__":
    main()
