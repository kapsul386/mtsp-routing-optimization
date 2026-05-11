"""Regenerate fig_ruin_recreate_demo.png.

Fixes the three issues with the previous version:
  (a) starting solution, 4 routes.
  (b) DESTROY:  k clients removed -> the edges through them are removed,
                neighbours are connected directly ("repaired" graph
                before repair phase).  Removed clients drawn as orphan
                red crosses with no incident edges.
  (c) REPAIR:   removed clients inserted back via cheapest insertion;
                topology is genuinely DIFFERENT from (a) -- some yellow
                clients land in different routes / different positions.

The figure intentionally drops the long sup-title -- the caption in
main.tex already describes the picture.
"""
from __future__ import annotations
import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

OUT = Path(__file__).resolve().parent.parent / "fig_ruin_recreate_demo.png"

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 12,
    "axes.titlesize": 13,
    "legend.fontsize": 11,
    "figure.dpi": 150,
})

# ---------------------------------------------------------------------------
# Toy instance: 27 points, 4 routes
# ---------------------------------------------------------------------------
rng = np.random.default_rng(7)
depot = np.array([50.0, 50.0])

# Cluster centres around the depot, one per route.
centres = np.array([
    [25, 80],  # route 0 (green, NW)
    [80, 75],  # route 1 (orange, NE)
    [80, 25],  # route 2 (blue, SE)
    [20, 25],  # route 3 (purple, SW)
])

pts = [depot]
route_of = [-1]  # depot has no route
for r, c in enumerate(centres):
    for _ in range(6):
        p = c + rng.normal(0, 8, size=2)
        pts.append(p)
        route_of.append(r)
# Plus a few "scattered" outliers
extras = [(55, 88), (90, 50), (55, 10)]
for p in extras:
    pts.append(np.array(p, dtype=float))
    # nearest cluster
    d = [np.linalg.norm(np.array(p) - c) for c in centres]
    route_of.append(int(np.argmin(d)))
pts = np.array(pts)
N = len(pts) - 1  # excluding depot

# Order each route greedily (nearest-neighbour starting from depot)
routes = [[] for _ in range(4)]
for i in range(1, len(pts)):
    routes[route_of[i]].append(i)

def order_route(idx_list):
    if not idx_list:
        return []
    remaining = list(idx_list)
    cur = 0  # depot index
    ordered = []
    while remaining:
        nxt = min(remaining, key=lambda j: np.linalg.norm(pts[cur] - pts[j]))
        ordered.append(nxt)
        remaining.remove(nxt)
        cur = nxt
    return ordered

routes = [order_route(r) for r in routes]
route_colors = ["#4daf4a", "#ff7f00", "#377eb8", "#984ea3"]

# Pick k=8 clients to destroy (a mix from routes 1 & 2 around the right side)
destroyed = []
# 4 from route 1, 3 from route 2, 1 from route 3 (to show cross-route effect)
destroyed += routes[1][1:4]      # 3 from orange
destroyed += routes[2][0:3]      # 3 from blue
destroyed += [routes[0][2]]      # 1 from green
destroyed += [routes[3][3]]      # 1 from purple
destroyed = sorted(set(destroyed))[:8]

# ---------------------------------------------------------------------------
# Build the (b) state: each route minus destroyed clients
# ---------------------------------------------------------------------------
routes_b = []
for r in routes:
    routes_b.append([c for c in r if c not in destroyed])

# ---------------------------------------------------------------------------
# Build the (c) state: insert destroyed clients into a (possibly different)
# route via cheapest insertion.  This produces a topology that differs from
# (a) because some clients now sit in a different route / position.
# ---------------------------------------------------------------------------
def insertion_cost(route_pts, p):
    """Return (best_cost, best_pos) for inserting point p into route_pts.
    Route is implicitly closed at depot=0 on both sides.
    """
    if not route_pts:
        # depot -> p -> depot
        return 2.0 * np.linalg.norm(pts[0] - p), 0
    seq = [0] + route_pts + [0]
    best = (float("inf"), 0)
    for k in range(len(seq) - 1):
        a, b = seq[k], seq[k + 1]
        cost = (np.linalg.norm(pts[a] - p)
                + np.linalg.norm(p - pts[b])
                - np.linalg.norm(pts[a] - pts[b]))
        if cost < best[0]:
            best = (cost, k)
    return best

routes_c = [list(r) for r in routes_b]
# Insert destroyed in a perturbed order so that the result is not identical to (a)
order = destroyed[::-1]
for client in order:
    p = pts[client]
    best = (float("inf"), -1, -1)  # cost, route, pos
    for ri, r in enumerate(routes_c):
        cost, pos = insertion_cost(r, p)
        if cost < best[0]:
            best = (cost, ri, pos)
    ri, pos = best[1], best[2]
    routes_c[ri].insert(pos, client)

# ---------------------------------------------------------------------------
# Drawing helpers
# ---------------------------------------------------------------------------
def draw_route(ax, route, color, lw=1.8, alpha=0.95):
    if not route:
        return
    seq = [0] + route + [0]
    xs = [pts[i, 0] for i in seq]
    ys = [pts[i, 1] for i in seq]
    ax.plot(xs, ys, color=color, lw=lw, alpha=alpha, zorder=1)


def draw_points(ax, idxs, **kw):
    ax.scatter(pts[idxs, 0], pts[idxs, 1], **kw)


def panel(ax, title, routes_state, *, highlight=None, mode="a"):
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 100)
    ax.set_aspect("equal")
    ax.set_xticks([])
    ax.set_yticks([])
    ax.set_title(title, fontsize=12)

    # routes
    for r, col in zip(routes_state, route_colors):
        draw_route(ax, r, col)

    # all non-depot, non-highlighted clients
    all_clients = set(range(1, len(pts)))
    hl = set(highlight or [])
    plain = sorted(all_clients - hl)
    draw_points(ax, plain, s=55, c="#5a8bb5", edgecolors="black",
                linewidths=0.6, zorder=2)

    # highlights
    if mode == "destroy":
        # red crosses, no fill, no connecting edges (already absent from routes_state)
        for c in hl:
            ax.scatter(pts[c, 0], pts[c, 1], marker="x", s=120,
                       c="#d62728", linewidths=2.5, zorder=3)
    elif mode == "repair":
        # yellow filled circles, larger, with edge
        draw_points(ax, sorted(hl), s=110, c="#ffd633",
                    edgecolors="#b58900", linewidths=1.2, zorder=3)

    # depot
    ax.scatter([pts[0, 0]], [pts[0, 1]], marker="*", s=350,
               c="#d62728", edgecolors="black", linewidths=1.0, zorder=4)
    # depot label
    ax.annotate("депо", xy=(pts[0, 0], pts[0, 1]),
                xytext=(pts[0, 0] + 3, pts[0, 1] + 4),
                fontsize=10, color="#333333")


# ---------------------------------------------------------------------------
# Figure
# ---------------------------------------------------------------------------
fig, axes = plt.subplots(1, 3, figsize=(13.5, 4.5))

panel(axes[0], "(а) Текущее решение $S$: 4 маршрута",
      routes, highlight=None, mode="a")

panel(axes[1], f"(б) Destroy: удалено $k={len(destroyed)}$ клиентов",
      routes_b, highlight=destroyed, mode="destroy")

panel(axes[2], "(в) Repair: жадная вставка",
      routes_c, highlight=destroyed, mode="repair")

plt.subplots_adjust(left=0.02, right=0.99, top=0.94, bottom=0.04, wspace=0.07)
plt.savefig(OUT, dpi=200, bbox_inches="tight")
print(f"Saved: {OUT}")
