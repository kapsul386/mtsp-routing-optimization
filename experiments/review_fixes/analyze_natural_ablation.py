"""
Natural-ablation analysis: compare existing solver variants on identical instances.

Each solver variant lacks specific v21 components, giving us a natural ablation
ladder without re-running anything:

  2opt+greed             — no ALNS, no SA, no GLS, no POPMUSIC, no candidate sets
  grasp                   — multi-start, RCL, no ALNS framework
  lkh-wrapper-v21         — ALNS, but simpler single-file pipeline (no PT, no autotune)
  lkh_v21_minsum          — full modular architecture
  lkh_v21_minsum_cap      — full + capacity-aware repair
  lkh_v21_minsum_depot2m_plus — full + depot2m seeding

The LKH-3 default-MINSUM is included as external upper bound on MINSUM (with
the well-known caveat about degenerate solutions).

Output: experiments/review_fixes/natural_ablation_summary.{csv,json}
"""

import csv
import json
import math
import os
import statistics
from collections import defaultdict

csv.field_size_limit(2**31 - 1)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RES = os.path.join(ROOT, "data", "results")


def euclidean(p, q):
    return math.hypot(p[0] - q[0], p[1] - q[1])


def load_instance(path):
    with open(path) as f:
        first = f.readline().split()
        n, m = int(first[0]), int(first[1])
        coords = []
        for _ in range(n):
            xy = f.readline().split()
            coords.append((float(xy[0]), float(xy[1])))
    return coords, n, m


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


def find_instance_path(filename):
    candidates = [
        os.path.join(ROOT, "data", "mtsp", "stratum1_small", filename),
        os.path.join(ROOT, "data", "mtsp", "generated_multifamily", filename),
        os.path.join(ROOT, "data", "mtsp", "generated_high_m_fixedgeo", filename),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def parse_routes(routes_field):
    if not routes_field:
        return None
    try:
        return json.loads(routes_field)
    except Exception:
        try:
            return json.loads(routes_field.replace("'", '"'))
        except Exception:
            return None


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
        "sum": total,
        "makespan": max(lens),
        "min_route": min(lens),
        "range": max(lens) - min(lens),
        "std": statistics.pstdev(lens) if m >= 2 else 0.0,
        "gini": gini(lens),
        "balance_ratio": max(lens) / mean if mean > 0 else float("inf"),
    }


def load_csv_results(path):
    if not os.path.exists(path):
        return []
    with open(path) as f:
        return list(csv.DictReader(f))


def collect_ablation_data():
    """Collect paired data across all stratum-3 result files."""
    files = [
        os.path.join(RES, "verify_n25000_results.csv"),
        os.path.join(RES, "night2_n50000_results.csv"),
        os.path.join(RES, "night2_n100000_results.csv"),
        os.path.join(RES, "night_n25000_results.csv"),
    ]
    by_inst = defaultdict(dict)  # instance -> solver -> {metrics}
    for fp in files:
        for r in load_csv_results(fp):
            if r.get("valid", "").lower() != "true":
                continue
            inst_path = find_instance_path(r["instance"])
            if inst_path is None:
                continue
            try:
                routes = parse_routes(r.get("routes", ""))
            except Exception:
                routes = None
            if routes is None:
                continue
            coords, n_inst, m_inst = load_instance(inst_path)
            metrics = compute_metrics(coords, routes)
            if metrics is None:
                continue
            try:
                t = float(r["time_seconds"])
            except Exception:
                t = float("nan")
            by_inst[(r["instance_family"], r["instance"])][r["solver"]] = {
                "n": n_inst,
                "m": m_inst,
                "objective": float(r["objective"]),
                "time": t,
                **metrics,
            }
    return by_inst


def main():
    data = collect_ablation_data()
    print(f"Collected {len(data)} unique (family, instance) combinations on stratum-3")

    SOLVERS_LADDER = [
        # Most ablated → least ablated
        "2opt+greed",
        "lkh-wrapper-v21",
        "lkh_v21_minsum",
        "lkh_v21_minsum_cap",
        "lkh_v21_minsum_depot2m_plus",
    ]
    DESCRIPTIONS = {
        "2opt+greed": "no ALNS / no SA / no POPMUSIC / no GLS / no candidates",
        "lkh-wrapper-v21": "single-file ALNS, simpler pipeline (no PT/autotune)",
        "lkh_v21_minsum": "full modular architecture (baseline reference)",
        "lkh_v21_minsum_cap": "full + capacity-aware repair (FILO2-inspired)",
        "lkh_v21_minsum_depot2m_plus": "full + depot-2m seeding + post-rebalance",
    }

    rows = []
    for solver in SOLVERS_LADDER:
        # Pair with lkh_v21_minsum (reference) where both available
        sums_solver, sums_ref = [], []
        gini_solver, gini_ref = [], []
        makespan_solver, makespan_ref = [], []
        time_solver, time_ref = [], []
        n_pairs = 0
        for key, solvers in data.items():
            if solver not in solvers or "lkh_v21_minsum" not in solvers:
                continue
            sums_solver.append(solvers[solver]["sum"])
            sums_ref.append(solvers["lkh_v21_minsum"]["sum"])
            gini_solver.append(solvers[solver]["gini"])
            gini_ref.append(solvers["lkh_v21_minsum"]["gini"])
            makespan_solver.append(solvers[solver]["makespan"])
            makespan_ref.append(solvers["lkh_v21_minsum"]["makespan"])
            time_solver.append(solvers[solver]["time"])
            time_ref.append(solvers["lkh_v21_minsum"]["time"])
            n_pairs += 1

        if n_pairs == 0:
            continue
        rels = [100 * (s - r) / r for s, r in zip(sums_solver, sums_ref)]
        rows.append({
            "solver": solver,
            "description": DESCRIPTIONS[solver],
            "n_pairs": n_pairs,
            "mean_sum_diff_pct": statistics.mean(rels),
            "median_sum_diff_pct": statistics.median(rels),
            "mean_gini_solver": statistics.mean(gini_solver),
            "mean_gini_ref": statistics.mean(gini_ref),
            "mean_makespan_solver": statistics.mean(makespan_solver),
            "mean_makespan_ref": statistics.mean(makespan_ref),
            "mean_time_solver": statistics.mean(time_solver),
            "mean_time_ref": statistics.mean(time_ref),
        })

    # Print
    print()
    print(f"{'Solver':32s} {'n':>3s} {'sum vs ref':>12s} {'gini':>8s} {'makespan':>10s} {'time':>8s}")
    print("-" * 100)
    for r in rows:
        print(f"{r['solver']:32s} {r['n_pairs']:>3d} {r['mean_sum_diff_pct']:>+11.2f}% "
              f"{r['mean_gini_solver']:>8.4f} {r['mean_makespan_solver']:>10.0f} "
              f"{r['mean_time_solver']:>7.1f}s")

    out_dir = os.path.join(ROOT, "experiments", "review_fixes")
    with open(os.path.join(out_dir, "natural_ablation_summary.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    with open(os.path.join(out_dir, "natural_ablation_summary.json"), "w") as f:
        json.dump(rows, f, indent=2)
    print(f"\nWrote: {os.path.join(out_dir, 'natural_ablation_summary.csv')}")


if __name__ == "__main__":
    main()
