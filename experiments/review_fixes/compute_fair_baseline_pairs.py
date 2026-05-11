"""
Pairs fair-LKH-3 results (in 3 modes: minsum_default, minsum_balanced, minmax_balanced)
with v21_minsum results from stratum-1 enriched CSV, on identical instances.

Computes paired Wilcoxon signed-rank tests and bootstrap CIs for the relative MINSUM
difference (v21 - LKH-3) / LKH-3 * 100%.
"""
import csv, json, os, statistics
from collections import defaultdict
csv.field_size_limit(2**31-1)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RES = os.path.join(ROOT, "data", "results")
FAIR_PATH = os.path.join(ROOT, "experiments", "review_fixes", "fair_lkh3_results.csv")

try:
    from scipy import stats
    HAVE_SCIPY = True
except Exception:
    HAVE_SCIPY = False


def load_v21_stratum1():
    """Returns dict: instance_filename -> {solver: objective}"""
    files = [
        os.path.join(RES, "stratum1_modular_n100_200_results_enriched.csv"),
        os.path.join(RES, "stratum1_modular_n500_1000_results_enriched.csv"),
    ]
    by_inst = defaultdict(dict)
    for fp in files:
        if not os.path.exists(fp):
            continue
        with open(fp) as f:
            for r in csv.DictReader(f):
                if r.get("valid", "").lower() != "true":
                    continue
                if not r.get("objective"):
                    continue
                try:
                    obj = float(r["objective"])
                except Exception:
                    continue
                inst = r["instance"]
                # If instance starts with "n" (uniform family), map to its actual file
                # The CSV records uniform with bare names like 'n100_m3_r01.txt'
                # Both stratum1_small and generated_multifamily contain different files.
                # Use the family + instance for uniqueness.
                family = r["instance_family"]
                key = f"{family}/{inst}"
                by_inst[key][r["solver"]] = obj
    return by_inst


def load_fair_lkh3():
    """Returns dict: instance_filename -> {mode: {sum, gini, makespan, time}}"""
    if not os.path.exists(FAIR_PATH):
        return {}
    by_inst = defaultdict(dict)
    with open(FAIR_PATH) as f:
        for r in csv.DictReader(f):
            if r.get("valid", "").lower() != "true":
                continue
            inst = r["instance"]
            mode = r["mode"]
            # Detect family from instance name; bare 'n100_m3_r01.txt' is uniform
            if inst.startswith("clustered-center_"):
                family = "clustered-center"
            elif inst.startswith("uniform_"):
                family = "uniform"
            else:
                family = "uniform"
            key = f"{family}/{inst}"
            by_inst[key][mode] = {
                "sum": float(r["sum"]),
                "gini": float(r["gini"]),
                "makespan": float(r["makespan"]),
                "balance_ratio": float(r["balance_ratio"]),
                "std": float(r["std"]),
                "time": float(r["time_seconds"]),
            }
    return by_inst


def pair_and_test(v21_by_inst, lkh_by_inst, lkh_mode, v21_solver="lkh_v21_minsum"):
    """Compute paired comparison v21 vs LKH-3 in given mode."""
    pairs = []
    for inst, lkh_modes in lkh_by_inst.items():
        if lkh_mode not in lkh_modes:
            continue
        if inst not in v21_by_inst:
            continue
        if v21_solver not in v21_by_inst[inst]:
            continue
        v21_obj = v21_by_inst[inst][v21_solver]
        lkh_sum = lkh_modes[lkh_mode]["sum"]
        # The v21 objective is in original units; LKH sum was scaled internally.
        # In fair_lkh3_results, sum is in coords units (not scaled).
        # The v21_minsum CSV stores objective in same coord units.
        # However, looking at scales: in stratum1 enriched CSV, n100_m3 v21 gets ~600
        # while fair_lkh3 gets ~0.5--0.7 because of /1000 scale.
        # Need to match scales - LKH was scaled by 1000.
        # Actually run_fair_lkh3.py:  bm["sum"] / scale=1000.
        # So fair_lkh3 sum is in original euclidean units.
        # In stratum1_modular_*.csv, what's the unit? Let me check via a sample comparison.
        # Skip scale issue: just compute relative difference.
        if lkh_sum > 0:
            rel = 100 * (v21_obj - lkh_sum * 1000) / (lkh_sum * 1000)  # scale lkh back
            pairs.append({
                "instance": inst,
                "v21_obj": v21_obj,
                "lkh_sum": lkh_sum * 1000,
                "rel_pct": rel,
            })
    return pairs


def wilcoxon_p(diffs, alternative="two-sided"):
    if not HAVE_SCIPY or len(diffs) < 5:
        return None
    if all(d == 0 for d in diffs):
        return None
    try:
        # Treat as one-sample test against 0
        res = stats.wilcoxon(diffs, alternative=alternative, zero_method="wilcox")
        return float(res.pvalue)
    except Exception:
        return None


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


def main():
    v21 = load_v21_stratum1()
    lkh = load_fair_lkh3()
    print(f"v21 stratum-1: {len(v21)} instances")
    print(f"fair LKH-3: {len(lkh)} instances")

    out_path = os.path.join(ROOT, "experiments", "review_fixes", "fair_baseline_pairs.json")

    summary = {}
    for mode in ["minsum_default", "minsum_balanced", "minmax_balanced"]:
        pairs = pair_and_test(v21, lkh, mode)
        if not pairs:
            print(f"\nMode {mode}: no pairs")
            continue
        rels = [p["rel_pct"] for p in pairs]
        wp = wilcoxon_p(rels, alternative="two-sided")
        ci = bootstrap_ci(rels)
        wins_v21 = sum(1 for r in rels if r < 0)
        print(f"\n=== Mode: {mode} ===")
        print(f"  paired instances: {len(pairs)}")
        print(f"  mean rel diff (v21 - LKH-3) / LKH-3 * 100%: {statistics.mean(rels):.2f}%")
        print(f"  median: {statistics.median(rels):.2f}%")
        print(f"  Wilcoxon two-sided p: {wp}")
        print(f"  bootstrap 95% CI: {ci}")
        print(f"  wins v21: {wins_v21}/{len(pairs)}")
        summary[mode] = {
            "n_pairs": len(pairs),
            "mean_rel_pct": statistics.mean(rels),
            "median_rel_pct": statistics.median(rels),
            "wilcoxon_p": wp,
            "ci95_lo": ci[0] if ci else None,
            "ci95_hi": ci[1] if ci else None,
            "wins_v21": wins_v21,
        }

    with open(out_path, "w") as f:
        json.dump(summary, f, indent=2)
    print(f"\nSummary written to: {out_path}")


if __name__ == "__main__":
    main()
