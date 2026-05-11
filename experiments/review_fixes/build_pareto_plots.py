"""
Pareto cost-vs-balance plots for stratum-3 (Matl-Hartl-Vidal 2019 style).

For each (N, m, family), plots all solver results as (MINSUM, Gini) points;
size of marker proportional to wall-clock time. Pareto frontier highlighted.

Output: data/results/figures/fig_pareto_*.png
"""
import csv
import json
import math
import os
import statistics
from collections import defaultdict
from pathlib import Path

csv.field_size_limit(2**31 - 1)

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[2]
RES = ROOT / "data" / "results"
FIGS = RES / "figures"
FIGS.mkdir(parents=True, exist_ok=True)


def euclidean(p, q):
    return math.hypot(p[0] - q[0], p[1] - q[1])


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


def load_instance(path):
    with open(path) as f:
        first = f.readline().split()
        n, m = int(first[0]), int(first[1])
        coords = [tuple(map(float, f.readline().split())) for _ in range(n)]
    return coords, n, m


def find_instance_path(filename):
    for sub in ["stratum1_small", "generated_multifamily", "generated_high_m_fixedgeo", "mtsplib"]:
        p = ROOT / "data" / "mtsp" / sub / filename
        if p.exists():
            return p
    return None


def parse_routes(field):
    if not field:
        return None
    try:
        return json.loads(field)
    except Exception:
        try:
            return json.loads(field.replace("'", '"'))
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
    return {
        "sum": sum(lens),
        "makespan": max(lens),
        "gini": gini(lens),
        "balance_ratio": max(lens) / (sum(lens) / len(lens)) if sum(lens) > 0 else 0,
    }


def collect_stratum3():
    """Collect all stratum-3 solver results: (N, m, family, solver) -> {sum, gini, time}"""
    files = [
        RES / "verify_n25000_results.csv",
        RES / "extended_n50000_results.csv",
        RES / "extended_n100000_results.csv",
        RES / "large_scale_n25000_results.csv",
    ]
    out = []
    for fp in files:
        if not fp.exists():
            continue
        with open(fp) as f:
            for r in csv.DictReader(f):
                if r.get("valid", "").lower() != "true":
                    continue
                inst_path = find_instance_path(r["instance"])
                if inst_path is None:
                    continue
                routes = parse_routes(r.get("routes", ""))
                if routes is None:
                    continue
                coords, n_inst, m_inst = load_instance(inst_path)
                metrics = compute_metrics(coords, routes)
                if metrics is None:
                    continue
                out.append({
                    "n": n_inst,
                    "m": m_inst,
                    "family": r["instance_family"],
                    "instance": r["instance"],
                    "solver": r["solver"],
                    "sum": metrics["sum"],
                    "gini": metrics["gini"],
                    "balance_ratio": metrics["balance_ratio"],
                    "time": float(r.get("time_seconds", 0)),
                })
    return out


def collect_multiseed():
    """Add multi-seed v21 means."""
    runs_dir = RES / "audit" / "stratum3_multiseed" / "runs"
    if not runs_dir.is_dir():
        return []
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
        by_inst[inst].append(d)

    out = []
    for inst, runs in by_inst.items():
        inst_path = find_instance_path(inst + ".txt")
        if inst_path is None:
            continue
        coords, n, m = load_instance(inst_path)
        # Use first run for representative metrics (since we average sums separately)
        sums, ginis, makespans, times = [], [], [], []
        for r in runs:
            metrics = compute_metrics(coords, r["routes"])
            if metrics is None:
                continue
            sums.append(metrics["sum"])
            ginis.append(metrics["gini"])
            times.append(float(r.get("time", 0)))
        if not sums:
            continue
        family = inst.split("_n")[0]
        out.append({
            "n": n, "m": m, "family": family, "instance": inst + ".txt",
            "solver": "lkh_v21_minsum_3seeds",
            "sum": statistics.mean(sums),
            "gini": statistics.mean(ginis),
            "balance_ratio": 0,  # not needed for Pareto
            "time": statistics.mean(times),
        })
    return out


def is_dominated(p, others):
    """Returns True if any point in `others` dominates p (lower sum AND lower gini)."""
    for q in others:
        if q is p:
            continue
        if q["sum"] <= p["sum"] and q["gini"] <= p["gini"] and (q["sum"] < p["sum"] or q["gini"] < p["gini"]):
            return True
    return False


