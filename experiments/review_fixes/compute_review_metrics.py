"""
Computes standard balance metrics (Matl-Hartl-Vidal 2018), Wilcoxon signed-rank tests,
and bootstrap CIs for all existing experimental data, addressing reviewer's critique.

Outputs LaTeX-ready tables and a JSON summary.

Standard metrics computed per solution (per Matl-Hartl-Vidal 2018, Transp. Sci. 52(2)):
  - makespan      = max_s len(R_s)
  - sum           = sum_s len(R_s)
  - mean          = sum / m
  - range         = max_s len(R_s) - min_s len(R_s)
  - std           = sqrt(mean((len(R_s) - mean)^2))
  - mad           = mean(|len(R_s) - mean|)
  - gini          = sum_i sum_j |xi - xj| / (2 m^2 mean)
  - balance_ratio = max / mean   (= m * makespan / sum, which is the metric used in the report)
"""

import csv
import json
import math
import os
import sys
import statistics
from collections import defaultdict

# Allow large CSV fields
csv.field_size_limit(2**31 - 1)

try:
    from scipy import stats
    HAVE_SCIPY = True
except Exception:
    HAVE_SCIPY = False


# ----------------------------------------------------------------------
# Geometry helpers
# ----------------------------------------------------------------------

def euclidean(p, q):
    return math.hypot(p[0] - q[0], p[1] - q[1])


def load_instance(instance_path):
    """Load instance file: returns list of (x, y) tuples; coords[0] is depot."""
    with open(instance_path) as f:
        first = f.readline().split()
        n, m = int(first[0]), int(first[1])
        coords = []
        for _ in range(n):
            xy = f.readline().split()
            coords.append((float(xy[0]), float(xy[1])))
    return coords, n, m


def route_lengths(routes, coords):
    """Return list of route lengths in the same units as coords."""
    lens = []
    for route in routes:
        if not route:
            lens.append(0.0)
            continue
        L = 0.0
        for i in range(len(route) - 1):
            L += euclidean(coords[route[i]], coords[route[i + 1]])
        lens.append(L)
    return lens


# ----------------------------------------------------------------------
# Balance metrics (Matl-Hartl-Vidal 2018)
# ----------------------------------------------------------------------

def gini_coefficient(values):
    """Gini coefficient for non-negative values."""
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


def compute_balance_metrics(route_lens):
    """Return dict of standard balance metrics per Matl-Hartl-Vidal 2018."""
    if not route_lens:
        return None
    m = len(route_lens)
    total = sum(route_lens)
    mean = total / m if m > 0 else 0.0
    nonempty = [L for L in route_lens if L > 0]
    if not nonempty:
        return None
    return {
        "m": m,
        "n_nonempty": len(nonempty),
        "sum": total,
        "mean": mean,
        "makespan": max(route_lens),
        "min_route": min(route_lens),
        "min_nonempty": min(nonempty),
        "range": max(route_lens) - min(route_lens),
        "std": (statistics.pstdev(route_lens) if m >= 2 else 0.0),
        "mad": (sum(abs(L - mean) for L in route_lens) / m if m else 0.0),
        "gini": gini_coefficient(route_lens),
        "balance_ratio_max_avg": (max(route_lens) / mean if mean > 0 else float("inf")),
        "imbalance_lex": max(route_lens) / mean if mean > 0 else 0.0,
    }


# ----------------------------------------------------------------------
# Bootstrap CI
# ----------------------------------------------------------------------

def bootstrap_mean_ci(values, n_resamples=10000, level=0.95, seed=42):
    if not values:
        return None
    if HAVE_SCIPY:
        try:
            res = stats.bootstrap(
                (values,), statistic=lambda x: sum(x) / len(x),
                n_resamples=n_resamples, confidence_level=level,
                random_state=seed, method="percentile",
            )
            return float(res.confidence_interval.low), float(res.confidence_interval.high)
        except Exception:
            pass
    # fallback simple bootstrap
    import random
    rng = random.Random(seed)
    means = []
    n = len(values)
    for _ in range(n_resamples):
        s = sum(rng.choice(values) for _ in range(n))
        means.append(s / n)
    means.sort()
    lo = means[int((1 - level) / 2 * n_resamples)]
    hi = means[int((1 + level) / 2 * n_resamples) - 1]
    return lo, hi


