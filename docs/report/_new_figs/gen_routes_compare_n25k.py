#!/usr/bin/env python3
"""Regenerate the routes-compare figure for N=25000, m=5 (uniform).

Updated solver labels to current naming:
  LKH-3 (default)        <-- lkh3-baseline
  alns_depot2m           <-- lkh_v21_minsum_depot2m_plus
  alns_minsum            <-- lkh_v21_minsum
  alns_cap               <-- lkh_v21_minsum_cap

No global figure title (the caption lives in the LaTeX document).
"""

from __future__ import annotations

import ast
import csv
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

REPO_ROOT = Path(__file__).resolve().parents[3]
CSV_PATH = REPO_ROOT / "data" / "results" / "verify_n25000_results.csv"
INST_PATH = REPO_ROOT / "data" / "mtsp" / "generated_multifamily" / "uniform_n25000_m5_r01.txt"
OUT_PATH = REPO_ROOT / "docs" / "report" / "fig4_routes_compare_n25k.png"

PANELS = [
    ("lkh3-baseline", "LKH-3 (default)"),
    ("lkh_v21_minsum_depot2m_plus", "alns_depot2m"),
    ("lkh_v21_minsum", "alns_minsum"),
    ("lkh_v21_minsum_cap", "alns_cap"),
]
INSTANCE_NAME = "uniform_n25000_m5_r01.txt"

PALETTE = ["#377eb8", "#e41a1c", "#4daf4a", "#984ea3", "#ff7f00"]


def load_coords(path: Path) -> list[tuple[float, float]]:
    coords: list[tuple[float, float]] = []
    with path.open() as fh:
        header = fh.readline().split()
        n = int(header[0])
        for _ in range(n):
            x, y = fh.readline().split()
            coords.append((float(x), float(y)))
    return coords


def parse_routes(cell: str) -> list[list[int]]:
    try:
        return json.loads(cell)
    except json.JSONDecodeError:
        return ast.literal_eval(cell)


def load_routes(csv_path: Path, instance: str, solver: str) -> list[list[int]]:
    csv.field_size_limit(sys.maxsize if sys.platform != "win32" else 2**31 - 1)
    with csv_path.open(newline="", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            if row["instance"] != instance or row["solver"] != solver:
                continue
            return parse_routes(row["routes"])
    raise KeyError(f"{solver} on {instance} not found")


def route_sizes(routes: list[list[int]]) -> list[int]:
    sizes: list[int] = []
    for r in routes:
        body = [v for v in r if v != 0]
        sizes.append(len(body))
    return sorted(sizes, reverse=True)


def plot_panel(ax, coords, routes, title):
    sizes = route_sizes(routes)
    ax.set_title(f"{title}\nразмеры: {sizes}", fontsize=11)
    ax.set_aspect("equal")
    ax.set_xticks([])
    ax.set_yticks([])
    for spine in ax.spines.values():
        spine.set_edgecolor("#888")
        spine.set_linewidth(0.7)

    for k, r in enumerate(routes):
        if len(r) <= 2:
            continue
        xs = [coords[v][0] for v in r]
        ys = [coords[v][1] for v in r]
        color = PALETTE[k % len(PALETTE)]
        ax.plot(xs, ys, color=color, linewidth=0.35, alpha=0.85)

    depot = coords[0]
    ax.scatter([depot[0]], [depot[1]], marker="*", color="black", s=80, zorder=5)


def main() -> None:
    coords = load_coords(INST_PATH)

    fig, axes = plt.subplots(2, 2, figsize=(11, 11))
    axes_flat = axes.flatten()
    for ax, (solver_key, title) in zip(axes_flat, PANELS):
        routes = load_routes(CSV_PATH, INSTANCE_NAME, solver_key)
        plot_panel(ax, coords, routes, title)

    fig.tight_layout()
    fig.savefig(OUT_PATH, dpi=140, bbox_inches="tight")
    print(f"wrote {OUT_PATH}")


if __name__ == "__main__":
    main()
