"""Regenerate fig_regret2_demo.png.

Fixes for the previous version:
  * no suptitle (the caption in main.tex already explains the figure);
  * "вставить ->" arrow no longer overlaps the Delta-annotation box;
  * cost-boxes moved to the corners that do not collide with the
    route polygons or the legend;
  * the chosen client B is highlighted with a coloured halo on both
    panels so the eye instantly sees what was picked.
"""
from __future__ import annotations
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch
import numpy as np

OUT = Path(__file__).resolve().parent.parent / "fig_regret2_demo.png"

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 12,
    "axes.titlesize": 12,
    "legend.fontsize": 10,
    "figure.dpi": 150,
})

# ---------------------------------------------------------------------------
# Scenario
# ---------------------------------------------------------------------------
depot = np.array([4.5, 4.5])
r1 = np.array([
    depot, [1.5, 2.0], [2.0, 3.5], [1.2, 4.8], [2.8, 5.2], depot,
])
r2 = np.array([
    depot, [6.5, 6.5], [7.8, 5.0], [7.5, 3.0], depot,
])
# Two unassigned customers
A = np.array([3.5, 2.4])  # similar costs in both routes -> small regret
B = np.array([5.6, 7.1])  # one obviously cheaper route -> large regret

# Best-insertion positions (target points on the route polygons)
INSERT_B = np.array([6.5, 6.5])  # into route R2 (best Δ=0.8)


def clean_axes(ax):
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(True)
    ax.set_xticks([])
    ax.set_yticks([])


def draw_scenario(ax, title, highlight_color):
    # Routes
    ax.plot(r1[:, 0], r1[:, 1], color="#e74c3c", linewidth=2.2,
            marker="o", markersize=7, markerfacecolor="#fadbd8",
            markeredgecolor="black", markeredgewidth=0.6, zorder=3,
            label="маршрут $R_1$")
    ax.plot(r2[:, 0], r2[:, 1], color="#3498db", linewidth=2.2,
            marker="s", markersize=7, markerfacecolor="#d6eaf8",
            markeredgecolor="black", markeredgewidth=0.6, zorder=3,
            label="маршрут $R_2$")
    # Depot
    ax.scatter(*depot, s=320, c="#f4d03f", marker="*",
               edgecolors="black", linewidths=1.0, zorder=5, label="депо")

    # Halo behind B (chosen)
    ax.scatter(*B, s=520, c="none", edgecolors=highlight_color,
               linewidths=2.6, zorder=3.5)

    # Unassigned customers
    for p, name, label_offset in [(A, "A", (10, -4)), (B, "B", (10, 6))]:
        ax.scatter(*p, s=210, c="#f7dc6f", edgecolors="black",
                   linewidths=1.0, marker="D", zorder=4)
        ax.annotate(name, p, xytext=label_offset, textcoords="offset points",
                    fontsize=14, fontweight="bold", color="#7d6608")

    # Cost annotations — placed in corners that don't collide with the routes.
    cost_box = dict(boxstyle="round,pad=0.3", fc="#ffffff",
                    ec="#888", lw=0.7)
    # A: below-right, away from the red route on the left
    ax.annotate("$\\Delta_{R_1}=1.2$\n$\\Delta_{R_2}=2.5$\n"
                "$\\mathrm{regret}=1.3$",
                A, xytext=(40, -8), textcoords="offset points",
                fontsize=9.5, bbox=cost_box, ha="left",
                arrowprops=dict(arrowstyle="-", color="#777", lw=0.6))
    # B: upper-right, well clear of the routes and of the arrow target
    ax.annotate("$\\Delta_{R_2}=0.8$\n$\\Delta_{R_1}=4.6$\n"
                "$\\mathrm{regret}=3.8$",
                B, xytext=(35, 20), textcoords="offset points",
                fontsize=9.5, bbox=cost_box, ha="left",
                arrowprops=dict(arrowstyle="-", color="#777", lw=0.6))

    # Insertion arrow B -> route R2 (dashed, coloured by panel)
    arrow_start = B + np.array([-0.15, -0.25])  # offset so it doesn't sit on the diamond
    arrow_end = INSERT_B + np.array([0.15, 0.15])
    arrow = FancyArrowPatch(arrow_start, arrow_end,
                            arrowstyle="-|>", mutation_scale=18,
                            color=highlight_color, linewidth=2.0,
                            linestyle=(0, (4, 2)), zorder=6)
    ax.add_patch(arrow)
    # Label sits *along* the arrow midpoint, slightly offset to the left
    mid = 0.55 * arrow_start + 0.45 * arrow_end
    ax.text(mid[0] - 0.35, mid[1] + 0.15, "вставить",
            fontsize=10.5, color=highlight_color, fontweight="bold",
            ha="right", zorder=7,
            bbox=dict(boxstyle="round,pad=0.18", fc="white", ec="none",
                      alpha=0.85))

    ax.set_title(title, fontsize=11.5)
    ax.set_xlim(0, 10.5)
    ax.set_ylim(1.0, 9.0)
    ax.legend(loc="lower right", fontsize=9.5, frameon=True, framealpha=0.95)
    clean_axes(ax)


fig, (axL, axR) = plt.subplots(1, 2, figsize=(12.5, 5.0))

draw_scenario(axL,
              "(а) Cheapest insertion: выбирается $B$ "
              "($\\Delta_{R_2}=0.8$ минимально)",
              "#1abc9c")

draw_scenario(axR,
              "(б) Regret-2: первым вставляется $B$ "
              "(regret$=3.8$, $A$ откладывается)",
              "#8e44ad")

plt.tight_layout()
plt.savefig(OUT, dpi=200, bbox_inches="tight")
print(f"Saved: {OUT}")
