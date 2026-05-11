"""
Figure: side-by-side route visualization on n=100K uniform m=5
LEFT: ALNS-cap (balanced, cap=ceil((n-1)/m * 1.75))
RIGHT: FILO2 (CVRP-adapted, capacity=ceil((n-1)/m))

Both runs from data/results/audit/filo2_v21_largeN_20260430/
"""
import json
from pathlib import Path
import numpy as np
import matplotlib.pyplot as plt
import matplotlib

matplotlib.rcParams["font.family"] = "DejaVu Sans"
matplotlib.rcParams["font.size"] = 9

ROOT = Path(__file__).resolve().parents[3]
INSTANCE = ROOT / "data/mtsp/generated_multifamily/uniform_n100000_m5_r01.txt"
V21 = ROOT / "data/results/audit/filo2_v21_largeN_20260430/v21_n100k_uniform_m5/runs/uniform_n100000_m5_r01__seed001.json"
FILO2 = ROOT / "data/results/audit/filo2_v21_largeN_20260430/filo2_n100k_uniform_m5/uniform_n100000_m5_r01_m5_seed1_routes.json"

OUT = Path(__file__).parent / "fig_routes_compare_n100k_filo2_alns.png"


def read_instance(path):
    with open(path) as f:
        n, m = map(int, f.readline().split())
        coords = np.array([list(map(float, f.readline().split())) for _ in range(n)])
    return coords, m


def read_v21_routes(path):
    """v21 audit format: {'routes': [[node_ids...], ...], 'objective': float}"""
    with open(path) as f:
        d = json.load(f)
    return d["routes"], d.get("objective"), d.get("time")


def read_filo2_routes(path):
    """FILO2 routes: a list of lists of customer indices (1-indexed); depot = 0 implicit."""
    with open(path) as f:
        d = json.load(f)
    # d is List[List[int]]; each inner list is one route's customer sequence
    # Convert to (depot)-...-(depot) format
    routes = []
    for r in d:
        # r is list of customer ids; insert depot 0 at start and end
        if not r:
            continue
        if r[0] != 0:
            r = [0] + r + [0]
        routes.append(r)
    return routes


def route_length(coords, route):
    if len(route) < 2:
        return 0.0
    pts = coords[np.array(route)]
    diffs = np.diff(pts, axis=0)
    return float(np.sqrt((diffs ** 2).sum(axis=1)).sum())


def plot_routes(ax, coords, routes, title, palette):
    depot = coords[0]
    sizes = []
    for i, route in enumerate(routes):
        if len(route) < 2:
            continue
        pts = coords[np.array(route)]
        ax.plot(pts[:, 0], pts[:, 1], color=palette[i % len(palette)],
                linewidth=0.18, alpha=0.7)
        sizes.append(len(route) - 2)  # subtract two depot endpoints

    ax.scatter([depot[0]], [depot[1]], marker="*", s=180, c="red",
               edgecolors="black", linewidth=0.6, zorder=10, label="депо")
    ax.set_aspect("equal")
    ax.set_xticks([])
    ax.set_yticks([])
    sizes_str = ", ".join(str(s) for s in sizes)
    ax.set_title(f"{title}\nразмеры маршрутов: [{sizes_str}]", fontsize=10)


def main():
    print("Reading instance ...")
    coords, m = read_instance(INSTANCE)
    print(f"  n={len(coords)}, m={m}")

    print("Reading ALNS-cap routes ...")
    v21_routes, v21_obj, v21_t = read_v21_routes(V21)
    print(f"  {len(v21_routes)} routes, obj={v21_obj:.0f}, t={v21_t:.1f}s")

    print("Reading FILO2 routes ...")
    filo2_routes = read_filo2_routes(FILO2)
    filo2_obj = sum(route_length(coords, r) for r in filo2_routes)
    print(f"  {len(filo2_routes)} routes, obj={filo2_obj:.0f}")

    palette_v21 = ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd",
                   "#8c564b", "#e377c2", "#7f7f7f"]
    palette_f = ["#17becf", "#bcbd22", "#e377c2", "#9467bd", "#8c564b",
                 "#1f77b4", "#ff7f0e", "#2ca02c"]

    fig, axes = plt.subplots(1, 2, figsize=(11, 5.5))
    plot_routes(axes[0], coords, v21_routes,
                f"ALNS-cap (balanced)\nMINSUM={v21_obj/1e6:.2f}M, "
                f"max-route={max(route_length(coords, r) for r in v21_routes)/1e6:.2f}M, "
                f"t={v21_t:.0f}s",
                palette_v21)
    f_max = max(route_length(coords, r) for r in filo2_routes)
    plot_routes(axes[1], coords, filo2_routes,
                f"FILO2 (CVRP-adapted)\nMINSUM={filo2_obj/1e6:.2f}M, "
                f"max-route={f_max/1e6:.2f}M, t≈824s",
                palette_f)

    fig.suptitle("Сравнение структуры маршрутов на $N{=}100\\,000$, $m{=}5$ (uniform). "
                 "Слева — собственный ALNS-cap (5 сбалансированных зон, MINSUM на 1.5\\% хуже, "
                 "но max-route в 1.32× меньше); справа — FILO2 в CVRP-адаптации.",
                 fontsize=10, y=0.04)
    plt.tight_layout(rect=[0, 0.07, 1, 0.98])
    plt.savefig(OUT, dpi=170, bbox_inches="tight")
    print(f"Saved: {OUT}")


if __name__ == "__main__":
    main()