def plot_pareto(rows, n, m, family, out_path):
    """Plot cost vs gini for a single (n, m, family) cell."""
    cell_rows = [r for r in rows if r["n"] == n and r["m"] == m and r["family"] == family]
    if not cell_rows:
        return False

    # Group by solver, take mean if multiple
    by_solver = defaultdict(list)
    for r in cell_rows:
        by_solver[r["solver"]].append(r)

    points = []
    for solver, runs in by_solver.items():
        sums = [r["sum"] for r in runs]
        ginis = [r["gini"] for r in runs]
        times = [r["time"] for r in runs]
        points.append({
            "solver": solver,
            "sum": statistics.mean(sums),
            "gini": statistics.mean(ginis),
            "time": statistics.mean(times),
            "n_runs": len(runs),
        })

    # Mark Pareto-non-dominated
    for p in points:
        p["pareto"] = not is_dominated(p, points)

    # Plot
    fig, ax = plt.subplots(figsize=(8, 6))
    color_map = {
        "lkh3-baseline": "tab:red",
        "lkh3-baseline-minmax": "tab:pink",
        "lkh_v21_minsum": "tab:blue",
        "lkh_v21_minsum_3seeds": "tab:cyan",
        "lkh_v21_minsum_cap": "tab:green",
        "lkh_v21_minsum_depot2m_plus": "tab:orange",
        "lkh_v21_minmax": "tab:purple",
        "lkh-wrapper-v21": "tab:brown",
        "2opt+greed": "gray",
    }
    for p in points:
        c = color_map.get(p["solver"], "black")
        size = 60 + 20 * math.log10(max(1, p["time"]))
        marker = "*" if p["pareto"] else "o"
        ax.scatter(p["sum"], p["gini"], s=size * (3 if p["pareto"] else 1),
                   c=c, marker=marker, label=p["solver"], alpha=0.85,
                   edgecolors="black" if p["pareto"] else None,
                   linewidths=1.5 if p["pareto"] else 0)

    # Sort Pareto-front by sum and connect with line
    pareto_pts = sorted([p for p in points if p["pareto"]], key=lambda p: p["sum"])
    if len(pareto_pts) > 1:
        xs = [p["sum"] for p in pareto_pts]
        ys = [p["gini"] for p in pareto_pts]
        ax.plot(xs, ys, "g--", alpha=0.5, label="Pareto frontier")

    # Reference line: Gini = 0.05 (practical balance threshold)
    ax.axhline(0.05, ls=":", c="gray", alpha=0.5, label="Gini=0.05 (balanced)")
    ax.axhline((m - 1) / m, ls=":", c="red", alpha=0.5,
               label=f"Gini={1-1/m:.2f} (theor. max)")

    ax.set_xlabel("MINSUM (lower is better)")
    ax.set_ylabel("Gini coefficient (lower = more balanced)")
    ax.set_title(f"Pareto cost-vs-balance: N={n}, m={m}, {family}")
    ax.set_yscale("symlog", linthresh=0.01)
    ax.grid(alpha=0.3)
    ax.legend(loc="best", fontsize=8, framealpha=0.85)

    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)
    return True


