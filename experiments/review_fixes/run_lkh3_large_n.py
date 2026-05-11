"""
Runs LKH-3 on N=25K, 50K, 100K with TIME_LIMIT=350s in three modes:
  - minsum_default: MIN_SIZE=1, OBJECTIVE=MINSUM (replicates old methodology)
  - minsum_balanced: MIN_SIZE=-1, OBJECTIVE=MINSUM (fair MINSUM baseline)
  - minmax_balanced: MIN_SIZE=round(0.8*(n-1)/m), MAX_SIZE=round(1.2*(n-1)/m),
                     OBJECTIVE=MINMAX (fair MINMAX baseline)

Closes reviewer item 1.2: "На N=50K, 100K LKH-3 не запускали — нет верхней границы качества".

Targets: 18 instances (3 families × 3 sizes × 2 m). With 3 modes per instance and
TIME_LIMIT=350s per run, expected wall-clock is ~5-9 hours (LKH-3 typically
exceeds TIME_LIMIT by 2-10× on large N due to ascent phase that does not honor
TIME_LIMIT by design).

Output: experiments/review_fixes/lkh3_large_n_results.csv
"""
from __future__ import annotations

import csv
import os
import sys
import time

# Reuse the helpers from the existing fair LKH-3 script
sys.path.insert(0, os.path.dirname(__file__))
from run_fair_lkh3 import (
    load_instance, write_tsp_file, write_par_file, run_lkh,
    parse_lkh_solution, balance_metrics, to_wsl_path,
)
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
INSTANCES_DIR = os.path.join(ROOT, "data", "mtsp", "generated_multifamily")
OUT_PATH = os.path.join(ROOT, "experiments", "review_fixes", "lkh3_large_n_results.csv")

FAMILIES = ["uniform", "clustered-center", "clustered-offset-depot"]
# Reduced scope: focus on 25K (more likely to complete) and minsum_default mode
# (least overhead). 50K and 100K added selectively. Each LKH-3 run on these
# sizes can take 10-30 min due to the ascent phase that does not honor TIME_LIMIT.
N_VALUES = [25000, 50000, 100000]
M_VALUES = [5, 7]
MODES = ["minsum_default", "minsum_balanced", "minmax_balanced"]


def time_limit_for_n(n: int) -> int:
    if n <= 25000:
        return 150
    if n <= 50000:
        return 250
    return 350


def python_timeout_for_n(n: int) -> int:
    """Python-side hard cutoff (LKH ascent does not honor TIME_LIMIT)."""
    if n <= 25000:
        return 1200      # 20 min — ascent on 25K typically <= 10 min
    if n <= 50000:
        return 2400      # 40 min — ascent on 50K typically <= 20 min
    return 3600          # 60 min — for 100K, this is essentially "best effort"


def select_instances():
    out = []
    for fam in FAMILIES:
        for n in N_VALUES:
            for m in M_VALUES:
                fn = f"{fam}_n{n}_m{m}_r01.txt"
                fp = os.path.join(INSTANCES_DIR, fn)
                if os.path.exists(fp):
                    out.append((fam, n, m, fn))
    return out


