"""
Aggregates Level 2 results: mTSPLib v21, mTSPLib LKH-3 fair, FILO2 mTSPLib,
FILO2 stratum-3, into a single comparison.

Computes:
  - Per-instance per-solver MINSUM, Gini, makespan
  - Pairwise comparisons (v21 vs LKH-3-balanced, v21 vs FILO2)
  - Wilcoxon p-values, bootstrap CIs

Output: experiments/review_fixes/level2_summary.{csv,json}
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
REVIEW = ROOT / "experiments" / "review_fixes"

try:
    from scipy import stats
    HAVE_SCIPY = True
except Exception:
    HAVE_SCIPY = False


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


def wilcoxon_one_sample(values, alternative="two-sided"):
    if not HAVE_SCIPY or len(values) < 5:
        return None
    if all(v == 0 for v in values):
        return None
    try:
        return float(stats.wilcoxon(values, alternative=alternative,
                                      zero_method="wilcox").pvalue)
    except Exception:
        return None


def load_csv(path):
    if not os.path.exists(path):
        return []
    with open(path) as f:
        return list(csv.DictReader(f))


def aggregate_v21_mtsplib():
    """Mean-over-seeds v21 results on mTSPLib."""
    rows = load_csv(REVIEW / "mtsplib_v21_results.csv")
    by_key = defaultdict(list)
    for r in rows:
        if r.get("valid", "").lower() != "true":
            continue
        key = (r["base"], int(r["m"]), r["solver"])
        try:
            by_key[key].append({
                "objective": float(r["objective"]),
                "sum": float(r["sum"]),
                "gini": float(r["gini"]),
                "makespan": float(r["makespan"]),
                "balance_ratio": float(r["balance_ratio"]),
            })
        except Exception:
            pass

    out = {}
    for (base, m, solver), runs in by_key.items():
        if not runs:
            continue
        sums = [r["sum"] for r in runs]
        ginis = [r["gini"] for r in runs]
        makespans = [r["makespan"] for r in runs]
        out[(base, m, solver)] = {
            "n_seeds": len(runs),
            "mean_sum": statistics.mean(sums),
            "mean_gini": statistics.mean(ginis),
            "mean_makespan": statistics.mean(makespans),
            "min_sum": min(sums),
        }
    return out


def aggregate_lkh3_mtsplib():
    """LKH-3 fair-mode results on mTSPLib (no seeds, but 3 modes)."""
    rows = load_csv(REVIEW / "mtsplib_lkh3_results.csv")
    out = {}
    for r in rows:
        if r.get("valid", "").lower() != "true":
            continue
        try:
            base = r["instance"].rsplit(".", 1)[0].rsplit("_m", 1)[0]
            m = int(r["m"])
            mode = r["mode"]
            out[(base, m, mode)] = {
                "sum": float(r["sum"]) * 1000,  # rescale
                "makespan": float(r["makespan"]) * 1000,
                "gini": float(r["gini"]),
                "balance_ratio": float(r["balance_ratio"]),
                "time": float(r["time_seconds"]),
            }
        except Exception:
            pass
    return out


def aggregate_filo2_mtsplib():
    rows = load_csv(REVIEW / "filo2_mtsplib_results.csv")
    out = {}
    for r in rows:
        if r.get("valid", "").lower() != "true":
            continue
        try:
            inst = r["instance"]
            base = inst.rsplit(".", 1)[0].rsplit("_m", 1)[0]
            m = int(r["m_target"])
            out[(base, m)] = {
                "sum": float(r["sum"]),
                "makespan": float(r["makespan"]),
                "gini": float(r["gini"]),
                "balance_ratio": float(r["balance_ratio"]),
                "n_routes": int(r["n_routes"]),
                "time": float(r["time_seconds"]),
            }
        except Exception:
            pass
    return out


def main():
    v21 = aggregate_v21_mtsplib()
    lkh3 = aggregate_lkh3_mtsplib()
    filo2 = aggregate_filo2_mtsplib()

    print(f"v21 mTSPLib: {len(v21)} (base, m, solver) entries")
    print(f"LKH-3 mTSPLib: {len(lkh3)} (base, m, mode) entries")
    print(f"FILO2 mTSPLib: {len(filo2)} (base, m) entries")

    # Build a per-(base, m) comparison
    bases = sorted(set(k[0] for k in v21.keys()) | set(k[0] for k in filo2.keys()))
    ms = sorted(set(k[1] for k in v21.keys()) | set(k[1] for k in filo2.keys()))

    rows = []
    for base in bases:
        for m in ms:
            row = {"base": base, "m": m}
            v_main = v21.get((base, m, "lkh_v21_minsum"))
            v_cap = v21.get((base, m, "lkh_v21_minsum_cap"))
            v_2opt = v21.get((base, m, "2opt+greed"))
            f2 = filo2.get((base, m))
            l3_def = lkh3.get((base, m, "minsum_default"))
            l3_bal = lkh3.get((base, m, "minsum_balanced"))
            l3_minmax = lkh3.get((base, m, "minmax_balanced"))

            row["v21_sum"] = v_main["mean_sum"] if v_main else None
            row["v21_gini"] = v_main["mean_gini"] if v_main else None
            row["v21cap_sum"] = v_cap["mean_sum"] if v_cap else None
            row["v21cap_gini"] = v_cap["mean_gini"] if v_cap else None
            row["v21_2opt_sum"] = v_2opt["mean_sum"] if v_2opt else None
            row["filo2_sum"] = f2["sum"] if f2 else None
            row["filo2_gini"] = f2["gini"] if f2 else None
            row["lkh3_default_sum"] = l3_def["sum"] if l3_def else None
            row["lkh3_default_gini"] = l3_def["gini"] if l3_def else None
            row["lkh3_balanced_sum"] = l3_bal["sum"] if l3_bal else None
            row["lkh3_balanced_gini"] = l3_bal["gini"] if l3_bal else None
            row["lkh3_minmax_sum"] = l3_minmax["sum"] if l3_minmax else None
            row["lkh3_minmax_gini"] = l3_minmax["gini"] if l3_minmax else None

            # Relative differences
            if v_main and f2:
                row["v21_vs_filo2_pct"] = 100 * (v_main["mean_sum"] - f2["sum"]) / f2["sum"]
            if v_main and l3_bal:
                row["v21_vs_lkh3_balanced_pct"] = 100 * (v_main["mean_sum"] - l3_bal["sum"]) / l3_bal["sum"]
            if v_main and l3_minmax:
                row["v21_vs_lkh3_minmax_pct"] = 100 * (v_main["mean_sum"] - l3_minmax["sum"]) / l3_minmax["sum"]
            if v_main and l3_def:
                row["v21_vs_lkh3_default_pct"] = 100 * (v_main["mean_sum"] - l3_def["sum"]) / l3_def["sum"]
            rows.append(row)

    print(f"\n{len(rows)} (base, m) combinations")

    # Aggregate stats
    def agg_diffs(rows, key):
        vals = [r.get(key) for r in rows if r.get(key) is not None]
        if not vals:
            return None
        ci = bootstrap_ci(vals)
        wp = wilcoxon_one_sample(vals, alternative="two-sided")
        return {
            "n": len(vals),
            "mean": statistics.mean(vals),
            "median": statistics.median(vals),
            "wilcoxon_p": wp,
            "ci_lo": ci[0] if ci else None,
            "ci_hi": ci[1] if ci else None,
            "wins_v21": sum(1 for v in vals if v < 0),
        }

    aggregate = {
        "v21_vs_filo2": agg_diffs(rows, "v21_vs_filo2_pct"),
        "v21_vs_lkh3_default": agg_diffs(rows, "v21_vs_lkh3_default_pct"),
        "v21_vs_lkh3_balanced": agg_diffs(rows, "v21_vs_lkh3_balanced_pct"),
        "v21_vs_lkh3_minmax": agg_diffs(rows, "v21_vs_lkh3_minmax_pct"),
    }

    print("\n=== mTSPLib aggregated comparisons (v21_minsum vs others) ===")
    for label, agg in aggregate.items():
        if agg is None:
            print(f"  {label}: no data")
            continue
        ci = (f"[{agg['ci_lo']:+.2f}%, {agg['ci_hi']:+.2f}%]"
              if agg['ci_lo'] is not None else "n/a")
        print(f"  {label:30s} n={agg['n']:>2d} mean={agg['mean']:+.2f}% "
              f"median={agg['median']:+.2f}% wins={agg['wins_v21']}/{agg['n']} "
              f"p={agg['wilcoxon_p']} CI={ci}")

    # Write CSV
    with open(REVIEW / "level2_mtsplib_summary.csv", "w", newline="") as f:
        if rows:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)

    out = {
        "per_instance_summary": rows,
        "aggregate": aggregate,
    }
    with open(REVIEW / "level2_summary.json", "w") as f:
        json.dump(out, f, indent=2, default=str)


if __name__ == "__main__":
    main()
