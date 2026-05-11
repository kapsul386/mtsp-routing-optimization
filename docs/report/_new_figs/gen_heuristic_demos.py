"""Generate 4 educational illustrations of heuristic operators.

Outputs (into data/results/latex_report/):
  - fig_or_opt_demo.png         — Or-opt: move a segment of length k to a new position
  - fig_knn_candidate_demo.png  — k-NN candidate set: speed-up trick used by LKH-3 / ALNS
  - fig_regret2_demo.png        — Regret-2 vs Cheapest insertion: ALNS repair decisions
  - fig_polar_sweep_demo.png    — Polar sweep partitioning (v18 seed-strategy)

Style matches fig_2opt_demo.png — clean, two-panel, large fonts, no data dependency.
"""
from __future__ import annotations
import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyArrowPatch, Wedge
import numpy as np

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "data" / "results" / "latex_report"

plt.rcParams.update({
    "font.size": 12,
    "axes.titlesize": 13,
    "axes.labelsize": 12,
    "legend.fontsize": 11,
    "figure.dpi": 130,
})


def clean_axes(ax):
    for sp in ax.spines.values():
        sp.set_color("#999")
        sp.set_linewidth(0.8)
    ax.set_aspect("equal")
    ax.grid(False)
    ax.set_xticks([]); ax.set_yticks([])


# ============================================================
# Figure: Or-opt — move a chain of consecutive customers to a new position
# ============================================================
print("[fig] Or-opt demo")
fig, (axL, axR) = plt.subplots(1, 2, figsize=(12, 5.5), dpi=130)

# Vertices laid out as a route v1..v8
verts = np.array([
    [0.8, 1.0],   # v1
    [2.0, 2.6],   # v2
    [3.6, 3.2],   # v3 — start of segment to move
    [4.5, 2.6],   # v4 — segment
    [4.6, 1.2],   # v5 — end of segment
    [6.0, 1.5],   # v6
    [7.0, 2.6],   # v7
    [8.0, 1.6],   # v8
])
labels = [f"$v_{i+1}$" for i in range(len(verts))]

# Before: route is v1 -> v2 -> [v3 -> v4 -> v5] -> v6 -> v7 -> v8 -> v1
def draw_route(ax, route_idx, color, lw=2.0, alpha=1.0, zorder=2):
    pts = verts[route_idx]
    pts = np.vstack([pts, pts[0]])
    ax.plot(pts[:, 0], pts[:, 1], color=color, linewidth=lw,
            alpha=alpha, zorder=zorder)


# === Left panel: BEFORE Or-opt ===
order_before = [0, 1, 2, 3, 4, 5, 6, 7]  # v1..v8 in order
# Draw whole route in blue
draw_route(axL, order_before, "#3498db", lw=1.8, alpha=0.9)
# Highlight segment v3-v4-v5 in red
seg_idx = [2, 3, 4]
seg_pts = verts[seg_idx]
axL.plot(seg_pts[:, 0], seg_pts[:, 1], color="#e74c3c", linewidth=3.5,
         zorder=3, label="сегмент $[v_3, v_4, v_5]$")
# Mark the edges that will be removed: v2->v3 and v5->v6
edges_to_remove = [(1, 2), (4, 5)]
for a, b in edges_to_remove:
    axL.plot([verts[a, 0], verts[b, 0]], [verts[a, 1], verts[b, 1]],
             color="#c0392b", linewidth=2.5, linestyle=":",
             zorder=2.5, alpha=0.85)
# Nodes
axL.scatter(verts[:, 0], verts[:, 1], s=180, c="#85c1e2",
            edgecolors="black", linewidths=1.0, zorder=4)
# Highlight segment nodes
axL.scatter(verts[seg_idx, 0], verts[seg_idx, 1], s=210, c="#f1948a",
            edgecolors="black", linewidths=1.2, zorder=5)
for (x, y), lbl in zip(verts, labels):
    axL.annotate(lbl, (x, y), xytext=(0, 16), textcoords="offset points",
                 fontsize=12.5, ha="center", fontweight="bold")
axL.set_title("(а) До Or-opt: сегмент $[v_3,v_4,v_5]$ выделен красным",
              fontsize=12)