def bootstrap_paired_diff_ci(a, b, n_resamples=10000, level=0.95, seed=42):
    """Bootstrap CI for mean of (a_i - b_i) on paired data."""
    if not a or len(a) != len(b):
        return None
    diffs = [ai - bi for ai, bi in zip(a, b)]
    return bootstrap_mean_ci(diffs, n_resamples, level, seed), diffs


def wilcoxon_signed_rank(a, b, alternative="two-sided"):
    """Returns (statistic, p_value) for Wilcoxon signed-rank, or None if not available / too small."""
    if not HAVE_SCIPY or len(a) < 5 or len(a) != len(b):
        return None
    diffs = [ai - bi for ai, bi in zip(a, b)]
    if all(d == 0 for d in diffs):
        return None
    try:
        res = stats.wilcoxon(a, b, alternative=alternative, zero_method="wilcox")
        return float(res.statistic), float(res.pvalue)
    except Exception:
        return None


# ----------------------------------------------------------------------
# Loading existing results
# ----------------------------------------------------------------------

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RES = os.path.join(ROOT, "data", "results")


def parse_routes(routes_field):
    if not routes_field:
        return None
    try:
        v = json.loads(routes_field)
    except Exception:
        try:
            v = json.loads(routes_field.replace("'", '"'))
        except Exception:
            return None
    if not isinstance(v, list):
        return None
    return v


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


def load_csv_results(path):
    """Yield dict rows from a results CSV."""
    if not os.path.exists(path):
        return []
    with open(path) as f:
        return list(csv.DictReader(f))


# ----------------------------------------------------------------------
# Audit JSONs (10 seeds variance audit + multi_m100 + h2h)
# ----------------------------------------------------------------------

def load_audit_runs(audit_dir):
    """Load all per-seed JSON files from an audit/runs directory.
    Returns list of dicts {instance, seed, objective, time, valid, routes}."""
    runs_dir = os.path.join(audit_dir, "runs")
    out = []
    if not os.path.isdir(runs_dir):
        return out
    for fn in sorted(os.listdir(runs_dir)):
        if not fn.endswith(".json"):
            continue
        # naming: {instance}__seed{NNN}.json
        base = fn[:-5]
        if "__seed" not in base:
            continue
        instance, seed_str = base.rsplit("__seed", 1)
        try:
            seed = int(seed_str)
        except Exception:
            continue
        with open(os.path.join(runs_dir, fn)) as f:
            d = json.load(f)
        out.append({
            "instance": instance,
            "seed": seed,
            "objective": d.get("objective"),
            "time": d.get("time"),
            "valid": d.get("valid"),
            "routes": d.get("routes"),
        })
    return out


# ----------------------------------------------------------------------
# Main analyses
# ----------------------------------------------------------------------

def analyze_audit_baseline():
    """Variance audit lkh_v21_minsum: 10 seeds × {n=10K, 50K, 100K} uniform."""
    audit_dir = os.path.join(RES, "audit", "baseline")
    runs = load_audit_runs(audit_dir)
    by_inst = defaultdict(list)
    for r in runs:
        if not r.get("valid"):
            continue
        by_inst[r["instance"]].append(r)

    rows = []
    for instance, rs in sorted(by_inst.items()):
        rs = sorted(rs, key=lambda x: x["seed"])
        objs = [r["objective"] for r in rs]
        times = [r["time"] for r in rs]
        # Compute balance metrics per run (need coords)
        inst_path = find_instance_path(instance + ".txt")
        if inst_path is None:
            continue
        coords, n_inst, m_inst = load_instance(inst_path)
        bmetrics_list = []
        for r in rs:
            lens = route_lengths(r["routes"], coords)
            bm = compute_balance_metrics(lens)
            if bm is not None:
                bmetrics_list.append(bm)
        # Aggregate
        gini_vals = [b["gini"] for b in bmetrics_list]
        std_vals = [b["std"] for b in bmetrics_list]
        range_vals = [b["range"] for b in bmetrics_list]
        makespan_vals = [b["makespan"] for b in bmetrics_list]
        ratio_vals = [b["balance_ratio_max_avg"] for b in bmetrics_list]

        n_seeds = len(objs)
        ci_obj = bootstrap_mean_ci(objs) if n_seeds >= 5 else None

        rows.append({
            "instance": instance,
            "n": n_inst, "m": m_inst,
            "n_seeds": n_seeds,
            "mean_obj": statistics.mean(objs),
            "std_obj": statistics.pstdev(objs) if n_seeds >= 2 else 0.0,
            "cv_obj_pct": (100 * statistics.pstdev(objs) / statistics.mean(objs)) if n_seeds >= 2 else 0.0,
            "ci95_obj_lo": ci_obj[0] if ci_obj else None,
            "ci95_obj_hi": ci_obj[1] if ci_obj else None,
            "mean_time": statistics.mean(times),
            "mean_makespan": statistics.mean(makespan_vals),
            "mean_gini": statistics.mean(gini_vals),
            "std_gini": statistics.pstdev(gini_vals) if n_seeds >= 2 else 0.0,
            "mean_std_route": statistics.mean(std_vals),
            "mean_range": statistics.mean(range_vals),
            "mean_balance_ratio": statistics.mean(ratio_vals),
        })
    return rows


