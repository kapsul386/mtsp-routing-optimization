"""
Long-run LKH-3 large-N retry: prioritize N=25K (most likely to complete) with
extended timeouts. Document timeouts as findings per reviewer.

Strategy:
  - N=25K all 3 modes × 6 instances = 18 runs × 1800s py_timeout = up to 9 hours
  - Stop early if 2 modes fail consistently
  - Extra: 1 run on N=50K minsum_default (the fastest combination) as a probe

Output: appends to experiments/review_fixes/lkh3_large_n_results.csv (resume mode)
"""
from __future__ import annotations

import csv
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(__file__))
from run_fair_lkh3 import (
    load_instance, write_tsp_file, write_par_file, run_lkh,
    parse_lkh_solution, balance_metrics, to_wsl_path,
)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INSTANCES_DIR = os.path.join(ROOT, "data", "mtsp", "generated_multifamily")
OUT_PATH = os.path.join(ROOT, "experiments", "review_fixes", "lkh3_large_n_results.csv")

# Priority order: smaller N first, default mode first (most likely to complete)
RUNS = []
for fam in ["uniform", "clustered-center", "clustered-offset-depot"]:
    for n in [25000]:  # keep N=25K only initially
        for m in [5, 7]:
            for mode in ["minsum_default", "minsum_balanced", "minmax_balanced"]:
                fn = f"{fam}_n{n}_m{m}_r01.txt"
                RUNS.append((fn, n, m, fam, mode))

# Add probes on N=50K minsum_default (just 3 instances)
for fam in ["uniform", "clustered-center", "clustered-offset-depot"]:
    fn = f"{fam}_n50000_m5_r01.txt"
    RUNS.append((fn, 50000, 5, fam, "minsum_default"))


def run_one(inst_filename, n, m, family, mode, time_limit, py_timeout, scale=1000):
    inst_path = os.path.join(INSTANCES_DIR, inst_filename)
    coords, n_actual, m_actual = load_instance(inst_path)

    with tempfile.TemporaryDirectory(prefix="lkh3_long_run_") as tmp:
        prob_path = os.path.join(tmp, "problem.tsp")
        result_path = os.path.join(tmp, "result.txt")
        par_path = os.path.join(tmp, "params.par")

        write_tsp_file(coords, n_actual, m_actual, prob_path, scale=scale)
        write_par_file(par_path, to_wsl_path(prob_path), to_wsl_path(result_path),
                       n=n_actual, m=m_actual, mode=mode,
                       time_limit=time_limit, runs=1)

        stdout, stderr, rc, wall, timed_out = run_lkh(to_wsl_path(par_path), timeout=py_timeout)

        base = {
            "instance": inst_filename, "family": family,
            "n": n_actual, "m": m_actual, "mode": mode,
            "time_seconds": wall,
            "n_routes": 0, "valid": False, "timed_out": timed_out,
            "sum": 0, "makespan": 0, "min_route": 0,
            "range": 0, "std": 0, "gini": 0, "balance_ratio": 0,
            "n_nonempty": 0,
        }
        if timed_out:
            return base

        routes = parse_lkh_solution(result_path)
        if not routes:
            return base

        all_clients = set()
        for r in routes:
            for v in r:
                if v != 0:
                    all_clients.add(v)
        valid = (len(all_clients) == n_actual - 1)

        bm = balance_metrics(coords, routes)
        return {
            **base,
            "valid": valid, "n_routes": len(routes),
            "sum": bm["sum"] / scale,
            "makespan": bm["makespan"] / scale,
            "min_route": bm["min_route"] / scale,
            "range": bm["range"] / scale,
            "std": bm["std"] / scale,
            "gini": bm["gini"],
            "balance_ratio": bm["balance_ratio"],
            "n_nonempty": bm["n_nonempty"],
        }


def main():
    fields = [
        "instance", "family", "n", "m", "mode", "time_seconds", "valid",
        "timed_out", "n_routes", "sum", "makespan", "min_route", "range", "std",
        "gini", "balance_ratio", "n_nonempty",
    ]

    # Resume support
    done = set()
    if os.path.exists(OUT_PATH):
        with open(OUT_PATH) as f:
            for r in csv.DictReader(f):
                done.add((r["instance"], r["mode"]))
        print(f"Resume: {len(done)} (instance, mode) already attempted.")

    write_header = not os.path.exists(OUT_PATH)

    print(f"Will attempt {len(RUNS)} runs (skipping {len(done)} already done)")
    sys.stdout.flush()

    with open(OUT_PATH, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        if write_header:
            w.writeheader()

        for idx, (fn, n, m, fam, mode) in enumerate(RUNS, 1):
            if (fn, mode) in done:
                print(f"[{idx}/{len(RUNS)}] SKIP {fn} {mode}")
                continue

            # Time limits scaled to N
            time_limit = 150 if n <= 25000 else 250
            py_timeout = 1800 if n <= 25000 else 3600

            print(f"[{idx}/{len(RUNS)}] {fn} {mode} (TIME_LIMIT={time_limit}s, py_timeout={py_timeout}s)...",
                  flush=True)
            t0 = time.time()
            try:
                r = run_one(fn, n, m, fam, mode, time_limit, py_timeout)
            except Exception as ex:
                print(f"   EXCEPTION: {ex}", flush=True)
                continue
            t_wall = time.time() - t0

            row = {k: r.get(k) for k in fields}
            w.writerow(row)
            f.flush()

            if r.get("timed_out"):
                print(f"   TIMED OUT after {t_wall:.0f}s (py_timeout={py_timeout}s)", flush=True)
            elif r.get("valid"):
                print(f"   done in {t_wall:.0f}s | sum={r['sum']:.0f} "
                      f"gini={r['gini']:.3f} bal={r['balance_ratio']:.2f}", flush=True)
            else:
                print(f"   FAILED in {t_wall:.0f}s", flush=True)

    print("All done.")


if __name__ == "__main__":
    main()
