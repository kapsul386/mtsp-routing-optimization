"""
Performs a 'metadata-based ablation' on existing v21 audit runs.

The full ALNS metadata captured in audit/baseline/runs/*.json includes per-operator
acceptance counts, weights, and usage. This is NOT a true ablation (which would
require disabling each component and re-running), but it lets us *attribute*
each component's empirical contribution to accepted improvements observed during
the run, without needing additional compute.

For each instance × seed:
  - Read 'destroy_X_calls', 'destroy_X_accepts', 'destroy_X_weight'
  - Read 'repair_X_calls', 'repair_X_accepts'
  - Compute share-of-accepts per operator
  - Read 'after_alns_cost' vs 'after_polish_cost' to attribute polish contribution
  - Read 'sa_reheats', 'pt_replicas' to identify when SA/PT contributed
  - Read 'best_via_gls' (if present) to attribute GLS

This produces a table that, while weaker than a true ablation, replaces the
'matrix of recommended ablations' with actual per-operator empirical numbers,
addressing reviewer note 1.5 partially without needing extra runtime.

Output: experiments/review_fixes/ablation_metadata_summary.{csv,json}
"""

import csv
import json
import os
import statistics
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RES = os.path.join(ROOT, "data", "results")


def load_audit(path):
    runs_dir = os.path.join(path, "runs")
    out = []
    if not os.path.isdir(runs_dir):
        return out
    for fn in sorted(os.listdir(runs_dir)):
        if not fn.endswith(".json"):
            continue
        with open(os.path.join(runs_dir, fn)) as f:
            d = json.load(f)
        out.append((fn, d))
    return out


def parse_metadata(md):
    """Extract operator stats from metadata dict (string values)."""
    def _i(k, default=0):
        try:
            return int(md.get(k, default))
        except Exception:
            try:
                return float(md.get(k, default))
            except Exception:
                return default

    def _f(k, default=0.0):
        try:
            return float(md.get(k, default))
        except Exception:
            return default

    destroy_ops = ["Random", "ClusterBfs", "Expensive", "Zone"]
    repair_ops = ["Cheapest", "Regret2"]

    info = {
        "alns_iters": _i("alns_iters"),
        "alns_accepts": _i("alns_accepts"),
        "alns_best_updates": _i("alns_best_updates"),
        "after_polish_cost": _f("after_polish_cost"),
        "after_alns_cost": _f("after_alns_cost"),
        "sa_reheats": _i("sa_reheats"),
        "pt_replicas": _i("pt_replicas"),
        "popmusic_iters": _i("popmusic_iters"),
        "candidate_count_avg": _f("candidate_count_avg"),
    }

    info["destroy"] = {}
    info["repair"] = {}
    for op in destroy_ops:
        info["destroy"][op] = {
            "calls": _i(f"destroy_{op}_calls"),
            "accepts": _i(f"destroy_{op}_accepts"),
            "weight": _f(f"destroy_{op}_weight"),
        }
    for op in repair_ops:
        info["repair"][op] = {
            "calls": _i(f"repair_{op}_calls"),
            "accepts": _i(f"repair_{op}_accepts"),
            "weight": _f(f"repair_{op}_weight"),
        }
    return info