def analyze_h2h_high_m():
    """Pair v21 vs v21_cap on 4 high-m instances (5 seeds each)."""
    pairs = {
        "v21": os.path.join(RES, "audit", "multi_m100", "v21"),
        "v21_cap": os.path.join(RES, "audit", "multi_m100", "v21_cap"),
    }
    extra_pairs = {
        "v21_h2h": os.path.join(RES, "audit", "h2h_n10k_m100", "v21"),
        "v21_cap_075": os.path.join(RES, "audit", "h2h_n10k_m100", "v21_cap_075"),
    }
    base = load_audit_runs(pairs["v21"])
    cap = load_audit_runs(pairs["v21_cap"])
    base_extra = load_audit_runs(extra_pairs["v21_h2h"])
    cap_extra = load_audit_runs(extra_pairs["v21_cap_075"])

    # Merge clustered-offset-depot from h2h/v21_cap_075 (the 5 seeds for the offset family)
    # Group by instance
    def group(rs):
        g = defaultdict(list)
        for r in rs:
            if r.get("valid"):
                g[r["instance"]].append(r)
        return g

    g_base = group(base)
    g_cap = group(cap)
    g_h2h_base = group(base_extra)
    g_h2h_cap = group(cap_extra)

    # Use the multi_m100 instances as primary, plus h2h offset
    all_instances = sorted(set(g_base.keys()) | set(g_cap.keys()) | set(g_h2h_cap.keys()))

    rows = []
    paired_diffs_all = []
    for inst in all_instances:
        b = g_base.get(inst) or g_h2h_base.get(inst, [])
        c = g_cap.get(inst) or g_h2h_cap.get(inst, [])
        if not b or not c:
            continue
        b = sorted(b, key=lambda x: x["seed"])
        c = sorted(c, key=lambda x: x["seed"])
        # Match seeds
        b_obj = {r["seed"]: r["objective"] for r in b}
        c_obj = {r["seed"]: r["objective"] for r in c}
        common = sorted(set(b_obj) & set(c_obj))
        if not common:
            continue
        ba = [b_obj[s] for s in common]
        ca = [c_obj[s] for s in common]
        diffs = [ci - bi for ci, bi in zip(ca, ba)]  # cap - base
        improvements = sum(1 for d in diffs if d < 0)
        wt = wilcoxon_signed_rank(ca, ba, alternative="less")
        ci_diff = bootstrap_paired_diff_ci(ca, ba) if len(ba) >= 5 else None
        rel = [100 * d / b_v for d, b_v in zip(diffs, ba)]
        rows.append({
            "instance": inst,
            "n_seeds": len(common),
            "mean_base": statistics.mean(ba),
            "mean_cap": statistics.mean(ca),
            "mean_diff": statistics.mean(diffs),
            "mean_rel_pct": statistics.mean(rel),
            "n_improved": improvements,
            "wilcoxon_p": wt[1] if wt else None,
            "ci95_lo": ci_diff[0][0] if ci_diff else None,
            "ci95_hi": ci_diff[0][1] if ci_diff else None,
        })
        paired_diffs_all.extend(rel)

    # Aggregate across all paired runs
    if paired_diffs_all:
        wt = wilcoxon_signed_rank(
            [0.0] * len(paired_diffs_all),
            [-d for d in paired_diffs_all],  # alternative form
            alternative="two-sided",
        )

    return rows, paired_diffs_all


