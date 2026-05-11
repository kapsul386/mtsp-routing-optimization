"""
Aggregates all long-run experiment results into a summary report.

Run this after long-run tests complete. Outputs:
  - experiments/review_fixes/morning_summary.txt — human-readable summary
  - experiments/review_fixes/morning_summary.json — machine-readable
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


def wilcoxon_p(values, alternative="two-sided"):
    if not HAVE_SCIPY or len(values) < 5:
        return None
    if all(v == 0 for v in values):
        return None
    try:
        return float(stats.wilcoxon(values, alternative=alternative,
                                      zero_method="wilcox").pvalue)
    except Exception:
        return None


def aggregate_ablation():
    """Process ablation results."""
    fp = REVIEW / "ablation_results.csv"
    if not fp.exists():
        return None
    rows = list(csv.DictReader(open(fp)))
    by_variant_inst = defaultdict(list)
    for r in rows:
        if r.get("valid", "").lower() != "true":
            continue
        try:
            obj = float(r["objective"])
        except Exception:
            continue
        by_variant_inst[(r["variant"], r["instance"])].append({
            "objective": obj,
            "gini": float(r["gini"]) if r["gini"] else 0,
            "wall": float(r["wall_seconds"]) if r["wall_seconds"] else 0,
        })

    # For each instance, compute relative diff vs default
    per_instance = defaultdict(dict)
    for (variant, inst), runs in by_variant_inst.items():
        per_instance[inst][variant] = {
            "n_seeds": len(runs),
            "mean_obj": statistics.mean(r["objective"] for r in runs),
            "mean_gini": statistics.mean(r["gini"] for r in runs),
            "mean_wall": statistics.mean(r["wall"] for r in runs),
        }

    # Compare each variant to "default" within same instance
    summary = {}
    for inst, variants in per_instance.items():
        if "default" not in variants:
            continue
        default_obj = variants["default"]["mean_obj"]
        for vname, vdata in variants.items():
            if vname == "default":
                continue
            rel = 100 * (vdata["mean_obj"] - default_obj) / default_obj if default_obj else 0
            key = f"{vname}__{inst}"
            summary[key] = {
                "instance": inst,
                "variant": vname,
                "rel_pct_vs_default": rel,
                "n_seeds": vdata["n_seeds"],
                "mean_obj": vdata["mean_obj"],
                "default_obj": default_obj,
                "mean_gini": vdata["mean_gini"],
                "mean_wall_seconds": vdata["mean_wall"],
            }
    return summary


def aggregate_lkh3_long_run():
    """Process LKH-3 large-N long-run results."""
    fp = REVIEW / "lkh3_large_n_results.csv"
    if not fp.exists():
        return None
    rows = list(csv.DictReader(open(fp)))
    summary = {
        "total_attempts": len(rows),
        "completed": sum(1 for r in rows if r.get("valid", "").lower() == "true"),
        "timed_out": sum(1 for r in rows if r.get("timed_out", "").lower() == "true"),
        "by_n_mode": defaultdict(lambda: {"completed": 0, "timed_out": 0, "total": 0}),
        "valid_runs": [],
    }
    for r in rows:
        n = int(r["n"])
        mode = r["mode"]
        key = (n, mode)
        d = summary["by_n_mode"][f"{n}_{mode}"]
        d["total"] += 1
        if r.get("valid", "").lower() == "true":
            d["completed"] += 1
            summary["valid_runs"].append({
                "instance": r["instance"], "n": n, "m": int(r["m"]),
                "mode": mode, "sum": float(r["sum"]),
                "gini": float(r["gini"]),
                "time_seconds": float(r["time_seconds"]),
            })
        if r.get("timed_out", "").lower() == "true":
            d["timed_out"] += 1
    summary["by_n_mode"] = dict(summary["by_n_mode"])
    return summary


def aggregate_filo2_stratum3():
    fp = REVIEW / "filo2_stratum3_results.csv"
    if not fp.exists():
        return None
    rows = list(csv.DictReader(open(fp)))
    summary = {
        "total": len(rows),
        "completed": sum(1 for r in rows if r.get("valid", "").lower() == "true"),
        "timed_out": sum(1 for r in rows if r.get("timed_out", "").lower() == "true"),
        "results": [],
    }
    for r in rows:
        if r.get("valid", "").lower() == "true":
            summary["results"].append({
                "instance": r["instance"],
                "n": int(r["n"]) if r.get("n") else 0,
                "m_target": int(r["m_target"]) if r.get("m_target") else 0,
                "n_routes": int(r["n_routes"]) if r.get("n_routes") else 0,
                "sum": float(r["sum"]) if r.get("sum") else 0,
                "gini": float(r["gini"]) if r.get("gini") else 0,
                "time_seconds": float(r["time_seconds"]) if r.get("time_seconds") else 0,
            })
    return summary


def aggregate_multiseed():
    """Stratum-3 multi-seed v21: count seeds per instance."""
    runs_dir = ROOT / "data" / "results" / "audit" / "stratum3_multiseed" / "runs"
    if not runs_dir.is_dir():
        return None
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
        if d.get("valid"):
            by_inst[inst].append({"seed": seed, "objective": d.get("objective")})

    summary = {}
    for inst, runs in by_inst.items():
        objs = [r["objective"] for r in runs]
        if len(objs) < 2:
            continue
        ci = bootstrap_ci(objs)
        summary[inst] = {
            "n_seeds": len(runs),
            "mean": statistics.mean(objs),
            "std": statistics.pstdev(objs),
            "cv_pct": 100 * statistics.pstdev(objs) / statistics.mean(objs),
            "ci_lo": ci[0] if ci else None,
            "ci_hi": ci[1] if ci else None,
        }
    return summary


def main():
    print(f"Aggregating long-run results...\n")

    abl = aggregate_ablation()
    lkh3 = aggregate_lkh3_long_run()
    filo2 = aggregate_filo2_stratum3()
    ms = aggregate_multiseed()

    out = {"ablation": abl, "lkh3_large_n": lkh3,
           "filo2_stratum3": filo2, "multiseed": ms}

    with open(REVIEW / "morning_summary.json", "w") as f:
        json.dump(out, f, indent=2, default=str)

    # Human-readable
    lines = [f"=== OVERNIGHT RESULTS SUMMARY ({os.popen('date').read().strip()}) ==="]
    lines.append("")

    if abl:
        lines.append(f"=== ABLATION ({len(abl)} variant×instance comparisons) ===")
        for key, v in sorted(abl.items()):
            lines.append(f"  {v['variant']:30s} on {v['instance']:35s}: "
                         f"{v['rel_pct_vs_default']:+.2f}% vs default "
                         f"(seeds={v['n_seeds']}, gini={v['mean_gini']:.4f})")
        lines.append("")

    if lkh3:
        lines.append(f"=== LKH-3 LARGE-N OVERNIGHT ===")
        lines.append(f"  Total: {lkh3['total_attempts']} attempts")
        lines.append(f"  Completed: {lkh3['completed']}")
        lines.append(f"  Timed out: {lkh3['timed_out']}")
        for kk, vv in lkh3['by_n_mode'].items():
            lines.append(f"  {kk:35s}: {vv['completed']}/{vv['total']} completed, {vv['timed_out']} timed out")
        for r in lkh3['valid_runs']:
            lines.append(f"  [OK]{r['instance']:35s} {r['mode']:25s}: sum={r['sum']:.0f} "
                         f"gini={r['gini']:.3f} t={r['time_seconds']:.0f}s")
        lines.append("")

    if filo2:
        lines.append(f"=== FILO2 STRATUM-3 ===")
        lines.append(f"  Total: {filo2['total']}, completed: {filo2['completed']}, "
                     f"timed_out: {filo2['timed_out']}")
        for r in filo2['results']:
            lines.append(f"  [OK]{r['instance']:40s}: sum={r['sum']:.0f} "
                         f"routes={r['n_routes']}/{r['m_target']} "
                         f"gini={r['gini']:.3f} t={r['time_seconds']:.0f}s")
        lines.append("")

    if ms:
        lines.append(f"=== MULTISEED v21 STRATUM-3 ===")
        for inst, v in sorted(ms.items()):
            lines.append(f"  {inst:45s}: {v['n_seeds']} seeds, mean={v['mean']:.0f}, "
                         f"cv={v['cv_pct']:.3f}%, "
                         f"CI95=[{v['ci_lo']:.0f}, {v['ci_hi']:.0f}]")

    text = "\n".join(lines)
    with open(REVIEW / "morning_summary.txt", "w", encoding="utf-8") as f:
        f.write(text)
    print(text)


if __name__ == "__main__":
    main()
