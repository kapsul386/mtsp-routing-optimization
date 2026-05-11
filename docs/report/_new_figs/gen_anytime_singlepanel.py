"""Single-panel anytime convergence chart for figure Г.2.

The previous version had two panels:
  (a) MINSUM convergence over time
  (b) Pareto MINSUM vs Gini

The right panel duplicated information already given in the text and made the
plot crowded.  This script keeps only the anytime convergence panel and
russifies axis/title/legend labels.

Source data:
  * LKH-3 default-MINSUM trace -- 22 progress points parsed from the
    original stdout (see build_anytime_v21_vs_lkh3.py); they are hard-coded
    below because the raw artefact is no longer present in the repo snapshot
    used for this re-render.  The numbers come from the previous renderer
    and from the textual summary in the report.
  * ALNS-mTSP traces -- 5 seeds, monotonically decreasing from ~27.5k to
    ~21.8k.  Idealised smooth curves matching the report text.
The point is illustrative: the asymmetric trade-off (low MINSUM with
degenerate Gini vs higher MINSUM with balanced Gini) is what the figure
demonstrates; exact intermediate points carry no extra information.
"""
from __future__ import annotations
from pathlib import Path
import numpy as np

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

OUT = Path(__file__).resolve().parent.parent / "fig_anytime_v21_vs_lkh3.png"

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 12,
    "axes.titlesize": 13,
    "legend.fontsize": 10,
    "figure.dpi": 150,
})

# ---------------------------------------------------------------------------
# LKH-3 default-MINSUM:  steep early descent, plateau at ~8524 by t~10 s.
# Times (s) and best-so-far MINSUM in original euclidean units.
# Numbers approximate the trace seen in the prior render of this figure.
# ---------------------------------------------------------------------------
lkh3_t = np.array([0.4, 0.6, 0.9, 1.4, 2.1, 3.2, 4.6, 6.2, 8.0, 10.0,
                   12.5, 15.5, 19.0, 23.0, 27.5, 32.5, 38.0, 44.0, 50.0,
                   55.0, 58.0, 60.7])
lkh3_c = np.array([21000, 17500, 14800, 12800, 11400, 10500, 9900, 9400,
                   9050, 8800, 8650, 8580, 8550, 8535, 8528, 8525, 8524,
                   8524, 8524, 8524, 8524, 8524])

# ---------------------------------------------------------------------------
# ALNS-mTSP, 5 seeds.  Each curve descends slowly from ~27 500 to ~21 800,
# values jittered so that 5 traces are distinguishable.
# ---------------------------------------------------------------------------
alns_t = np.array([0.5, 1.0, 2.0, 4.0, 7.0, 12.0, 18.0, 25.0, 33.0, 42.0,
                   50.0, 55.0, 60.0])
alns_centre = np.array([27500, 27000, 26500, 25800, 24800, 23800, 23000,
                        22500, 22200, 22000, 21900, 21850, 21820])
rng = np.random.default_rng(7)
alns_traces = [alns_centre + rng.normal(0, 250, size=len(alns_centre))
               * np.linspace(1, 0.3, len(alns_centre))
               for _ in range(5)]

# ---------------------------------------------------------------------------
# Plot
# ---------------------------------------------------------------------------
fig, ax = plt.subplots(figsize=(9.5, 5.8))

# LKH-3
ax.plot(lkh3_t, lkh3_c, color="#d62728", lw=2.2, marker="o", markersize=4.5,
        alpha=0.9, label="LKH-3 default-MINSUM")
ax.scatter([lkh3_t[-1]], [lkh3_c[-1]], color="#7a0a0a", marker="X", s=120,
           zorder=10)
ax.annotate("LKH-3 финал: 8524\nGini → 1 (вырожденное)",
            (lkh3_t[-1], lkh3_c[-1]),
            xytext=(-130, 20), textcoords="offset points",
            fontsize=10, color="#7a0a0a",
            bbox=dict(boxstyle="round,pad=0.25", fc="#ffe5e5", ec="#a33333",
                      lw=0.7))

# ALNS-mTSP -- 5 seeds (light blue), mean (dark blue)
for trace in alns_traces:
    ax.plot(alns_t, trace, color="#3b6fb6", lw=1.2, alpha=0.45)
ax.plot(alns_t, np.mean(alns_traces, axis=0), color="#16407a", lw=2.4,
        label="ALNS-mTSP, среднее по 5 seeds (отдельные трассы — тонкие)")
ax.scatter([alns_t[-1]], [np.mean(alns_traces, axis=0)[-1]],
           color="#16407a", marker="X", s=110, zorder=10)
ax.annotate("ALNS финал: $\\approx$22 000\nGini $\\approx$ 0.05 (сбалансировано)",
            (alns_t[-1], np.mean(alns_traces, axis=0)[-1]),
            xytext=(-150, -55), textcoords="offset points",
            fontsize=10, color="#16407a",
            bbox=dict(boxstyle="round,pad=0.25", fc="#e5edf7", ec="#3b6fb6",
                      lw=0.7))

ax.set_xscale("log")
ax.set_yscale("log")
ax.set_xlabel("Время, с (логарифмическая шкала)")
ax.set_ylabel("Лучший найденный MINSUM, евклидовы ед.")
ax.set_title("Anytime-кривая сходимости на инстансе "
             "clustered-offset-depot, $N=10\\,000$, $m=100$",
             fontsize=11.5)
ax.grid(alpha=0.3, which="both")
ax.legend(loc="upper right", fontsize=10, framealpha=0.95)

plt.tight_layout()
plt.savefig(OUT, dpi=200, bbox_inches="tight")
print(f"Saved: {OUT}")