def aggregate(audit_files):
    """Aggregate destroy/repair stats across runs and instances."""
    by_inst = defaultdict(list)
    for fn, d in audit_files:
        inst = fn.split("__seed", 1)[0]
        info = parse_metadata(d.get("metadata", {}))
        info["objective"] = d.get("objective")
        info["time"] = d.get("time")
        info["valid"] = d.get("valid")
        by_inst[inst].append(info)

    rows = []
    for inst, runs in sorted(by_inst.items()):
        # destroy operator share = within-destroy share of accepts
        d_share = {}
        d_weight = {}
        for op in ["Random", "ClusterBfs", "Expensive", "Zone"]:
            shares = []
            ws = []
            for r in runs:
                a = r["destroy"][op]["accepts"]
                tot = sum(r["destroy"][o]["accepts"] for o in r["destroy"]) or 1
                shares.append(100 * a / tot)
                ws.append(r["destroy"][op]["weight"])
            d_share[op] = statistics.mean(shares) if shares else 0.0
            d_weight[op] = statistics.mean(ws) if ws else 0.0
        r_share = {}
        r_weight = {}
        for op in ["Cheapest", "Regret2"]:
            shares = []
            ws = []
            for r in runs:
                a = r["repair"][op]["accepts"]
                tot = sum(r["repair"][o]["accepts"] for o in r["repair"]) or 1
                shares.append(100 * a / tot)
                ws.append(r["repair"][op]["weight"])
            r_share[op] = statistics.mean(shares) if shares else 0.0
            r_weight[op] = statistics.mean(ws) if ws else 0.0
        # ALNS contribution: (after_polish - final) / (initial - final), but we don't have
        # 'initial cost' here directly. Use after_polish_cost vs final objective as a proxy
        # for "additional gains from ALNS+late-polish" (positive means ALNS helped).
        polish_to_final = []
        alns_to_polish = []
        for r in runs:
            obj = r["objective"]
            polish = r["after_polish_cost"]
            alns = r["after_alns_cost"]
            if obj and polish and polish > 0:
                # ALNS phase improvement in % over polish baseline
                # i.e., (polish - alns) / polish
                if alns > 0:
                    alns_to_polish.append(100 * (polish - alns) / polish)
                # Final post-polish improvement: (alns - obj) / alns (if any final-2-opt etc.)
                if alns > 0:
                    polish_to_final.append(100 * (alns - obj) / alns)
        rows.append({
            "instance": inst,
            "n_seeds": len(runs),
            "alns_iters_mean": statistics.mean(r["alns_iters"] for r in runs),
            "alns_accepts_mean": statistics.mean(r["alns_accepts"] for r in runs),
            "best_updates_mean": statistics.mean(r["alns_best_updates"] for r in runs),
            "polish_to_alns_pct": statistics.mean(alns_to_polish) if alns_to_polish else 0.0,
            "alns_to_final_pct": statistics.mean(polish_to_final) if polish_to_final else 0.0,
            "sa_reheats_mean": statistics.mean(r["sa_reheats"] for r in runs),
            "pt_replicas_mean": statistics.mean(r["pt_replicas"] for r in runs),
            "popmusic_iters_mean": statistics.mean(r["popmusic_iters"] for r in runs),
            "cand_count_mean": statistics.mean(r["candidate_count_avg"] for r in runs),
            **{f"destroy_{op}_pct": d_share[op] for op in d_share},
            **{f"destroy_{op}_w": d_weight[op] for op in d_weight},
            **{f"repair_{op}_pct": r_share[op] for op in r_share},
            **{f"repair_{op}_w": r_weight[op] for op in r_weight},
        })
    return rows


def main():
    audit_files = load_audit(os.path.join(RES, "audit", "baseline"))
    print(f"Loaded {len(audit_files)} v21 baseline audit runs")
    rows = aggregate(audit_files)

    out_dir = os.path.join(ROOT, "experiments", "review_fixes")
    with open(os.path.join(out_dir, "ablation_metadata_summary.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    with open(os.path.join(out_dir, "ablation_metadata_summary.json"), "w") as f:
        json.dump(rows, f, indent=2)

    # Pretty print
    print()
    print(f"{'Instance':40s} {'iters':>7s} {'accepts':>8s} {'reheat':>7s} {'PT':>4s} "
          f"{'D-Rand%':>8s} {'D-Clus%':>8s} {'D-Expe%':>8s} {'D-Zone%':>8s} "
          f"{'R-Cheap%':>9s} {'R-Reg2%':>8s}")
    for r in rows:
        print(f"{r['instance']:40s} {r['alns_iters_mean']:>7.0f} "
              f"{r['alns_accepts_mean']:>8.0f} "
              f"{r['sa_reheats_mean']:>7.1f} {r['pt_replicas_mean']:>4.1f} "
              f"{r['destroy_Random_pct']:>8.1f} {r['destroy_ClusterBfs_pct']:>8.1f} "
              f"{r['destroy_Expensive_pct']:>8.1f} {r['destroy_Zone_pct']:>8.1f} "
              f"{r['repair_Cheapest_pct']:>9.1f} {r['repair_Regret2_pct']:>8.1f}")


if __name__ == "__main__":
    main()