axL.legend(loc="lower right", fontsize=10.5, frameon=True, framealpha=0.95)
axL.set_xlim(0, 9); axL.set_ylim(0, 4.2)
clean_axes(axL)

# === Right panel: AFTER Or-opt ===
# Route reordered: v1 -> v2 -> v6 -> [v3 -> v4 -> v5] -> v7 -> v8 -> v1
order_after = [0, 1, 5, 2, 3, 4, 6, 7]
# Draw new route in blue
draw_route(axR, order_after, "#3498db", lw=1.8, alpha=0.9)
# Segment v3-v4-v5 (still red) at new location
axR.plot(seg_pts[:, 0], seg_pts[:, 1], color="#e74c3c", linewidth=3.5,
         zorder=3)
# Highlight new connecting edges: v2->v6, v6->v3, v5->v7
new_edges = [(1, 5), (5, 2), (4, 6)]
for a, b in new_edges:
    axR.plot([verts[a, 0], verts[b, 0]], [verts[a, 1], verts[b, 1]],
             color="#27ae60", linewidth=2.8, linestyle="-",
             zorder=2.5, alpha=0.9)
# Curved arrow showing the move
arrow = FancyArrowPatch((4.0, 3.5), (5.5, 2.6),
                        connectionstyle="arc3,rad=-0.4",
                        arrowstyle="-|>", mutation_scale=20,
                        color="#8e44ad", linewidth=2.0,
                        zorder=6)
axR.add_patch(arrow)
axR.text(4.5, 3.95, "перенос", fontsize=11, color="#8e44ad",
         fontweight="bold", ha="center")
# Nodes
axR.scatter(verts[:, 0], verts[:, 1], s=180, c="#85c1e2",
            edgecolors="black", linewidths=1.0, zorder=4)
axR.scatter(verts[seg_idx, 0], verts[seg_idx, 1], s=210, c="#f1948a",
            edgecolors="black", linewidths=1.2, zorder=5)
for (x, y), lbl in zip(verts, labels):
    axR.annotate(lbl, (x, y), xytext=(0, 16), textcoords="offset points",
                 fontsize=12.5, ha="center", fontweight="bold")

# Legend for new edges
green_patch = mpatches.Patch(color="#27ae60", label="новые рёбра $(v_2{,}v_6),\\,(v_6{,}v_3),\\,(v_5{,}v_7)$")
axR.legend(handles=[green_patch], loc="lower right",
           fontsize=10.5, frameon=True, framealpha=0.95)

axR.set_title("(б) После Or-opt: сегмент перенесён между $v_6$ и $v_7$",
              fontsize=12)
axR.set_xlim(0, 9); axR.set_ylim(0, 4.2)
clean_axes(axR)

fig.suptitle("Принцип работы Or-opt: перенос связного сегмента маршрута "
             "в новую позицию (длина сегмента $k\\in\\{1,2,3\\}$)",
             fontsize=13.5)
plt.tight_layout()
plt.savefig(OUT / "fig_or_opt_demo.png", dpi=200, bbox_inches="tight")
plt.close()


# ============================================================
# Figure: k-NN candidate set
# ============================================================
print("[fig] k-NN candidate set demo")
rng = np.random.default_rng(7)
n = 50
pts = rng.uniform(0, 10, size=(n, 2))
focus_idx = 14  # node we inspect
focus = pts[focus_idx]

# Compute distances from focus to all others
dists = np.linalg.norm(pts - focus, axis=1)
dists[focus_idx] = np.inf  # exclude self
k = 5
nearest = np.argsort(dists)[:k]

fig, (axL, axR) = plt.subplots(1, 2, figsize=(12, 5.5), dpi=130)

# === Left panel: full neighborhood (everyone) ===
# Lines from focus to ALL other nodes
for j in range(n):
    if j == focus_idx:
        continue
    axL.plot([focus[0], pts[j, 0]], [focus[1], pts[j, 1]],
             color="#bbb", linewidth=0.5, alpha=0.6, zorder=1)
axL.scatter(pts[:, 0], pts[:, 1], s=80, c="#85c1e2",
            edgecolors="black", linewidths=0.6, zorder=3)
