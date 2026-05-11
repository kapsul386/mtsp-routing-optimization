"""Regenerate 2-opt and Or-opt demo figures with clean textbook geometry.

The previous versions had geometry that didn't visibly demonstrate the operator.
This version uses canonical layouts:
- 2-opt: hexagonal layout. Bad tour visits vertices out of order causing one
  obvious crossing. 2-opt reverses the segment between the crossing edges
  and yields the clean convex-hull tour.
- Or-opt: linear route with one vertex out of place. Moving it to its natural
  position eliminates a long detour.
"""
from __future__ import annotations
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyArrowPatch
import numpy as np

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "data" / "results" / "latex_report"

plt.rcParams.update({
    "font.size": 12,
    "axes.titlesize": 13,
    "axes.labelsize": 12,
    "legend.fontsize": 10.5,
    "figure.dpi": 130,
})


def clean_axes(ax):
    for sp in ax.spines.values():
        sp.set_color("#999")
        sp.set_linewidth(0.8)
    ax.set_aspect("equal")
    ax.grid(False)
    ax.set_xticks([])
    ax.set_yticks([])


# ============================================================
# Figure 1: 2-opt — convex-hull repair
# ============================================================
print("[fig] 2-opt demo (v3)")

# Hexagonal layout — convex hull tour is v1->v2->v3->v4->v5->v6->v1.
# Bad tour visits v3 and v4 swapped: v1->v2->v4->v3->v5->v6->v1.
# This introduces ONE crossing between (v2,v4) and (v3,v5).
# 2-opt reverses the segment [v4, v3] -> [v3, v4] giving back the hull tour.
verts = np.array([
    [1.0, 2.0],   # v1  (lower-left)
    [2.5, 4.5],   # v2  (upper-left)
    [5.5, 4.5],   # v3  (upper-right)
    [7.0, 2.0],   # v4  (right)
    [5.5, -0.5],  # v5  (lower-right)
    [2.5, -0.5],  # v6  (lower)
])
labels = [f"$v_{{{i+1}}}$" for i in range(len(verts))]

fig, (axL, axR) = plt.subplots(1, 2, figsize=(12, 5.6), dpi=130)

# ---- Left panel: BEFORE 2-opt ----
# Bad tour: v1 -> v2 -> v4 -> v3 -> v5 -> v6 -> v1
order_before = [0, 1, 3, 2, 4, 5]
for i in range(len(order_before)):
    a = order_before[i]
    b = order_before[(i + 1) % len(order_before)]
    axL.plot([verts[a, 0], verts[b, 0]],
             [verts[a, 1], verts[b, 1]],
             color="#3498db", linewidth=2.0, alpha=0.85, zorder=2)

# Highlight the two crossing edges: (v2, v4) and (v3, v5)
removed = [(1, 3), (2, 4)]
edge_colors = ["#e74c3c", "#e67e22"]
for (a, b), col in zip(removed, edge_colors):
    axL.plot([verts[a, 0], verts[b, 0]],
             [verts[a, 1], verts[b, 1]],
             color=col, linewidth=3.8, alpha=0.95, zorder=3)

axL.scatter(verts[:, 0], verts[:, 1], s=220, c="#85c1e2",
            edgecolors="black", linewidths=1.2, zorder=5)
for (x, y), lbl in zip(verts, labels):
    axL.annotate(lbl, (x, y), xytext=(0, 16), textcoords="offset points",
                 fontsize=13.5, ha="center", fontweight="bold")

red_patch = mpatches.Patch(color="#e74c3c", label="ребро $(v_2,v_4)$")
orange_patch = mpatches.Patch(color="#e67e22", label="ребро $(v_3,v_5)$")
axL.legend(handles=[red_patch, orange_patch], loc="lower center",
           bbox_to_anchor=(0.5, -0.02),
           fontsize=10.5, frameon=True, framealpha=0.95, ncol=2)

axL.set_title("(а) До 2-opt: тур $v_1\\!\\to\\!v_2\\!\\to\\!v_4\\!\\to\\!v_3"
              "\\!\\to\\!v_5\\!\\to\\!v_6\\!\\to\\!v_1$ с пересечением",
              fontsize=11.5)
axL.set_xlim(-0.5, 8.5)
axL.set_ylim(-1.8, 6.0)
clean_axes(axL)

