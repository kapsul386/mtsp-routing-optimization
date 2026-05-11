"""Horizontal stacked-bar timeline showing how alns_depot2m allocates its time budget.

Source: solver source (`src/v21/minsum/minsum_solver_depot2m.cpp`) — fixed time-budget
fractions; see Appendix A.2 of the report.
"""
from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt

OUT = Path(__file__).resolve().parent.parent / "docs" / "report" / "fig_depot2m_budget.png"

# Phase, share-of-budget %, color
PHASES = [
    ("seed",          1,  "#6c8ebf"),
    ("candidate-set", 8,  "#82b366"),
    ("pre-polish",    9,  "#d6b656"),
    ("ALNS-цикл",     86, "#b85450"),
    ("final 2-opt",   5,  "#9673a6"),
]
POST_COLOR = "#7f7f7f"

fig, ax = plt.subplots(figsize=(11.0, 3.9))

bar_y = 0.0
bar_h = 0.45

# --- main bar ---
left = 0.0
total = sum(p[1] for p in PHASES)  # 109
for name, share, color in PHASES:
    ax.barh(bar_y, share, height=bar_h, left=left,
            color=color, edgecolor="white", linewidth=1.6)
    cx = left + share / 2
    # Internal % label only fits in the 86% and 9% segments comfortably
    if share >= 8:
        ax.text(cx, bar_y, f"{share}%", ha="center", va="center",
                fontsize=10.5, color="white", fontweight="bold")
    left += share

main_end = left

# --- post phase (out of budget) ---
gap = 4
post_width = 22
post_x0 = main_end + gap
ax.barh(bar_y, post_width, height=bar_h, left=post_x0,
        color=POST_COLOR, edgecolor="white", linewidth=1.6, hatch="///", alpha=0.85)
ax.text(post_x0 + post_width / 2, bar_y, "≤ 30 с",
        ha="center", va="center", fontsize=10.5, color="white", fontweight="bold")

# --- phase labels ---
label_y = bar_y - bar_h / 2 - 0.18

cum = 0
centers = []
for name, share, _ in PHASES:
    centers.append(cum + share / 2)
    cum += share

# Only the largest segment (ALNS-цикл, 86%) gets a direct label below.
# Everything else uses leader-lines above the bar to avoid overlap.
ALNS_IDX = 3

# Wide-segment label below
name_alns, share_alns, _ = PHASES[ALNS_IDX]
ax.text(centers[ALNS_IDX], label_y, name_alns,
        ha="center", va="top", fontsize=10)

# Stagger offsets for the small-segment leader labels above the bar
above_offsets = {
    "seed":          0.78,
    "candidate-set": 0.46,
    "pre-polish":    0.18,
    "final 2-opt":   0.46,
}

for (name, share, _), cx in zip(PHASES, centers):
    if name == name_alns:
        continue
    yoff = above_offsets[name]
    ax.annotate(
        f"{name}  {share}%",
        xy=(cx, bar_y + bar_h / 2),
        xytext=(cx, bar_y + bar_h / 2 + yoff),
        ha="center", va="bottom",
        fontsize=9.5,
        arrowprops=dict(arrowstyle="-", color="#666666", lw=0.6,
                        shrinkA=0, shrinkB=2),
    )

ax.text(post_x0 + post_width / 2, label_y,
        "пост-фаза\nперебалансировки",
        ha="center", va="top", fontsize=10)

# --- bracket annotations on top ---
brace_y = bar_y + bar_h / 2 + 1.25
# main-budget brace
ax.plot([0, main_end], [brace_y, brace_y], color="#222222", lw=1.0)
ax.plot([0, 0], [brace_y - 0.05, brace_y + 0.05], color="#222222", lw=1.0)
ax.plot([main_end, main_end], [brace_y - 0.05, brace_y + 0.05], color="#222222", lw=1.0)
ax.text(main_end / 2, brace_y + 0.10,
        "основной бюджет (--time-budget-ms)",
        ha="center", va="bottom", fontsize=10.5, fontweight="bold")

# post brace
ax.plot([post_x0, post_x0 + post_width], [brace_y, brace_y],
        color="#222222", lw=1.0)
ax.plot([post_x0, post_x0], [brace_y - 0.05, brace_y + 0.05],
        color="#222222", lw=1.0)
ax.plot([post_x0 + post_width, post_x0 + post_width],
        [brace_y - 0.05, brace_y + 0.05], color="#222222", lw=1.0)
ax.text(post_x0 + post_width / 2, brace_y + 0.10,
        "вне бюджета (rebalance_post_ms)",
        ha="center", va="bottom", fontsize=10.5, fontweight="bold")

# --- footnote ---
ax.text(
    0.0, label_y - 0.55,
    "Сумма долей основного бюджета  =  1 + 8 + 9 + 86 + 5  =  109 %: "
    "pre-polish-фаза частично пересекается с ALNS-циклом\nпо логике solver-а; "
    "значения по умолчанию из minsum_solver_depot2m.cpp.",
    ha="left", va="top",
    fontsize=8.7, color="#444444",
)

# --- axes ---
ax.set_xlim(-2, post_x0 + post_width + 3)
ax.set_ylim(label_y - 1.0, brace_y + 0.45)
ax.set_xticks([])
ax.set_yticks([])
for spine in ax.spines.values():
    spine.set_visible(False)

plt.tight_layout()
fig.savefig(OUT, dpi=180, bbox_inches="tight")
print(f"saved: {OUT}")