axL.scatter(focus[0], focus[1], s=260, c="#e74c3c",
            edgecolors="black", linewidths=1.2, marker="o", zorder=5,
            label=f"узел $v$ (всего рёбер: ${n-1}$)")
axL.set_title("(а) Полный набор кандидатов: $O(n)$ рёбер на узел\n"
              "(дорого для $n\\geq 10^4$)", fontsize=12)
axL.legend(loc="upper left", fontsize=10.5, frameon=True, framealpha=0.95)
axL.set_xlim(-0.5, 10.5); axL.set_ylim(-0.5, 10.5)
clean_axes(axL)

# === Right panel: k-NN restricted ===
# Background nodes (light)
axR.scatter(pts[:, 0], pts[:, 1], s=60, c="#dfe6e9",
            edgecolors="#999", linewidths=0.4, zorder=2)
# k-NN edges highlighted
for j in nearest:
    axR.plot([focus[0], pts[j, 0]], [focus[1], pts[j, 1]],
             color="#27ae60", linewidth=2.4, alpha=0.85, zorder=3)
# Highlight k-NN nodes
axR.scatter(pts[nearest, 0], pts[nearest, 1], s=160, c="#82e0aa",
            edgecolors="black", linewidths=1.0, zorder=4)
# Focus
axR.scatter(focus[0], focus[1], s=260, c="#e74c3c",
            edgecolors="black", linewidths=1.2, marker="o", zorder=5,
            label=f"узел $v$ + его ${k}$ ближайших соседей")
# Circle showing the "radius" of k-NN
max_d = np.max(dists[nearest])
circle = plt.Circle(focus, max_d, color="#27ae60", fill=False,
                    linestyle="--", linewidth=1.6, alpha=0.6)
axR.add_patch(circle)
axR.set_title(f"(б) k-NN с $k = {k}$: только {k} рёбер на узел\n"
              "(в~10--50$\\times$ ускоряет 2-opt и repair-операторы)",
              fontsize=12)
axR.legend(loc="upper left", fontsize=10.5, frameon=True, framealpha=0.95)
axR.set_xlim(-0.5, 10.5); axR.set_ylim(-0.5, 10.5)
clean_axes(axR)

fig.suptitle("Candidate-set: ограничение рассматриваемых рёбер до $k$ "
             "ближайших соседей по KD-tree",
             fontsize=13.5)
plt.tight_layout()
plt.savefig(OUT / "fig_knn_candidate_demo.png", dpi=200, bbox_inches="tight")
plt.close()


# ============================================================
# Figure: Regret-2 vs Cheapest insertion
# ============================================================
print("[fig] Regret-2 vs Cheapest insertion demo")
fig, (axL, axR) = plt.subplots(1, 2, figsize=(12, 5.5), dpi=130)

# A small partial routes scenario:
# 2 routes (red, blue) and 2 unassigned customers (yellow)
# Show insertion costs for each customer at the best and second-best positions
depot = np.array([4.5, 4.5])
# Route 1: red, around 4 customers
r1 = np.array([
    depot,
    [1.5, 2.0],
    [2.0, 3.5],
    [1.2, 4.8],
    [2.8, 5.2],
    depot,
])
# Route 2: blue, around 3 customers
r2 = np.array([
    depot,
    [6.5, 6.5],
    [7.8, 5.0],
    [7.5, 3.0],
    depot,
])

# Two unassigned customers to be inserted: A and B
A = np.array([3.5, 2.8])  # near both routes — small regret (similar insertion costs)
B = np.array([5.5, 7.2])  # only fits well in one route — large regret