# ---- Right panel: AFTER 2-opt ----
# Good tour: v1 -> v2 -> v3 -> v4 -> v5 -> v6 -> v1
order_after = [0, 1, 2, 3, 4, 5]
for i in range(len(order_after)):
    a = order_after[i]
    b = order_after[(i + 1) % len(order_after)]
    axR.plot([verts[a, 0], verts[b, 0]],
             [verts[a, 1], verts[b, 1]],
             color="#3498db", linewidth=2.0, alpha=0.85, zorder=2)

# Highlight new edges: (v2, v3) and (v4, v5)
new_edges = [(1, 2), (3, 4)]
new_colors = ["#27ae60", "#229954"]
for (a, b), col in zip(new_edges, new_colors):
    axR.plot([verts[a, 0], verts[b, 0]],
             [verts[a, 1], verts[b, 1]],
             color=col, linewidth=3.8, alpha=0.95, zorder=3)

# Show the reversal: the segment [v4, v3] in the old tour became [v3, v4].
# Mark the reversed segment with a small curved arrow above it.
seg_start = verts[2]   # v3
seg_end = verts[3]     # v4
mid = (seg_start + seg_end) / 2
arrow = FancyArrowPatch((mid[0] - 0.4, mid[1] + 1.1),
                        (mid[0] + 0.4, mid[1] + 1.1),
                        connectionstyle="arc3,rad=-0.5",
                        arrowstyle="<->", mutation_scale=18,
                        color="#7f8c8d", linewidth=1.6, zorder=4)
axR.add_patch(arrow)
axR.text(mid[0], mid[1] + 1.6, "реверс",
         fontsize=10.5, color="#555", fontweight="bold", ha="center")

axR.scatter(verts[:, 0], verts[:, 1], s=220, c="#abebc6",
            edgecolors="black", linewidths=1.2, zorder=5)
for (x, y), lbl in zip(verts, labels):
    axR.annotate(lbl, (x, y), xytext=(0, 16), textcoords="offset points",
                 fontsize=13.5, ha="center", fontweight="bold")

g_patch1 = mpatches.Patch(color="#27ae60", label="новое ребро $(v_2,v_3)$")
g_patch2 = mpatches.Patch(color="#229954", label="новое ребро $(v_4,v_5)$")
axR.legend(handles=[g_patch1, g_patch2], loc="lower center",
           bbox_to_anchor=(0.5, -0.02),
           fontsize=10.5, frameon=True, framealpha=0.95, ncol=2)

axR.set_title("(б) После 2-opt: сегмент $[v_4,v_3]$ перевёрнут, пересечение устранено",
              fontsize=11.5)
axR.set_xlim(-0.5, 8.5)
axR.set_ylim(-1.8, 6.0)
clean_axes(axR)

fig.suptitle("Принцип работы 2-opt: удаление двух пересекающихся рёбер "
             "и реверс сегмента между ними",
             fontsize=13.5)
plt.tight_layout()
plt.savefig(OUT / "fig_2opt_demo.png", dpi=200, bbox_inches="tight")
plt.close()
print(f"  saved: {OUT / 'fig_2opt_demo.png'}")


# ============================================================
# Figure 2: Or-opt — move a segment to a better position
# ============================================================
print("[fig] Or-opt demo (v3)")

# 8 customers arranged roughly along a horizontal line.
# v3 is at the far right (close to v7) but in the tour it is visited 3rd —
# this forces a long back-and-forth (v2 -> v3 -> v4). Or-opt moves v3 to
# its natural position between v7 and v8, eliminating the detour.
verts = np.array([
    [0.8, 1.5],   # v1
    [2.0, 1.7],   # v2
    [6.6, 2.2],   # v3 — visited out of order (geometrically near v7/v8)
    [3.0, 1.6],   # v4
    [4.0, 1.7],   # v5
    [5.0, 1.6],   # v6
    [5.9, 1.8],   # v7
    [7.6, 1.6],   # v8
])
labels = [f"$v_{{{i+1}}}$" for i in range(len(verts))]

fig, (axL, axR) = plt.subplots(1, 2, figsize=(12, 5.0), dpi=130)

# ---- Left panel: BEFORE Or-opt ----
order_before = [0, 1, 2, 3, 4, 5, 6, 7]
for i in range(len(order_before)):
    a = order_before[i]
    b = order_before[(i + 1) % len(order_before)]
    axL.plot([verts[a, 0], verts[b, 0]],
             [verts[a, 1], verts[b, 1]],
             color="#3498db", linewidth=2.0, alpha=0.85, zorder=2)

# Highlight segment {v3} in red — single-vertex segment (Or-opt with k=1)
seg_idx = [2]

