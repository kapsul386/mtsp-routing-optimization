"""
Profiling v21_minsum on N=100K to validate the 'std::reverse bottleneck' hypothesis.

Approach: since v21 is C++ (mtsp.exe), we use:
  1. Wall-clock decomposition from the audit metadata (already captured)
  2. Phase-level breakdown: seed/candidate/polish/ALNS/final-2-opt

This gives us the empirical share of time per phase WITHOUT needing C++ profiler
(which would require recompiling with -pg). The Python wall-clock around mtsp.exe
is already a sound diagnostic.

Output: experiments/review_fixes/profile_v21_summary.{csv,json} + table for report.
"""
import csv
import json
import statistics
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RES = ROOT / "data" / "results"


def parse_metadata_phases(metadata):
    """Extract phase budget vs actual time from metadata."""
    def _f(k, default=0.0):
        try:
            return float(metadata.get(k, default))
        except Exception:
            return default

    def _i(k, default=0):
        try:
            return int(metadata.get(k, default))
        except Exception:
            return default

    return {
        "budget_seed_ms": _i("budget_seed_ms"),
        "budget_cand_ms": _i("budget_cand_ms"),
        "budget_polish_ms": _i("budget_polish_ms"),
        "budget_alns_ms": _i("budget_alns_ms"),
        "budget_final_ms": _i("budget_final_ms"),
        "budget_ms": _i("budget_ms"),
        "alns_iters": _i("alns_iters"),
        "alns_accepts": _i("alns_accepts"),
        "alns_best_updates": _i("alns_best_updates"),
        "after_polish_cost": _f("after_polish_cost"),
        "after_alns_cost": _f("after_alns_cost"),
        "candidate_count_avg": _f("candidate_count_avg"),
    }


def main():
    runs_dir = RES / "audit" / "baseline" / "runs"
    if not runs_dir.is_dir():
        print(f"missing {runs_dir}")
        return

    by_inst = defaultdict(list)
    for fp in sorted(runs_dir.glob("*.json")):
        name = fp.stem
        if "__seed" not in name:
            continue
        inst, _ = name.rsplit("__seed", 1)
        with open(fp) as f:
            d = json.load(f)
        if not d.get("valid"):
            continue
        meta = parse_metadata_phases(d.get("metadata", {}))
        meta["wall_total_s"] = float(d.get("time", 0))
        meta["objective"] = float(d.get("objective", 0))
        by_inst[inst].append(meta)

    rows = []
    print(f"\n{'Instance':35s} {'iters':>7s} {'accepts':>8s} {'iter/s':>8s} "
          f"{'budget_ms':>10s} {'wall_s':>8s} "
          f"{'cand%':>6s} {'polish%':>8s} {'alns%':>7s} {'final%':>7s}")
    print("-" * 120)

    for inst, runs in sorted(by_inst.items()):
        # Aggregate means
        n_seeds = len(runs)
        if n_seeds == 0:
            continue
        budget_total_ms = statistics.mean(r["budget_ms"] for r in runs)
        wall_total_s = statistics.mean(r["wall_total_s"] for r in runs)
        alns_iters = statistics.mean(r["alns_iters"] for r in runs)
        alns_accepts = statistics.mean(r["alns_accepts"] for r in runs)
        iter_rate = alns_iters / wall_total_s if wall_total_s > 0 else 0

        # Phase budget shares (from autotune)
        b_seed = statistics.mean(r["budget_seed_ms"] for r in runs)
        b_cand = statistics.mean(r["budget_cand_ms"] for r in runs)
        b_polish = statistics.mean(r["budget_polish_ms"] for r in runs)
        b_alns = statistics.mean(r["budget_alns_ms"] for r in runs)
        b_final = statistics.mean(r["budget_final_ms"] for r in runs)

        total_budget = b_seed + b_cand + b_polish + b_alns + b_final
        if total_budget == 0:
            total_budget = budget_total_ms

        seed_pct = 100 * b_seed / total_budget if total_budget else 0
        cand_pct = 100 * b_cand / total_budget if total_budget else 0
        polish_pct = 100 * b_polish / total_budget if total_budget else 0
        alns_pct = 100 * b_alns / total_budget if total_budget else 0
        final_pct = 100 * b_final / total_budget if total_budget else 0

        # ALNS effectiveness: what fraction of ALNS calls produce improvements
        before = statistics.mean(r["after_polish_cost"] for r in runs if r["after_polish_cost"] > 0)
        after = statistics.mean(r["after_alns_cost"] for r in runs if r["after_alns_cost"] > 0)
        improvement_pct = 100 * (before - after) / before if before > 0 else 0

        candidate_count = statistics.mean(r["candidate_count_avg"] for r in runs)

        rows.append({
            "instance": inst,
            "n_seeds": n_seeds,
            "alns_iters": alns_iters,
            "alns_accepts": alns_accepts,
            "iter_rate": iter_rate,
            "budget_total_ms": budget_total_ms,
            "wall_total_s": wall_total_s,
            "budget_seed_pct": seed_pct,
            "budget_cand_pct": cand_pct,
            "budget_polish_pct": polish_pct,
            "budget_alns_pct": alns_pct,
            "budget_final_pct": final_pct,
            "alns_improvement_pct": improvement_pct,
            "candidate_count_avg": candidate_count,
        })

        print(f"{inst:35s} {alns_iters:>7.0f} {alns_accepts:>8.0f} "
              f"{iter_rate:>8.2f} {budget_total_ms:>10.0f} {wall_total_s:>8.1f} "
              f"{cand_pct:>5.1f}% {polish_pct:>7.1f}% {alns_pct:>6.1f}% {final_pct:>6.1f}%")

    # Save
    out_dir = ROOT / "experiments" / "review_fixes"
    with open(out_dir / "profile_v21_summary.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    with open(out_dir / "profile_v21_summary.json", "w") as f:
        json.dump(rows, f, indent=2, default=str)

    # Compute super-linear factor
    print("\n=== Super-linear analysis ===")
    n_to_iter_rate = {25000: None, 10000: None, 50000: None, 100000: None}
    for r in rows:
        n_str = r["instance"].split("_n")[1].split("_")[0]
        n = int(n_str)
        if n in n_to_iter_rate:
            n_to_iter_rate[n] = r["iter_rate"]

    # Compare 10K vs 100K
    if n_to_iter_rate.get(10000) and n_to_iter_rate.get(100000):
        ratio = n_to_iter_rate[10000] / n_to_iter_rate[100000]
        print(f"  iter/s at n=10K: {n_to_iter_rate[10000]:.2f}")
        print(f"  iter/s at n=100K: {n_to_iter_rate[100000]:.2f}")
        print(f"  throughput drop 10K -> 100K: {ratio:.1f}× (linear would be 10×)")
        print(f"  empirical scaling exponent: log10({ratio})/log10(10) = {(ratio-1)/9:.2f} super-linear factor")
        # Better: ratio = (100K/10K)^alpha, so alpha = log(ratio) / log(10)
        import math
        alpha = math.log(ratio) / math.log(10)
        print(f"  per-iter cost ~ n^{alpha:.2f}, super-linear factor = n^{alpha-1:.2f} = {10**(alpha-1):.2f}×")


if __name__ == "__main__":
    main()
