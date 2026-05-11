"""
Final aggregation of Level 1 results once all background jobs complete.

Inputs:
  - data/results/audit/stratum3_multiseed/runs/*.json     (multi-seed v21 N=25K-100K)
  - experiments/review_fixes/lkh3_large_n_results.csv     (LKH-3 large-N, 3 modes)
  - experiments/review_fixes/fair_lkh3_results.csv        (fair LKH-3 stratum-1)

Outputs (each as ready-to-paste LaTeX fragment + CSV):
  - level1_v21_stratum3_summary.csv         (mean+std+CI on N=25K/50K/100K with 3 seeds)
  - level1_lkh3_large_n_pairs.csv           (v21 vs LKH-3-large-N pairs, all 3 modes)
  - level1_complete_summary.json            (full picture)
"""

import csv
import json
import math
import os
import statistics
from collections import defaultdict
from pathlib import Path

csv.field_size_limit(2**31 - 1)

ROOT = Path(__file__).resolve().parents[2]
RES = ROOT / "data" / "results"
REVIEW = ROOT / "experiments" / "review_fixes"

try:
    from scipy import stats
    HAVE_SCIPY = True
except Exception:
    HAVE_SCIPY = False


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
    for sub in ["stratum1_small", "generated_multifamily", "generated_high_m_fixedgeo"]:
        p = ROOT / "data" / "mtsp" / sub / filename
        if p.exists():
            return p
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


def bootstrap_ci(values, n_resamples=10000, level=0.95, seed=42):
    if not HAVE_SCIPY or len(values) < 2:
        return None
    try:
        res = stats.bootstrap(
            (values,), statistic=lambda x: sum(x) / len(x),
            n_resamples=n_resamples, confidence_level=level,
            random_state=seed, method="percentile",
        )
        return float(res.confidence_interval.low), float(res.confidence_interval.high)
    except Exception:
        return None


def wilcoxon_p(a, b, alternative="two-sided"):
    if not HAVE_SCIPY or len(a) < 5 or len(a) != len(b):
        return None
    try:
        diffs = [ai - bi for ai, bi in zip(a, b)]
        if all(d == 0 for d in diffs):
            return None
        res = stats.wilcoxon(a, b, alternative=alternative, zero_method="wilcox")
        return float(res.pvalue)
    except Exception:
        return None


# ----------------------------------------------------------------------
# Multi-seed v21 stratum-3 summary
# ----------------------------------------------------------------------

def aggregate_multiseed():
    """Aggregate multi-seed v21 runs by instance."""
    runs_dir = RES / "audit" / "stratum3_multiseed" / "runs"
    if not runs_dir.is_dir():
        return []
    by_inst = defaultdict(list)
    for fp in sorted(runs_dir.glob("*.json")):
        name = fp.stem
        if "__seed" not in name:
            continue
        inst, seed_str = name.rsplit("__seed", 1)
        try:
            seed = int(seed_str)
        except Exception:
            continue
        with open(fp) as f:
            d = json.load(f)
        if not d.get("valid"):
            continue
        by_inst[inst].append({
            "seed": seed,
            "objective": d.get("objective"),
            "time": d.get("time"),
            "routes": d.get("routes"),
        })

    rows = []
    for inst, runs in sorted(by_inst.items()):
        inst_path = find_instance_path(inst + ".txt")
        if inst_path is None:
            continue
        coords, n, m = load_instance(inst_path)
        # Per-seed metrics
        ginis, makespans, sums = [], [], []
        objs = []
        for r in runs:
            metrics = compute_metrics(coords, r["routes"])
            if metrics is None:
                continue
            objs.append(r["objective"])
            ginis.append(metrics["gini"])
            makespans.append(metrics["makespan"])
            sums.append(metrics["sum"])

        n_seeds = len(objs)
        if n_seeds < 2:
            continue
        ci = bootstrap_ci(objs)
        rows.append({
            "instance": inst,
            "n": n, "m": m,
            "n_seeds": n_seeds,
            "mean_obj": statistics.mean(objs),
            "std_obj": statistics.pstdev(objs),
            "cv_obj_pct": 100 * statistics.pstdev(objs) / statistics.mean(objs),
            "ci_lo": ci[0] if ci else None,
            "ci_hi": ci[1] if ci else None,
            "mean_gini": statistics.mean(ginis),
            "mean_makespan": statistics.mean(makespans),
        })
    return rows


# ----------------------------------------------------------------------
# LKH-3 large-N pairs
# ----------------------------------------------------------------------