def analyze_stratum1():
    """Stratum-1: 60 instances × {solvers}. Compute balance metrics, paired Wilcoxon vs LKH-3."""
    # Load both files and merge
    files = [
        os.path.join(RES, "stratum1_modular_n100_200_results_enriched.csv"),
        os.path.join(RES, "stratum1_modular_n500_1000_results_enriched.csv"),
    ]
    rows = []
    for fp in files:
        rows.extend(load_csv_results(fp))

    # Group by (family, n, m, repeat)
    by_inst = defaultdict(dict)  # instance -> solver -> row
    for r in rows:
        if r.get("valid", "").lower() != "true":
            continue
        if not r.get("objective"):
            continue
        try:
            obj = float(r["objective"])
        except Exception:
            continue
        by_inst[r["instance"]][r["solver"]] = r

    # Pair v21_minsum vs lkh3-baseline by instance (same instance, different solvers)
    solvers_to_compare = [
        "lkh_v21_minsum",
        "lkh_v21_minsum_cap",
        "lkh_v21_minsum_depot2m_plus",
        "lkh-wrapper-v21",
    ]

    ref_solver = "lkh3-baseline"

    out = []
    paired_lkh3 = defaultdict(list)  # solver -> list of (this_obj, lkh3_obj, instance, n, m)

    for instance, solvers in by_inst.items():
        if ref_solver not in solvers:
            continue
        lkh3_obj = float(solvers[ref_solver]["objective"])
        n = int(solvers[ref_solver]["node_count"])
        m = int(solvers[ref_solver]["salesman_count"])
        family = solvers[ref_solver]["instance_family"]
        for s in solvers_to_compare:
            if s in solvers:
                paired_lkh3[s].append({
                    "instance": instance,
                    "family": family,
                    "n": n,
                    "m": m,
                    "obj": float(solvers[s]["objective"]),
                    "lkh3_obj": lkh3_obj,
                    "balance": float(solvers[s].get("balance_max_avg") or 0),
                    "lkh3_balance": float(solvers[ref_solver].get("balance_max_avg") or 0),
                })

    # Wilcoxon for each comparison
    for s, items in paired_lkh3.items():
        if len(items) < 5:
            continue
        a = [x["obj"] for x in items]
        b = [x["lkh3_obj"] for x in items]
        wt = wilcoxon_signed_rank(a, b, alternative="two-sided")
        # Bootstrap CI on ratio (a_i - b_i) / b_i
        rel_diffs = [100 * (ai - bi) / bi for ai, bi in zip(a, b)]
        ci_rel = bootstrap_mean_ci(rel_diffs)
        wins_a = sum(1 for ai, bi in zip(a, b) if ai < bi)
        out.append({
            "solver_a": s,
            "solver_b": ref_solver,
            "n_pairs": len(items),
            "mean_rel_diff_pct": statistics.mean(rel_diffs),
            "ci95_rel_lo": ci_rel[0] if ci_rel else None,
            "ci95_rel_hi": ci_rel[1] if ci_rel else None,
            "wilcoxon_p_two_sided": wt[1] if wt else None,
            "wins_a": wins_a,
            "wins_b": len(items) - wins_a,
            "ties": 0,
        })

    return out


def analyze_n_large():
    """Compute balance metrics for n=25K, 50K, 100K results from CSV files."""
    files = [
        ("verify_n25000_results.csv", os.path.join(RES, "verify_n25000_results.csv")),
        ("night2_n50000_results.csv", os.path.join(RES, "night2_n50000_results.csv")),
        ("night2_n100000_results.csv", os.path.join(RES, "night2_n100000_results.csv")),
    ]
    out = []
    for tag, path in files:
        rows = load_csv_results(path)
        for r in rows:
            if r.get("valid", "").lower() != "true":
                continue
            try:
                routes = parse_routes(r.get("routes", ""))
            except Exception:
                routes = None
            if routes is None:
                continue
            inst_path = find_instance_path(r["instance"])
            if inst_path is None:
                continue
            coords, n_inst, m_inst = load_instance(inst_path)
            lens = route_lengths(routes, coords)
            bm = compute_balance_metrics(lens)
            if bm is None:
                continue
            try:
                obj = float(r["objective"])
            except Exception:
                obj = bm["sum"]
            try:
                t = float(r["time_seconds"])
            except Exception:
                t = float("nan")
            out.append({
                "source": tag,
                "instance": r["instance"],
                "family": r["instance_family"],
                "n": n_inst,
                "m": m_inst,
                "solver": r["solver"],
                "objective": obj,
                "time_seconds": t,
                "makespan": bm["makespan"],
                "sum": bm["sum"],
                "mean": bm["mean"],
                "min_route": bm["min_route"],
                "min_nonempty": bm["min_nonempty"],
                "range": bm["range"],
                "std": bm["std"],
                "mad": bm["mad"],
                "gini": bm["gini"],
                "balance_ratio": bm["balance_ratio_max_avg"],
                "n_nonempty": bm["n_nonempty"],
                "n_empty": m_inst - bm["n_nonempty"],
            })
    return out