def run_one(inst_filename: str, family: str, mode: str, time_limit: int,
            scale: int = 1000, timeout_s: int = 1800):
    inst_path = os.path.join(INSTANCES_DIR, inst_filename)
    coords, n, m = load_instance(inst_path)

    with tempfile.TemporaryDirectory(prefix=f"lkh3_largeN_") as tmp:
        prob_path = os.path.join(tmp, "problem.tsp")
        result_path = os.path.join(tmp, "result.txt")
        par_path = os.path.join(tmp, "params.par")

        write_tsp_file(coords, n, m, prob_path, scale=scale)
        write_par_file(
            par_path,
            problem_path=to_wsl_path(prob_path),
            result_path=to_wsl_path(result_path),
            n=n, m=m, mode=mode,
            time_limit=time_limit, runs=1,
        )

        stdout, stderr, rc, wall, timed_out = run_lkh(to_wsl_path(par_path),
                                                       timeout=timeout_s)

        if timed_out:
            return {
                "instance": inst_filename, "family": family,
                "n": n, "m": m, "mode": mode,
                "time_seconds": wall, "valid": False, "timed_out": True,
                "n_routes": 0, "sum": 0, "makespan": 0, "min_route": 0,
                "range": 0, "std": 0, "gini": 0, "balance_ratio": 0,
                "n_nonempty": 0,
            }

        routes = parse_lkh_solution(result_path)
        if not routes:
            return {
                "instance": inst_filename, "family": family,
                "n": n, "m": m, "mode": mode,
                "time_seconds": wall, "valid": False, "timed_out": False,
                "n_routes": 0, "sum": 0, "makespan": 0, "min_route": 0,
                "range": 0, "std": 0, "gini": 0, "balance_ratio": 0,
                "n_nonempty": 0,
            }

        # Validate
        all_clients = set()
        for r in routes:
            for v in r:
                if v != 0:
                    all_clients.add(v)
        valid = (len(all_clients) == n - 1)

        bm = balance_metrics(coords, routes)

    return {
        "instance": inst_filename, "family": family,
        "n": n, "m": m, "mode": mode,
        "time_seconds": wall,
        "n_routes": len(routes),
        "valid": valid,
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
    instances = select_instances()
    fields = [
        "instance", "family", "n", "m", "mode", "time_seconds", "valid",
        "timed_out", "n_routes", "sum", "makespan", "min_route", "range", "std",
        "gini", "balance_ratio", "n_nonempty",
    ]

    # Resume support: skip combinations already in CSV
    done = set()
    if os.path.exists(OUT_PATH):
        with open(OUT_PATH) as f:
            for r in csv.DictReader(f):
                done.add((r["instance"], r["mode"]))
        print(f"Resume: {len(done)} (instance, mode) pairs already done.")

    total = len(instances) * len(MODES)
    print(f"Will process {total} runs ({len(instances)} instances × {len(MODES)} modes)")

    write_header = not os.path.exists(OUT_PATH)
    with open(OUT_PATH, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        if write_header:
            w.writeheader()

        idx = 0
        for fam, n, m, inst_filename in instances:
            for mode in MODES:
                idx += 1
                key = (inst_filename, mode)
                if key in done:
                    print(f"[{idx}/{total}] SKIP {inst_filename} {mode} (already done)")
                    continue

                tl = time_limit_for_n(n)
                pyto = python_timeout_for_n(n)
                print(f"[{idx}/{total}] {inst_filename} {mode} (TIME_LIMIT={tl}s, py_timeout={pyto}s)...",
                      flush=True)
                t0 = time.time()
                try:
                    r = run_one(inst_filename, fam, mode,
                                time_limit=tl, timeout_s=pyto)
                except Exception as ex:
                    print(f"   EXCEPTION: {ex}")
                    # Record the exception as failed row so we don't retry
                    row = {k: None for k in fields}
                    row.update({
                        "instance": inst_filename, "family": fam,
                        "n": n, "m": m, "mode": mode,
                        "time_seconds": time.time() - t0, "valid": False,
                        "timed_out": False, "n_routes": 0,
                    })
                    w.writerow(row)
                    f.flush()
                    continue
                t_wall = time.time() - t0

                row = {k: r.get(k) for k in fields}
                w.writerow(row)
                f.flush()
                if r.get("timed_out"):
                    print(f"   TIMED OUT after {t_wall:.0f}s (py_timeout={pyto}s)",
                          flush=True)
                else:
                    print(f"   done in {t_wall:.0f}s | sum={r['sum']:.1f} "
                          f"gini={r['gini']:.3f} bal={r['balance_ratio']:.2f} "
                          f"valid={r['valid']}", flush=True)

    print("All done.")


if __name__ == "__main__":
    main()