def aggregate_lkh3_large_n():
    """Pair LKH-3 large-N (3 modes) with v21 multi-seed mean on stratum-3."""
    fp = REVIEW / "lkh3_large_n_results.csv"
    if not fp.exists():
        return [], {}
    by_inst_mode = {}
    with open(fp) as f:
        for r in csv.DictReader(f):
            if r.get("valid", "").lower() != "true":
                continue
            try:
                obj = float(r["sum"])
            except Exception:
                continue
            by_inst_mode[(r["instance"], r["mode"])] = {
                "n": int(r["n"]),
                "m": int(r["m"]),
                "family": r["family"],
                "sum": obj * 1000,  # rescale to original units
                "makespan": float(r["makespan"]) * 1000,
                "gini": float(r["gini"]),
                "balance_ratio": float(r["balance_ratio"]),
                "time": float(r["time_seconds"]),
                "valid": r["valid"].lower() == "true",
            }

    # Match with v21 multi-seed
    multiseed = aggregate_multiseed()
    v21_by_inst = {r["instance"] + ".txt": r for r in multiseed}

    pairs = []
    for (inst, mode), lkh in by_inst_mode.items():
        v21 = v21_by_inst.get(inst)
        if v21 is None:
            continue
        v21_obj = v21["mean_obj"]
        rel = 100 * (v21_obj - lkh["sum"]) / lkh["sum"] if lkh["sum"] > 0 else float("nan")
        pairs.append({
            "instance": inst,
            "mode": mode,
            "n": lkh["n"], "m": lkh["m"], "family": lkh["family"],
            "v21_mean_obj": v21_obj,
            "lkh3_obj": lkh["sum"],
            "rel_pct": rel,
            "v21_n_seeds": v21["n_seeds"],
            "v21_gini": v21["mean_gini"],
            "lkh3_gini": lkh["gini"],
            "v21_makespan": v21["mean_makespan"],
            "lkh3_makespan": lkh["makespan"],
            "lkh3_time_s": lkh["time"],
        })

    # Aggregate by mode
    summary = {}
    for mode in ["minsum_default", "minsum_balanced", "minmax_balanced"]:
        mode_pairs = [p for p in pairs if p["mode"] == mode]
        if not mode_pairs:
            continue
        rels = [p["rel_pct"] for p in mode_pairs]
        wins = sum(1 for r in rels if r < 0)
        ci = bootstrap_ci(rels)
        wp = wilcoxon_p(rels, [0.0] * len(rels), alternative="two-sided") if False else None
        if HAVE_SCIPY:
            try:
                wp = float(stats.wilcoxon(rels, alternative="two-sided", zero_method="wilcox").pvalue)
            except Exception:
                wp = None
        summary[mode] = {
            "n_pairs": len(mode_pairs),
            "mean_rel_pct": statistics.mean(rels),
            "median_rel_pct": statistics.median(rels),
            "ci_lo": ci[0] if ci else None,
            "ci_hi": ci[1] if ci else None,
            "wilcoxon_p": wp,
            "wins_v21": wins,
        }
    return pairs, summary


# ----------------------------------------------------------------------
# Output
# ----------------------------------------------------------------------

def main():
    print("Aggregating multi-seed v21 stratum-3...")
    multiseed = aggregate_multiseed()
    print(f"  {len(multiseed)} instances with >=2 seeds")

    print("\n=== Multi-seed v21 stratum-3 ===")
    print(f"{'Instance':<45s} {'n':>6s} {'m':>3s} {'seeds':>6s} {'mean':>14s} "
          f"{'cv%':>6s} {'CI95':>30s}")
    for r in multiseed:
        ci = (f"[{r['ci_lo']:.0f}, {r['ci_hi']:.0f}]"
              if r['ci_lo'] is not None else "n/a")
        print(f"{r['instance']:<45s} {r['n']:>6d} {r['m']:>3d} {r['n_seeds']:>6d} "
              f"{r['mean_obj']:>14.0f} {r['cv_obj_pct']:>6.3f} {ci:>30s}")

    if multiseed:
        with open(REVIEW / "level1_v21_stratum3_summary.csv", "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(multiseed[0].keys()))
            w.writeheader()
            w.writerows(multiseed)

    print("\nAggregating LKH-3 large-N pairs...")
    pairs, summary = aggregate_lkh3_large_n()
    print(f"  {len(pairs)} (instance, mode) pairs")

    if pairs:
        print("\n=== LKH-3 large-N vs v21 (paired) ===")
        for mode in ["minsum_default", "minsum_balanced", "minmax_balanced"]:
            s = summary.get(mode)
            if s is None:
                print(f"  {mode}: no data")
                continue
            ci = f"[{s['ci_lo']:.2f}%, {s['ci_hi']:.2f}%]" if s['ci_lo'] is not None else "n/a"
            print(f"  {mode:20s} n={s['n_pairs']:>2d} "
                  f"mean={s['mean_rel_pct']:+.2f}% wins={s['wins_v21']}/{s['n_pairs']} "
                  f"p={s['wilcoxon_p']} CI={ci}")
        with open(REVIEW / "level1_lkh3_large_n_pairs.csv", "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(pairs[0].keys()))
            w.writeheader()
            w.writerows(pairs)

    # Combined summary
    out = {
        "multiseed_v21_stratum3": multiseed,
        "lkh3_large_n_pairs": pairs,
        "lkh3_large_n_summary": summary,
    }
    with open(REVIEW / "level1_complete_summary.json", "w") as f:
        json.dump(out, f, indent=2, default=str)

    print(f"\nDone. Results in: {REVIEW}")


if __name__ == "__main__":
    main()
