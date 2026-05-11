"""
Figure: 4-panel head-to-head summary FILO2 vs ALNS-cap on uniform m=5
across {n=50K, n=100K} and two budgets.

Panels: MINSUM, max-route, imbalance, wall-time.
Source: data/results/audit/filo2_v21_largeN_20260430/summary_proper.txt
"""
import matplotlib.pyplot as plt
import matplotlib
import numpy as np
from pathlib import Path

matplotlib.rcParams["font.family"] = "DejaVu Sans"
matplotlib.rcParams["font.size"] = 9.5

OUT = Path(__file__).parent / "fig_filo2_vs_alns_h2h_n50k_n100k.png"

# Data from summary_proper.txt and README.md
# n=50K uniform m=5, budget 300s; n=100K uniform m=5, budget 600s
data = {
    ("n=50K, T=300с", "FILO2"):     dict(sum=4.181, makespan=1.204, imbal=1.44, t=300.1),
    ("n=50K, T=300с", "ALNS-cap"):  dict(sum=4.506, makespan=0.930, imbal=1.03, t=188.2),
    ("n=100K, T=600с", "FILO2"):    dict(sum=12.708, makespan=3.479, imbal=1.37, t=823.8),
    ("n=100K, T=600с", "ALNS-cap"): dict(sum=12.892, makespan=2.641, imbal=1.02, t=464.5),
}

scales = ["n=50K, T=300с", "n=100K, T=600с"]
solvers = ["FILO2", "ALNS-cap"]
metrics = [
    ("sum",      "MINSUM, $10^6$",                "↓ меньше=лучше"),
    ("makespan", "max-route (makespan), $10^6$",  "↓ меньше=лучше"),
    ("imbal",    "imbalance = max/avg",           "→ ближе к 1.0=лучше"),
    ("t",        "wall-clock, секунд",            "↓ меньше=лучше"),
]

fig, axes = plt.subplots(1, 4, figsize=(13.5, 3.6))
colors = {"FILO2": "#bcbd22", "ALNS-cap": "#1f77b4"}

x = np.arange(len(scales))
w = 0.36

for ax, (key, label, hint) in zip(axes, metrics):
    for i, solver in enumerate(solvers):
        vals = [data[(s, solver)][key] for s in scales]
        offs = (-w / 2) if i == 0 else (w / 2)
        bars = ax.bar(x + offs, vals, w, label=solver, color=colors[solver],
                      edgecolor="black", linewidth=0.6)
        for j, b in enumerate(bars):
            v = vals[j]
            if key == "imbal":
                txt = f"{v:.2f}"
            elif key == "t":
                txt = f"{v:.0f}"
            else:
                txt = f"{v:.2f}"
            ax.text(b.get_x() + b.get_width() / 2, b.get_height() * 1.02,
                    txt, ha="center", fontsize=8.5)
    if key == "imbal":
        ax.axhline(1.0, color="green", linestyle="--", alpha=0.5,
                   label="идеал = 1.0", linewidth=0.8)
    ax.set_xticks(x)
    ax.set_xticklabels(scales, fontsize=9)
    ax.set_ylabel(label)
    ax.set_title(hint, fontsize=9.5)
    ax.grid(axis="y", alpha=0.3, linestyle=":")
    ax.set_axisbelow(True)
    if key == "t":
        ax.legend(loc="upper left", fontsize=8.5)
    else:
        ax.legend(loc="lower right", fontsize=8.5)
    # padding на верх для подписей
    ymax = ax.get_ylim()[1]
    ax.set_ylim(0, ymax * 1.15)

fig.suptitle("Head-to-head ALNS-cap vs FILO2 на $N{\\in}\\{50\\,000;\\,100\\,000\\}$ "
             "uniform $m{=}5$ (3 seeds, средние). Источник: "
             "data/results/audit/filo2\\_v21\\_largeN\\_20260430/summary\\_proper.txt",
             fontsize=10.5, y=1.02)
plt.tight_layout()
plt.savefig(OUT, dpi=170, bbox_inches="tight")
print(f"Saved: {OUT}")
