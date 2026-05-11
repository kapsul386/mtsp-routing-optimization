"""Render a 2x3 catalogue of the six geometric instance families used in the report."""
from __future__ import annotations

import random
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "data" / "mtsp" / "generated_multifamily"
OUT = ROOT / "docs" / "report" / "fig_instance_families.png"

# (display title, filename, optional subsample size)
FAMILIES = [
    ("uniform",              "uniform_n500_m5_r01.txt",                 None),
    ("clustered-center",     "clustered-center_n500_m5_r01.txt",        None),
    ("clustered-offset-depot", "clustered-offset-depot_n500_m5_r01.txt", None),
    ("mixed-outliers",       "mixed-outliers_n10000_m10_r01.txt",       500),
    ("outliers",             "outliers_n500_m5_r01.txt",                None),
    ("high-m-stress",        "high-m-stress_n500_m5_r01.txt",           None),
]

SUBTITLES = {
    "uniform":                "равномерное распределение",
    "clustered-center":       "кластеры около центра",
    "clustered-offset-depot": "кластеры, депо смещено к границе",
    "mixed-outliers":         "кластеры + доля outlier-точек",
    "outliers":               "плотное ядро + далёкие точки",
    "high-m-stress":          "геометрия под большие $m$",
}


def read_instance(path: Path) -> tuple[tuple[float, float], np.ndarray]:
    lines = path.read_text(encoding="utf-8").splitlines()
    n, _m = map(int, lines[0].split())
    coords = np.array([[float(x) for x in lines[i + 1].split()[:2]] for i in range(n)])
    depot = (coords[0, 0], coords[0, 1])
    clients = coords[1:]
    return depot, clients


fig, axes = plt.subplots(2, 3, figsize=(11.0, 7.2))

for ax, (title, fname, subsample) in zip(axes.flat, FAMILIES):
    depot, clients = read_instance(DATA / fname)
    if subsample is not None and len(clients) > subsample:
        rng = random.Random(20260511)
        idx = rng.sample(range(len(clients)), subsample)
        clients = clients[idx]

    ax.scatter(
        clients[:, 0], clients[:, 1],
        s=8, c="#3a7ca5", alpha=0.65, edgecolors="none",
    )
    ax.scatter(
        [depot[0]], [depot[1]],
        s=120, c="#c0392b", marker="*", edgecolors="black", linewidths=0.7,
        zorder=5, label="депо",
    )

    pad = 8.0
    xmin = min(clients[:, 0].min(), depot[0]) - pad
    xmax = max(clients[:, 0].max(), depot[0]) + pad
    ymin = min(clients[:, 1].min(), depot[1]) - pad
    ymax = max(clients[:, 1].max(), depot[1]) + pad
    side = max(xmax - xmin, ymax - ymin)
    cx, cy = (xmin + xmax) / 2, (ymin + ymax) / 2
    ax.set_xlim(cx - side / 2, cx + side / 2)
    ax.set_ylim(cy - side / 2, cy + side / 2)

    ax.set_aspect("equal")
    ax.set_xticks([])
    ax.set_yticks([])
    for spine in ax.spines.values():
        spine.set_color("#888888")
        spine.set_linewidth(0.7)

    ax.set_title(title, fontsize=11, fontweight="bold", pad=4)
    ax.text(
        0.5, -0.05, SUBTITLES[title],
        transform=ax.transAxes, ha="center", va="top",
        fontsize=9, color="#444444",
    )

axes[0, 0].legend(
    loc="upper right", fontsize=8, framealpha=0.92,
    handletextpad=0.4, borderpad=0.4,
)

plt.tight_layout(rect=(0, 0, 1, 0.97))
fig.savefig(OUT, dpi=180, bbox_inches="tight")
print(f"saved: {OUT}")