# Highlight edges that will be removed: (v2,v3), (v3,v4), (v7,v8)
to_remove = [(1, 2), (2, 3), (6, 7)]
for a, b in to_remove:
    axL.plot([verts[a, 0], verts[b, 0]],
             [verts[a, 1], verts[b, 1]],
             color="#c0392b", linewidth=2.6, linestyle=":",
             zorder=2.5, alpha=0.9)

axL.scatter(verts[:, 0], verts[:, 1], s=190, c="#85c1e2",
            edgecolors="black", linewidths=1.1, zorder=5)
axL.scatter(verts[seg_idx, 0], verts[seg_idx, 1], s=240, c="#e74c3c",
            edgecolors="black", linewidths=1.3, zorder=6)
for (x, y), lbl in zip(verts, labels):
    axL.annotate(lbl, (x, y), xytext=(0, 15), textcoords="offset points",
                 fontsize=12.5, ha="center", fontweight="bold")

seg_patch = mpatches.Patch(color="#e74c3c", label="перемещаемый клиент $v_3$")
rem_patch = mpatches.Patch(color="#c0392b",
                           label="удаляемые рёбра $(v_2,v_3),(v_3,v_4),(v_7,v_8)$")
axL.legend(handles=[seg_patch, rem_patch], loc="lower right",
           fontsize=10, frameon=True, framealpha=0.95)

axL.set_title("(а) До Or-opt: $v_3$ создаёт длинный детур из основного маршрута",
              fontsize=11.5)
axL.set_xlim(0, 9.0)
axL.set_ylim(0.3, 3.6)
clean_axes(axL)

# ---- Right panel: AFTER Or-opt ----
# New tour: v1 -> v2 -> v4 -> v5 -> v6 -> v7 -> v3 -> v8 -> v1
order_after = [0, 1, 3, 4, 5, 6, 2, 7]
for i in range(len(order_after)):
    a = order_after[i]
    b = order_after[(i + 1) % len(order_after)]
    axR.plot([verts[a, 0], verts[b, 0]],
             [verts[a, 1], verts[b, 1]],
             color="#3498db", linewidth=2.0, alpha=0.85, zorder=2)

# Highlight new edges: (v2,v4), (v7,v3), (v3,v8)
new_edges = [(1, 3), (6, 2), (2, 7)]
for a, b in new_edges:
    axR.plot([verts[a, 0], verts[b, 0]],
             [verts[a, 1], verts[b, 1]],
             color="#27ae60", linewidth=2.9, zorder=2.5, alpha=0.9)

# Curved arrow showing the move from old position (visited 3rd) to new
# position (between v7 and v8) — visually emphasises the reordering.
arrow = FancyArrowPatch((6.6, 2.5), (6.9, 1.9),
                        connectionstyle="arc3,rad=0.5",
                        arrowstyle="-|>", mutation_scale=22,
                        color="#8e44ad", linewidth=2.2, zorder=6)
axR.add_patch(arrow)
axR.text(6.9, 2.9, "перенос в новую\nпозицию",
         fontsize=10.5, color="#8e44ad",
         fontweight="bold", ha="center")

axR.scatter(verts[:, 0], verts[:, 1], s=190, c="#abebc6",
            edgecolors="black", linewidths=1.1, zorder=5)
axR.scatter(verts[seg_idx, 0], verts[seg_idx, 1], s=240, c="#e74c3c",
            edgecolors="black", linewidths=1.3, zorder=6)
for (x, y), lbl in zip(verts, labels):
    axR.annotate(lbl, (x, y), xytext=(0, 15), textcoords="offset points",
                 fontsize=12.5, ha="center", fontweight="bold")

new_patch = mpatches.Patch(color="#27ae60",
                           label="новые рёбра $(v_2,v_4),(v_7,v_3),(v_3,v_8)$")
axR.legend(handles=[new_patch], loc="lower right",
           fontsize=10, frameon=True, framealpha=0.95)

axR.set_title("(б) После Or-opt: $v_3$ вставлен между $v_7$ и $v_8$, детур устранён",
              fontsize=11.5)
axR.set_xlim(0, 9.0)
axR.set_ylim(0.3, 3.6)
clean_axes(axR)

fig.suptitle("Принцип работы Or-opt: связный сегмент длины $k\\in\\{1,2,3\\}$ "
             "удаляется и вставляется в новую позицию маршрута",
             fontsize=13.5)
plt.tight_layout()
plt.savefig(OUT / "fig_or_opt_demo.png", dpi=200, bbox_inches="tight")
plt.close()
print(f"  saved: {OUT / 'fig_or_opt_demo.png'}")

print("\nDone.")