def draw_scenario(ax, title, chosen_label, chosen_pos, chosen_color):
    # Draw routes
    ax.plot(r1[:, 0], r1[:, 1], color="#e74c3c", linewidth=2.2,
            marker="o", markersize=7, markerfacecolor="#fadbd8",
            markeredgecolor="black", markeredgewidth=0.6, zorder=3,
            label="маршрут $R_1$")
    ax.plot(r2[:, 0], r2[:, 1], color="#3498db", linewidth=2.2,
            marker="s", markersize=7, markerfacecolor="#d6eaf8",
            markeredgecolor="black", markeredgewidth=0.6, zorder=3,
            label="маршрут $R_2$")
    # Depot
    ax.scatter(depot[0], depot[1], s=320, c="#f4d03f", marker="*",
               edgecolors="black", linewidths=1.0, zorder=5, label="депо")
    # Unassigned customers
    ax.scatter(*A, s=200, c="#f7dc6f", edgecolors="black",
               linewidths=1.0, marker="D", zorder=4)
    ax.annotate("A", A, xytext=(8, 8), textcoords="offset points",
                fontsize=13, fontweight="bold", color="#7d6608")
    ax.scatter(*B, s=200, c="#f7dc6f", edgecolors="black",
               linewidths=1.0, marker="D", zorder=4)
    ax.annotate("B", B, xytext=(8, 8), textcoords="offset points",
                fontsize=13, fontweight="bold", color="#7d6608")
    # Insertion cost annotations
    # For A: best in R1 (Δ=1.2), second best in R2 (Δ=2.5) → regret = 1.3
    # For B: best in R2 (Δ=0.8), second best in R1 (Δ=4.6) → regret = 3.8
    cost_box = dict(boxstyle="round,pad=0.3", fc="#fefefe",
                    ec="#888", lw=0.7)
    ax.annotate("$\\Delta_{R_1}=1.2$\n$\\Delta_{R_2}=2.5$\n"
                "$\\mathrm{regret}=1.3$",
                A, xytext=(-95, -50), textcoords="offset points",
                fontsize=9.5, bbox=cost_box, ha="left")
    ax.annotate("$\\Delta_{R_2}=0.8$\n$\\Delta_{R_1}=4.6$\n"
                "$\\mathrm{regret}=3.8$",
                B, xytext=(20, -30), textcoords="offset points",
                fontsize=9.5, bbox=cost_box, ha="left")
    # Highlight the chosen insertion
    if chosen_label == "A":
        chosen = A
    else:
        chosen = B
    # Arrow from chosen customer to chosen position
    arrow = FancyArrowPatch(chosen, chosen_pos,
                            arrowstyle="->", mutation_scale=22,
                            color=chosen_color, linewidth=2.2,
                            linestyle="--", zorder=6)
    ax.add_patch(arrow)
    ax.text(chosen[0], chosen[1] - 0.4,
            f"вставить $\\rightarrow$", fontsize=11,
            color=chosen_color, fontweight="bold", ha="center", zorder=7)

    ax.set_title(title, fontsize=12)
    ax.set_xlim(0, 10); ax.set_ylim(1.5, 8.5)
    ax.legend(loc="lower left", fontsize=10, frameon=True, framealpha=0.95)
    clean_axes(ax)


# Left: Cheapest insertion — choose B (cheapest absolute Δ=0.8)
draw_scenario(axL,
              "(а) Cheapest insertion: выбирается $B$ ($\\Delta=0.8$ минимален)",
              "B", (6.5, 6.5), "#1abc9c")

# Right: Regret-2 — choose B but for different reason
# Actually under regret-2, both are inserted but in order of decreasing regret:
# B first (regret 3.8 > 1.3) because it has "no good alternative"
draw_scenario(axR,
              "(б) Regret-2: первым вставляется $B$ (max regret $=3.8$)\n"
              "— у $B$ только одна хорошая позиция, $A$ можно отложить",
              "B", (6.5, 6.5), "#8e44ad")

fig.suptitle("Стратегии repair-оператора в ALNS:\n"
             "$\\Delta_R$ — стоимость вставки в~маршрут $R$; "
             "$\\mathrm{regret}=\\Delta_{2nd}-\\Delta_{1st}$",
             fontsize=13.5)
plt.tight_layout()
plt.savefig(OUT / "fig_regret2_demo.png", dpi=200, bbox_inches="tight")
plt.close()


# ============================================================
# Figure: Polar sweep seed-strategy
# ============================================================
print("[fig] Polar sweep demo")
rng = np.random.default_rng(42)
n_pts = 60
pts = rng.uniform(-5, 5, size=(n_pts, 2))
depot = np.array([0.0, 0.0])
m = 4

