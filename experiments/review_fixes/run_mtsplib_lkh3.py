"""
Runs LKH-3 in three fair modes on mTSPLib instances.
  - minsum_default
  - minsum_balanced
  - minmax_balanced

mTSPLib instances are small (n=51..99), so LKH-3 finishes within seconds.
This shares WSL with run_lkh3_large_n.py — but small instances finish fast.

Output: experiments/review_fixes/mtsplib_lkh3_results.csv
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

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
INSTANCES_DIR = os.path.join(ROOT, "data", "mtsp", "mtsplib")
OUT_PATH = os.path.join(ROOT, "experiments", "review_fixes", "mtsplib_lkh3_results.csv")

BASES = ["eil51", "berlin52", "eil76", "rat99"]
M_VALUES = [2, 3, 5, 7]
MODES = ["minsum_default", "minsum_balanced", "minmax_balanced"]


def run_one(inst_filename, mode, time_limit=10, scale=1000, timeout_s=120):
    inst_path = os.path.join(INSTANCES_DIR, inst_filename)
    coords, n, m = load_instance(inst_path)

    with tempfile.TemporaryDirectory(prefix="mtsplib_lkh3_") as tmp:
        prob_path = os.path.join(tmp, "problem.tsp")
        result_path = os.path.join(tmp, "result.txt")
        par_path = os.path.join(tmp, "params.par")
        write_tsp_file(coords, n, m, prob_path, scale=scale)
        write_par_file(par_path, to_wsl_path(prob_path), to_wsl_path(result_path),
                       n=n, m=m, mode=mode, time_limit=time_limit, runs=1)
        stdout, stderr, rc, wall, timed_out = run_lkh(to_wsl_path(par_path),
                                                      timeout=timeout_s)
        if timed_out:
            return None
        routes = parse_lkh_solution(result_path)
        if not routes:
            return None
        all_clients = set()
        for r in routes:
            for v in r:
                if v != 0:
                    all_clients.add(v)
        valid = (len(all_clients) == n - 1)
        bm = balance_metrics(coords, routes)
        return {
            "instance": inst_filename, "n": n, "m": m,
            "mode": mode, "time_seconds": wall,
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
        "instance", "n", "m", "mode", "time_seconds", "valid", "n_routes",
        "sum", "makespan", "min_route", "range", "std", "gini",
        "balance_ratio", "n_nonempty",
    ]

    done = set()
    if os.path.exists(OUT_PATH):
        with open(OUT_PATH) as f:
            for r in csv.DictReader(f):
                done.add((r["instance"], r["mode"]))

    write_header = not os.path.exists(OUT_PATH)
    total = len(BASES) * len(M_VALUES) * len(MODES)
    idx = 0

    with open(OUT_PATH, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        if write_header:
            w.writeheader()

        for base in BASES:
            for m in M_VALUES:
                fn = f"{base}_m{m}.txt"
                for mode in MODES:
                    idx += 1
                    if (fn, mode) in done:
                        continue
                    print(f"[{idx}/{total}] {fn} {mode}...", flush=True)
                    r = run_one(fn, mode)
                    if r is None:
                        print(f"   FAILED")
                        continue
                    row = {k: r.get(k) for k in fields}
                    w.writerow(row)
                    f.flush()
                    print(f"   sum={r['sum']:.2f} gini={r['gini']:.3f} "
                          f"bal={r['balance_ratio']:.2f} t={r['time_seconds']:.1f}s",
                          flush=True)
    print("Done.")


if __name__ == "__main__":
    main()
