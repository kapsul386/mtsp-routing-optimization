"""
Runs solvers on mTSPLib (Necula et al.) instances:
  - eil51, berlin52, eil76, rat99 × m∈{2,3,5,7} = 16 instances
  - 5 seeds per (instance, solver)
  - 30 s budget per run

Solvers compared:
  - lkh_v21_minsum     (our flagship)
  - lkh_v21_minsum_cap (capacity-aware)
  - lkh-wrapper-v21    (single-file)
  - 2opt+greed         (lower bound on quality)

LKH-3 is run separately by run_mtsplib_lkh3.py to avoid mtsp.exe ↔ wsl
contention on small instances (LKH-3 finishes in <5s on these sizes).

Output: experiments/review_fixes/mtsplib_v21_results.csv
"""
from __future__ import annotations
import argparse
import csv
import json
import math
import os
import statistics
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INSTANCES_DIR = ROOT / "data" / "mtsp" / "mtsplib"
MTSP_EXE = ROOT / "build" / "src" / "Release" / "mtsp.exe"
OUT_PATH = ROOT / "experiments" / "review_fixes" / "mtsplib_v21_results.csv"

SOLVERS = ["lkh_v21_minsum", "lkh_v21_minsum_cap", "lkh-wrapper-v21", "2opt+greed"]
BASES = ["eil51", "berlin52", "eil76", "rat99"]
M_VALUES = [2, 3, 5, 7]


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
        coords = []
        for _ in range(n):
            xy = f.readline().split()
            coords.append((float(xy[0]), float(xy[1])))
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
    m = len(lens)
    total = sum(lens)
    mean = total / m if m else 0.0
    return {
        "sum": total, "makespan": max(lens), "min_route": min(lens),
        "range": max(lens) - min(lens),
        "std": statistics.pstdev(lens) if m >= 2 else 0.0,
        "gini": gini(lens),
        "balance_ratio": max(lens) / mean if mean > 0 else float("inf"),
        "n_nonempty": sum(1 for L in lens if L > 0),
    }


def run_mtsp(inst_path, solver, seed=1, budget_ms=30000):
    cmd = [
        str(MTSP_EXE), "--input-file", str(inst_path),
        "--step", solver,
        "--time-budget-ms", str(budget_ms),
        "--seed", str(seed),
    ]
    t0 = time.time()
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return None
    wall = time.time() - t0
    if proc.returncode != 0:
        return None
    try:
        d = json.loads(proc.stdout)
    except Exception:
        return None
    return {
        "objective": d.get("objective"),
        "valid": d.get("valid"),
        "time": d.get("time", wall),
        "routes": d.get("routes"),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seeds", type=int, default=5)
    parser.add_argument("--budget-ms", type=int, default=30000)
    parser.add_argument("--solvers", nargs="+", default=SOLVERS)
    args = parser.parse_args()

    fields = [
        "base", "n", "m", "instance_filename", "solver", "seed",
        "time_seconds", "valid", "objective",
        "sum", "makespan", "min_route", "range", "std", "gini",
        "balance_ratio", "n_nonempty",
    ]

    # Resume support
    done = set()
    if OUT_PATH.exists():
        with open(OUT_PATH) as f:
            for r in csv.DictReader(f):
                done.add((r["base"], int(r["m"]), r["solver"], int(r["seed"])))
        print(f"Resume: {len(done)} runs already done.")

    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    write_header = not OUT_PATH.exists()

    total = len(BASES) * len(M_VALUES) * len(args.solvers) * args.seeds
    idx = 0

    with open(OUT_PATH, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        if write_header:
            w.writeheader()

        for base in BASES:
            for m in M_VALUES:
                inst_filename = f"{base}_m{m}.txt"
                inst_path = INSTANCES_DIR / inst_filename
                if not inst_path.exists():
                    print(f"  MISSING: {inst_path}")
                    continue
                coords, n, m_actual = load_instance(inst_path)

                for solver in args.solvers:
                    for seed in range(1, args.seeds + 1):
                        idx += 1
                        if (base, m, solver, seed) in done:
                            continue
                        print(f"[{idx}/{total}] {inst_filename} {solver} seed={seed}...",
                              flush=True)
                        res = run_mtsp(inst_path, solver, seed=seed, budget_ms=args.budget_ms)
                        if res is None or not res.get("valid"):
                            row = {k: None for k in fields}
                            row.update({
                                "base": base, "n": n, "m": m, "instance_filename": inst_filename,
                                "solver": solver, "seed": seed, "valid": False,
                            })
                            w.writerow(row)
                            f.flush()
                            print(f"   FAILED")
                            continue

                        bm = compute_metrics(coords, res["routes"])
                        row = {
                            "base": base, "n": n, "m": m,
                            "instance_filename": inst_filename,
                            "solver": solver, "seed": seed,
                            "time_seconds": res.get("time"),
                            "valid": True,
                            "objective": res.get("objective"),
                        }
                        if bm:
                            row.update(bm)
                        w.writerow(row)
                        f.flush()
                        print(f"   obj={res['objective']:.2f} gini={bm['gini']:.3f} "
                              f"bal={bm['balance_ratio']:.2f} t={res.get('time'):.1f}s",
                              flush=True)

    print("Done.")


if __name__ == "__main__":
    main()