def main():
    rows = collect_stratum3() + collect_multiseed()
    print(f"Collected {len(rows)} rows on stratum-3")

    # Generate one figure per (N, m) combination, take uniform if available else first family
    for n in [25000, 50000, 100000]:
        for m in [5, 7]:
            for family in ["uniform", "clustered-center", "clustered-offset-depot"]:
                fname = f"fig_pareto_n{n}_m{m}_{family}.png"
                if plot_pareto(rows, n, m, family, FIGS / fname):
                    print(f"  wrote {fname}")
                    break

    # Multi-panel summary (3 N × 2 m, uniform) с общей легендой и без накладывающихся annotations
    color_map = {
        "lkh3-baseline":                  "tab:red",
        "lkh3-baseline-minmax":           "tab:pink",
        "lkh_v21_minsum":                 "tab:blue",
        "lkh_v21_minsum_3seeds":          "tab:cyan",
        "lkh_v21_minsum_cap":             "tab:green",
        "lkh_v21_minsum_depot2m_plus":    "tab:orange",
        "lkh_v21_minmax":                 "tab:purple",
        "lkh-wrapper-v21":                "tab:brown",
        "2opt+greed":                     "gray",
    }
    short_label = {
        "lkh3-baseline":                  "LKH-3 default",
        "lkh3-baseline-minmax":           "LKH-3 minmax",
        "lkh_v21_minsum":                 "alns_minsum",
        "lkh_v21_minsum_3seeds":          "alns_minsum (3-seed)",
        "lkh_v21_minsum_cap":             "alns_cap",
        "lkh_v21_minsum_depot2m_plus":    "alns_depot2m+",
        "lkh_v21_minmax":                 "alns_minmax",
        "lkh-wrapper-v21":                "lkh-wrapper-v21",
        "2opt+greed":                     "2opt+greed",
    }

    fig, axes = plt.subplots(2, 3, figsize=(15, 9))
    seen_solvers = set()  # to build unique legend

    for col, n in enumerate([25000, 50000, 100000]):
        for row, m in enumerate([5, 7]):
            ax = axes[row, col]
            cell = [r for r in rows if r["n"] == n and r["m"] == m and r["family"] == "uniform"]
            by_s = defaultdict(list)
            for r in cell:
                by_s[r["solver"]].append(r)
            pts = []
            for s, rr in by_s.items():
                pts.append({
                    "solver": s,
                    "sum": statistics.mean([r["sum"] for r in rr]),
                    "gini": statistics.mean([r["gini"] for r in rr]),
                    "time": statistics.mean([r["time"] for r in rr]),
                })
            for p in pts:
                p["pareto"] = not is_dominated(p, pts)

            for p in pts:
                c = color_map.get(p["solver"], "black")
                marker = "*" if p["pareto"] else "o"
                label = short_label.get(p["solver"], p["solver"])
                # Add to legend only on first encounter (across all panels)
                lbl = label if label not in seen_solvers else None
                seen_solvers.add(label)
                ax.scatter(p["sum"], p["gini"],
                           s=180 if p["pareto"] else 50,
                           c=c, marker=marker, alpha=0.85,
                           edgecolors="black" if p["pareto"] else "none",
                           linewidths=1.0 if p["pareto"] else 0,
                           label=lbl, zorder=3)
            pareto_pts = sorted([p for p in pts if p["pareto"]], key=lambda p: p["sum"])
            if len(pareto_pts) > 1:
                xs = [p["sum"] for p in pareto_pts]
                ys = [p["gini"] for p in pareto_pts]
                ax.plot(xs, ys, "g--", alpha=0.4, linewidth=1.0, zorder=2)
            ax.axhline(0.05, ls=":", c="gray", alpha=0.4, linewidth=0.7, zorder=1)
            ax.axhline((m - 1) / m, ls=":", c="red", alpha=0.4, linewidth=0.7, zorder=1)
            ax.set_yscale("symlog", linthresh=0.01)
            ax.set_xlabel("MINSUM (lower → better)", fontsize=9)
            ax.set_ylabel("Gini (lower → balanced)", fontsize=9)
            ax.set_title(f"$N={n}$, $m={m}$, uniform", fontsize=10)
            ax.grid(alpha=0.3, linestyle='--')
            # Tick label sizes
            ax.tick_params(axis='both', which='major', labelsize=8)

    # Common legend at bottom
    handles, labels = [], []
    for ax in axes.flat:
        h, l = ax.get_legend_handles_labels()
        for hh, ll in zip(h, l):
            if ll not in labels:
                handles.append(hh)
                labels.append(ll)
    # Add Pareto frontier line + reference lines
    handles.append(plt.Line2D([0], [0], ls='--', color='green', alpha=0.6))
    labels.append('Pareto frontier')
    handles.append(plt.Line2D([0], [0], ls=':', color='gray', alpha=0.6))
    labels.append('Gini = 0.05 (practical threshold)')
    handles.append(plt.Line2D([0], [0], ls=':', color='red', alpha=0.6))
    labels.append('Gini = $1-1/m$ (theor. max degeneracy)')

    fig.legend(handles, labels, loc='lower center', ncol=4, fontsize=9,
               bbox_to_anchor=(0.5, -0.02), frameon=True, framealpha=0.9)

    fig.suptitle("Pareto cost-vs-balance: stratum-3 uniform ($m=5$, $m=7$) $\\times$ $N\\in\\{25K, 50K, 100K\\}$",
                 fontsize=12, y=1.0, fontweight='bold')
    fig.tight_layout(rect=[0, 0.06, 1, 0.99])
    out = FIGS / "fig_pareto_stratum3_uniform_grid.png"
    fig.savefig(out, dpi=130, bbox_inches='tight')
    plt.close(fig)
    print(f"Wrote multi-panel: {out}")


if __name__ == "__main__":
    main()