# ----------------------------------------------------------------------
# Output
# ----------------------------------------------------------------------

def main():
    print("Computing audit baseline (variance audit)...")
    audit_rows = analyze_audit_baseline()
    print(f"  {len(audit_rows)} instances")

    print("Computing high-m h2h v21 vs v21_cap...")
    h2h_rows, h2h_diffs = analyze_h2h_high_m()
    print(f"  {len(h2h_rows)} instance pairs")

    print("Computing stratum-1 analysis (Wilcoxon paired)...")
    s1_rows = analyze_stratum1()
    print(f"  {len(s1_rows)} solver comparisons")

    print("Computing balance metrics for n=25K, 50K, 100K...")
    large_rows = analyze_n_large()
    print(f"  {len(large_rows)} solution rows")

    out_dir = os.path.join(ROOT, "experiments", "review_fixes")
    os.makedirs(out_dir, exist_ok=True)

    summary = {
        "audit_baseline": audit_rows,
        "h2h_high_m": h2h_rows,
        "stratum1_pairs": s1_rows,
        "large_balance_metrics": large_rows,
    }

    with open(os.path.join(out_dir, "review_metrics.json"), "w") as f:
        json.dump(summary, f, indent=2, default=str)

    # Also write CSV summaries
    if audit_rows:
        with open(os.path.join(out_dir, "audit_baseline_summary.csv"), "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(audit_rows[0].keys()))
            w.writeheader()
            w.writerows(audit_rows)

    if h2h_rows:
        with open(os.path.join(out_dir, "h2h_high_m_summary.csv"), "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(h2h_rows[0].keys()))
            w.writeheader()
            w.writerows(h2h_rows)

    if s1_rows:
        with open(os.path.join(out_dir, "stratum1_paired_wilcoxon.csv"), "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(s1_rows[0].keys()))
            w.writeheader()
            w.writerows(s1_rows)

    if large_rows:
        with open(os.path.join(out_dir, "large_balance_metrics.csv"), "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(large_rows[0].keys()))
            w.writeheader()
            w.writerows(large_rows)

    # Print summary
    print("\n=== AUDIT BASELINE (lkh_v21_minsum, 10 seeds) ===")
    for r in audit_rows:
        print(f"  {r['instance']}: cv={r['cv_obj_pct']:.3f}% gini={r['mean_gini']:.4f} "
              f"makespan={r['mean_makespan']:.0f} balance_ratio={r['mean_balance_ratio']:.3f} "
              f"CI95=[{r['ci95_obj_lo']:.0f}, {r['ci95_obj_hi']:.0f}]")

    print("\n=== HIGH-M H2H (v21 vs v21_cap, 5 seeds) ===")
    for r in h2h_rows:
        ci = f"[{r['ci95_lo']:.1f}, {r['ci95_hi']:.1f}]" if r['ci95_lo'] else "n/a"
        print(f"  {r['instance']}: dRel={r['mean_rel_pct']:+.2f}% "
              f"({r['n_improved']}/{r['n_seeds']} improved) "
              f"Wilcoxon p={r['wilcoxon_p']} CI95={ci}")

    print("\n=== STRATUM-1 PAIRED (vs LKH-3) ===")
    for r in s1_rows:
        ci = (f"[{r['ci95_rel_lo']:+.2f}%, {r['ci95_rel_hi']:+.2f}%]"
              if r['ci95_rel_lo'] is not None else "n/a")
        print(f"  {r['solver_a']}: dRel={r['mean_rel_diff_pct']:+.2f}% "
              f"wins={r['wins_a']}/{r['n_pairs']} Wilcoxon p={r['wilcoxon_p_two_sided']} "
              f"CI95={ci}")

    print("\nDone. JSON + CSVs written to:", out_dir)


if __name__ == "__main__":
    main()