# Sort by polar angle around depot
angles = np.arctan2(pts[:, 1], pts[:, 0])
order = np.argsort(angles)
sorted_pts = pts[order]
chunks = np.array_split(np.arange(n_pts), m)
palette = ["#3498db", "#e74c3c", "#27ae60", "#9b59b6"]

fig, (axL, axR) = plt.subplots(1, 2, figsize=(12, 6), dpi=130)

# === Left: raw input + polar angles ===
axL.scatter(pts[:, 0], pts[:, 1], s=80, c="#85c1e2",
            edgecolors="black", linewidths=0.5, zorder=3)
axL.scatter(*depot, s=320, c="#f4d03f", marker="*",
            edgecolors="black", linewidths=1.0, zorder=5, label="депо")
# Draw radial lines from depot to each point (light grey)
for p in pts:
    axL.plot([depot[0], p[0]], [depot[1], p[1]],
             color="#bbb", linewidth=0.4, alpha=0.5, zorder=1)
# Polar axis decorations
for deg in [0, 90, 180, 270]:
    a = math.radians(deg)
    axL.plot([0, 5.5 * math.cos(a)], [0, 5.5 * math.sin(a)],
             color="#666", linewidth=0.6, linestyle=":", alpha=0.7)
    axL.text(5.8 * math.cos(a), 5.8 * math.sin(a),
             f"${deg}^{{\\circ}}$", fontsize=10, color="#444",
             ha="center", va="center")
axL.set_title("(а) Вход: $n$ клиентов и общий депот\n"
              "(полярные углы $\\theta_i$ вокруг депота)",
              fontsize=12)
axL.legend(loc="upper right", fontsize=10.5, frameon=True, framealpha=0.95)
axL.set_xlim(-6.5, 6.5); axL.set_ylim(-6.5, 6.5)
clean_axes(axL)

# === Right: sectored split ===
# Draw sector wedges in background
for ri, chunk in enumerate(chunks):
    chunk_pts = sorted_pts[chunk]
    chunk_angles = np.arctan2(chunk_pts[:, 1], chunk_pts[:, 0])
    a_min = math.degrees(chunk_angles.min())
    a_max = math.degrees(chunk_angles.max())
    # Add tiny margin to make wedge clearly visible
    wedge = Wedge(depot, 6.2, a_min - 1, a_max + 1,
                  facecolor=palette[ri], alpha=0.15, zorder=1)
    axR.add_patch(wedge)

# Draw routes (depot -> swept customers -> depot)
for ri, chunk in enumerate(chunks):
    chunk_pts = sorted_pts[chunk]
    coords = np.vstack([depot, chunk_pts, depot])
    axR.plot(coords[:, 0], coords[:, 1], color=palette[ri],
             linewidth=2.0, marker="o", markersize=6,
             markerfacecolor=palette[ri], markeredgecolor="black",
             markeredgewidth=0.5, alpha=0.9, zorder=3,
             label=f"маршрут $R_{ri+1}$ ({len(chunk)} клиентов)")
axR.scatter(*depot, s=320, c="#f4d03f", marker="*",
            edgecolors="black", linewidths=1.0, zorder=5)
axR.set_title("(б) Polar sweep: $m=4$ сектора одинакового размера,\n"
              "каждый сектор $\\to$ отдельный маршрут (seed-стратегия v18)",
              fontsize=12)
axR.legend(loc="upper right", fontsize=10, frameon=True, framealpha=0.95,
           ncol=2, columnspacing=0.8)
axR.set_xlim(-6.5, 6.5); axR.set_ylim(-6.5, 6.5)
clean_axes(axR)

fig.suptitle("Polar sweep partitioning: сортировка по $\\theta$ + "
             "жадное разбиение на $m$ маршрутов",
             fontsize=13.5)
plt.tight_layout()
plt.savefig(OUT / "fig_polar_sweep_demo.png", dpi=200, bbox_inches="tight")
plt.close()

print("\nDone. New educational figures:")
for fname in ["fig_or_opt_demo.png", "fig_knn_candidate_demo.png",
              "fig_regret2_demo.png", "fig_polar_sweep_demo.png"]:
    p = OUT / fname
    if p.exists():
        print(f"  {fname:35s}  {p.stat().st_size // 1024} KB")
