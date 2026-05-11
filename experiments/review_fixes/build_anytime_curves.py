"""
Anytime curves: cost vs time for v21_minsum on flagship instances.

Reads anytime_trace from audit/baseline/runs/*.json and plots
cost-vs-time per seed, with mean line + min/max envelope.

Output: data/results/figures/fig_anytime_*.png
"""
import json
import math
import os
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[2]
RES = ROOT / "data" / "results"
FIGS = RES / "figures"
FIGS.mkdir(parents=True, exist_ok=True)


def parse_anytime_trace(metadata):
    raw = metadata.get("anytime_trace")
    if not raw:
        return None
    try:
        if isinstance(raw, str):
            return json.loads(raw)
        return raw
    except Exception:
        return None


def collect_traces(audit_dir, tag):
    runs_dir = audit_dir / "runs"
    if not runs_dir.is_dir():
        return defaultdict(list)
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
        trace = parse_anytime_trace(d.get("metadata", {}))
        if not trace:
            continue
        by_inst[inst].append({
            "seed": seed,
            "trace": trace,
            "tag": tag,
            "final": d.get("objective"),
            "time": d.get("time"),
        })
    return by_inst


def plot_anytime(inst, runs, out_path):
    fig, ax = plt.subplots(figsize=(9, 6))
    cmap = plt.colormaps.get_cmap("tab10")
    all_finals = []

    for i, run in enumerate(runs):
        trace = run["trace"]
        if not trace:
            continue
        ts = [pt[0] / 1000.0 for pt in trace]  # ms -> s
        costs = [pt[1] for pt in trace]
        c = cmap(i % 10)
        ax.plot(ts, costs, alpha=0.4, c=c, lw=1, label=f"seed {run['seed']}")
        all_finals.append((run["time"], run["final"]))

    # Mean envelope: for each time-bucket, compute mean/min/max across seeds
    if len(runs) > 1:
        # Sample time grid
        t_min = max(1.0, min(run["trace"][0][0] / 1000 for run in runs if run["trace"]))
        t_max = max(run["trace"][-1][0] / 1000 for run in runs if run["trace"])
        n_pts = 100
        t_grid = [t_min + (t_max - t_min) * i / (n_pts - 1) for i in range(n_pts)]

        # For each grid point, get the "best so far" of each run at that time
        means, lows, highs = [], [], []
        for t in t_grid:
            vals = []
            for run in runs:
                trace = run["trace"]
                if not trace:
                    continue
                # Find best cost up to time t
                best = None
                for pt in trace:
                    if pt[0] / 1000 <= t:
                        best = pt[1]
                    else:
                        break
                if best is not None:
                    vals.append(best)
            if vals:
                means.append(statistics.mean(vals))
                lows.append(min(vals))
                highs.append(max(vals))
            else:
                means.append(None)
                lows.append(None)
                highs.append(None)
        valid = [(t, m, l, h) for t, m, l, h in zip(t_grid, means, lows, highs) if m is not None]
        if valid:
            ts_v = [v[0] for v in valid]
            ms = [v[1] for v in valid]
            ls = [v[2] for v in valid]
            hs = [v[3] for v in valid]
            ax.plot(ts_v, ms, c="black", lw=2.5, label="mean across seeds")
            ax.fill_between(ts_v, ls, hs, color="gray", alpha=0.2, label="min-max envelope")

    # Mark final convergence points
    for t, c in all_finals:
        if t and c:
            ax.scatter(t, c, c="red", marker="x", s=40, zorder=10)

    ax.set_xscale("log")
    ax.set_xlabel("Time (s, log scale)")
    ax.set_ylabel("Best MINSUM so far")
    ax.set_title(f"Anytime convergence: {inst}\n(10 seeds, audit/baseline)")
    ax.grid(alpha=0.3, which="both")
    ax.legend(loc="upper right", fontsize=8)

    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def main():
    base = collect_traces(RES / "audit" / "baseline", "baseline")
    print(f"Found {len(base)} instances with anytime_trace")

    for inst, runs in base.items():
        if len(runs) < 2:
            continue
        out_path = FIGS / f"fig_anytime_{inst}.png"
        plot_anytime(inst, runs, out_path)
        print(f"  wrote {out_path.name} ({len(runs)} seeds)")

    # Multi-panel summary: 3 instances side by side
    fig, axes = plt.subplots(1, 3, figsize=(20, 6))
    insts_to_show = ["uniform_n10000_m5_r01", "uniform_n50000_m5_r01", "uniform_n100000_m5_r01"]
    for ax, inst in zip(axes, insts_to_show):
        runs = base.get(inst, [])
        if not runs:
            ax.text(0.5, 0.5, "no data", transform=ax.transAxes)
            continue
        cmap = plt.colormaps.get_cmap("tab10")
        for i, run in enumerate(runs):
            trace = run["trace"]
            if not trace:
                continue
            ts = [pt[0] / 1000.0 for pt in trace]
            costs = [pt[1] for pt in trace]
            ax.plot(ts, costs, alpha=0.4, c=cmap(i % 10), lw=1)

        # Mean line
        if len(runs) > 1:
            t_min = max(1.0, min(run["trace"][0][0] / 1000 for run in runs if run["trace"]))
            t_max = max(run["trace"][-1][0] / 1000 for run in runs if run["trace"])
            n_pts = 80
            t_grid = [t_min + (t_max - t_min) * i / (n_pts - 1) for i in range(n_pts)]
            means = []
            for t in t_grid:
                vals = []
                for run in runs:
                    trace = run["trace"]
                    if not trace:
                        continue
                    best = None
                    for pt in trace:
                        if pt[0] / 1000 <= t:
                            best = pt[1]
                        else:
                            break
                    if best is not None:
                        vals.append(best)
                means.append(statistics.mean(vals) if vals else None)
            valid_pts = [(t, m) for t, m in zip(t_grid, means) if m is not None]
            if valid_pts:
                ax.plot([v[0] for v in valid_pts], [v[1] for v in valid_pts],
                        c="black", lw=2.5, label="mean")

        ax.set_xscale("log")
        ax.set_xlabel("Time (s, log scale)")
        ax.set_ylabel("Best MINSUM so far")
        ax.set_title(inst)
        ax.grid(alpha=0.3, which="both")

    fig.suptitle("Anytime convergence on flagship-инстансах: lkh_v21_minsum, 10 seeds",
                 fontsize=12, y=0.99)
    fig.tight_layout()
    fig.savefig(FIGS / "fig_anytime_grid.png", dpi=110)
    plt.close(fig)
    print(f"Wrote multi-panel: fig_anytime_grid.png")


if __name__ == "__main__":
    main()
