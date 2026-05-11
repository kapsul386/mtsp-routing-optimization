"""Refresh 5 legacy figures with higher DPI and improved layout.

Outputs (overwrites in data/results/latex_report/):
  - fig_mtsp_problem.png       (DPI 200, larger labels)
  - fig1_pareto_n25k.png       (m=5/m=7 jitter so labels don't overlap)
  - fig6_pareto_dominance_n25k.png  (same fix + cleaner legend)
  - fig2_scaling_time.png      (larger figure, bigger fonts)
  - fig7_n_trivial_vs_n.png    (larger figure, bigger fonts, sharper markers)

Run:
    python refresh_legacy_figures.py

Data sources (all under data/results/):
  - stratum1_modular_n100_200_results_enriched.csv
  - stratum1_modular_n500_1000_results_enriched.csv
  - verify_n25000_results.csv
  - verify_n50000_v21_only_results.csv
"""
from __future__ import annotations
import csv, json, math
from pathlib import Path
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[4]
RESULTS = ROOT / "data" / "results"
OUT = ROOT / "data" / "results" / "latex_report"
csv.field_size_limit(10**8)

# ---------- shared style ----------
plt.rcParams.update({
    "font.size": 12,
    "axes.titlesize": 13,
    "axes.labelsize": 12,
    "xtick.labelsize": 11,
    "ytick.labelsize": 11,
    "legend.fontsize": 11,
    "figure.dpi": 130,
})

SOLVER_LABEL = {
    "lkh3-baseline": "LKH-3 (default)",
    "lkh3-baseline-minmax": "LKH-3 minmax",
    "lkh-wrapper-v21": "v21 (single-file)",
    "lkh_v21_minsum": "v21_minsum",
    "lkh_v21_minsum_cap": "v21_minsum_cap",
    "lkh_v21_minsum_depot2m_plus": "v21_depot2m_plus",
    "lkh_v21_minmax": "v21_minmax",
}
COL = {
    "lkh3-baseline": "#e74c3c", "lkh3-baseline-minmax": "#f39c12",
    "lkh-wrapper-v21": "#1abc9c", "lkh_v21_minsum": "#3498db",
    "lkh_v21_minsum_cap": "#2980b9", "lkh_v21_minsum_depot2m_plus": "#0d3d6e",
    "lkh_v21_minmax": "#e67e22",
}
MARK = {
    "lkh3-baseline": "D", "lkh3-baseline-minmax": "v",
    "lkh-wrapper-v21": "^", "lkh_v21_minsum": "o",
    "lkh_v21_minsum_cap": "s", "lkh_v21_minsum_depot2m_plus": "*",
    "lkh_v21_minmax": "P",
}


def style(ax):
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.grid(True, linestyle="--", alpha=0.35)


def load_rows(path):
    if not path.exists():
        print(f"[warn] missing: {path}")
        return []
    with path.open("r", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def f(row, key, default=None):
    v = row.get(key, "")
    if v in ("", None): return default
    try: return float(v)
    except: return default


def i(row, key, default=None):
    v = row.get(key, "")
    if v in ("", None): return default
    try: return int(v)
    except:
        try: return int(float(v))
        except: return default


def enrich_inline(rows):
    out = []
    for row in rows:
        if not row.get("valid") or row.get("valid") == "False":
            continue
        if row.get("balance_max_avg", "") in ("", None):
            try:
                routes = json.loads(row.get("routes", "[]"))
                path = ROOT / row["path"]
                if not path.exists():
                    out.append(row); continue
                with path.open("r", encoding="utf-8") as fh:
                    lines = [ln.strip() for ln in fh if ln.strip()]
                coords = [tuple(map(float, ln.split())) for ln in lines[1:]]
                lengths, empty, singletons = [], 0, 0
                for r in routes:
                    if len(r) <= 2:
                        empty += 1; lengths.append(0.0); continue
                    if len(r) == 3:
                        singletons += 1
                    lengths.append(sum(math.hypot(coords[a][0] - coords[b][0],
                                                  coords[a][1] - coords[b][1])
                                       for a, b in zip(r, r[1:])))
                nz = [l for l in lengths if l > 0]
                row = dict(row)
                row["sum_length"] = sum(lengths)
                row["max_length"] = max(lengths) if lengths else 0
                mean_l = sum(nz) / len(nz) if nz else 0
                row["balance_max_avg"] = max(lengths) / mean_l if mean_l > 0 else 0
                row["n_empty"] = empty
                row["n_singleton"] = singletons
            except Exception:
                pass
        out.append(row)
    return out


# ============================================================
# Load all data once
# ============================================================
s1_a = load_rows(RESULTS / "stratum1_modular_n100_200_results_enriched.csv")
s1_b = load_rows(RESULTS / "stratum1_modular_n500_1000_results_enriched.csv")
v25  = enrich_inline(load_rows(RESULTS / "verify_n25000_results.csv"))
v50  = enrich_inline(load_rows(RESULTS / "verify_n50000_v21_only_results.csv"))
all_rows = s1_a + s1_b + v25 + v50
print(f"[load] {len(all_rows)} valid rows; verify_n25k: {len(v25)}")


# ============================================================
# Helper: smart label placement for overlapping points
# ============================================================
def place_m_label(ax, x, y, m, used, fontsize=10, color="#444"):
    """Place an 'm=X' label next to (x, y), avoiding nearby labels.

    `used` is a dict mapping (rounded x_pos, rounded y_pos) -> count of labels there;
    each subsequent label at the same spot gets vertically offset.
    """
    key = (round(math.log10(max(x, 1e-9)), 1), round(y, -4))
    count = used.get(key, 0)
    used[key] = count + 1
    dx, dy = 8, 6 + count * 14   # stack downward to upward
    ax.annotate(f"m={m}", (x, y),
                textcoords="offset points", xytext=(dx, dy),
                fontsize=fontsize, color=color,
                bbox=dict(boxstyle="round,pad=0.2", fc="white", ec="none",
                          alpha=0.7))


# ============================================================
# Figure 1: Pareto sum vs time on N=25K — fixed labels
# ============================================================
print("[fig1] Pareto N=25K — m-label de-overlap")
fig, ax = plt.subplots(figsize=(9, 6), dpi=130)
n25k_rows = [r for r in v25 if i(r, "node_count") == 25000]
solver_pts = defaultdict(list)
for row in n25k_rows:
    s = row["solver"]
    sum_l = f(row, "sum_length")
    t = f(row, "time_seconds")
    if sum_l and t:
        solver_pts[s].append((t, sum_l, i(row, "salesman_count")))

label_used = {}
for s, pts in solver_pts.items():
    if not pts:
        continue
    xs, ys, ms = zip(*pts)
    ax.scatter(xs, ys, label=SOLVER_LABEL.get(s, s), s=200,
               c=COL.get(s, "black"), marker=MARK.get(s, "o"),
               edgecolors="black", linewidths=0.8, alpha=0.88, zorder=3)
    for x, y, m in pts:
        place_m_label(ax, x, y, m, label_used, fontsize=10)

ax.set_xscale("log")
ax.set_xlabel("Время работы, секунд (log-scale)")
ax.set_ylabel("Сумма длин маршрутов (sum)")
ax.set_title("Рисунок 1 — Pareto: качество vs время на N = 25 000\n"
             "(uniform, m ∈ {5, 7}, бюджет 150 с)")
ax.axvline(150, color="#888", linestyle=":", linewidth=1.2)
ax.text(150 * 1.06, ax.get_ylim()[0] + 0.02 * (ax.get_ylim()[1] - ax.get_ylim()[0]),
        "budget = 150 с", fontsize=10, color="#666",
        verticalalignment="bottom")
ax.legend(loc="upper left", fontsize=10, frameon=False)
style(ax)
plt.tight_layout()
plt.savefig(OUT / "fig1_pareto_n25k.png", dpi=200, bbox_inches="tight")
plt.close()


# ============================================================
# Figure 6: Pareto dominance N=25K — fixed labels + clean legend
# ============================================================
print("[fig6] Pareto-dominance N=25K — m-label de-overlap")
fig, ax = plt.subplots(figsize=(10, 6.5), dpi=130)
N_filter = 25000
plot_solvers = ["lkh3-baseline", "lkh-wrapper-v21", "lkh_v21_minsum",
                "lkh_v21_minsum_cap", "lkh_v21_minsum_depot2m_plus"]
points_for_pareto = []
seen_legend = set()
label_used = {}
for r in all_rows:
    if i(r, "node_count") != N_filter:
        continue
    s = r["solver"]
    if s not in plot_solvers:
        continue
    sl = f(r, "sum_length"); t = f(r, "time_seconds")
    bal = f(r, "balance_max_avg") or 1.0
    m = i(r, "salesman_count")
    if not (sl and t):
        continue
    base_label = f"{SOLVER_LABEL.get(s)} (bal={bal:.2f})"
    label = base_label if s not in seen_legend else None
    if label:
        seen_legend.add(s)
    ax.scatter(t, sl, s=180 + bal * 36,
               c=COL.get(s), marker=MARK.get(s, "o"),
               edgecolors="black", linewidths=0.8, alpha=0.85,
               label=label, zorder=3)
    place_m_label(ax, t, sl, m, label_used)
    points_for_pareto.append((t, sl, s))

# Pareto frontier
points_for_pareto.sort()
pareto, best_y = [], float("inf")
for t, sl, _ in points_for_pareto:
    if sl < best_y:
        pareto.append((t, sl))
        best_y = sl
if len(pareto) >= 2:
    px, py = zip(*pareto)
    ax.step(px, py, where="post", color="#27ae60",
            linestyle="--", alpha=0.7, linewidth=1.8,
            label="Pareto-frontier")

ax.set_xscale("log")
ax.axvline(150, color="#888", linestyle=":", linewidth=1.2)
ax.text(150 * 1.06, ax.get_ylim()[0] + 0.02 * (ax.get_ylim()[1] - ax.get_ylim()[0]),
        "budget = 150 с", fontsize=10, color="#666",
        verticalalignment="bottom")
ax.set_xlabel("Время работы, секунд (log)")
ax.set_ylabel("Сумма длин маршрутов")
ax.set_title("Рисунок 6 — Pareto-доминирование на N = 25 000 (uniform)\n"
             "Размер точки ∝ balance_max_avg; зелёный пунктир — Pareto-frontier")
ax.legend(loc="upper right", fontsize=10, frameon=False)
style(ax)
plt.tight_layout()
plt.savefig(OUT / "fig6_pareto_dominance_n25k.png", dpi=200, bbox_inches="tight")
plt.close()


# ============================================================
# Figure 2: Scaling time vs N — bigger fonts & lines
# ============================================================
print("[fig2] Scaling time vs N — refresh")
fig, ax = plt.subplots(figsize=(9.5, 6), dpi=130)
solver_xy = defaultdict(list)
for r in all_rows:
    s = r["solver"]; n = i(r, "node_count"); t = f(r, "time_seconds")
    if s and n and t and t > 0:
        solver_xy[s].append((n, t))

order = ["lkh3-baseline", "lkh3-baseline-minmax", "lkh-wrapper-v21",
         "lkh_v21_minsum", "lkh_v21_minsum_cap",
         "lkh_v21_minsum_depot2m_plus", "lkh_v21_minmax"]
for s in order:
    pts = solver_xy.get(s, [])
    if not pts:
        continue
    pts.sort()
    by_n = defaultdict(list)
    for n, t in pts:
        by_n[n].append(t)
    xs = sorted(by_n.keys())
    ys = [sum(by_n[n]) / len(by_n[n]) for n in xs]
    ax.plot(xs, ys, marker=MARK.get(s, "o"), color=COL.get(s, "black"),
            label=SOLVER_LABEL.get(s, s), linewidth=2.0, markersize=8.5,
            markeredgecolor="black", markeredgewidth=0.6)

# Budget line — interpolate by N
ns_budget = [100, 200, 500, 1000, 25000, 50000]
budgets   = [10,  10,  30,  30,   150,   250]
ax.plot(ns_budget, budgets, color="#2ecc71", linestyle="--",
        linewidth=2.0, label="заданный budget", zorder=2)

# Annotate the LKH-3 overrun visually
# (overrun arrow on N=1000 minmax and N=25K default)
ax.annotate("LKH-3 minmax:\n+22× от budget",
            xy=(1000, 668), xytext=(160, 1100),
            fontsize=10, color="#f39c12",
            arrowprops=dict(arrowstyle="->", color="#f39c12", lw=1.2))
ax.annotate("LKH-3 default:\n+10× от budget",
            xy=(25000, 1600), xytext=(4000, 5000),
            fontsize=10, color="#e74c3c",
            arrowprops=dict(arrowstyle="->", color="#e74c3c", lw=1.2))

ax.set_xscale("log"); ax.set_yscale("log")
ax.set_xlabel("Размер задачи N (log)")
ax.set_ylabel("Среднее время работы, секунд (log)")
ax.set_title("Рисунок 2 — Масштабирование времени работы по N\n"
             "(LKH-3 default нарушает budget при N ≥ 1000; "
             "v21_depot2m_plus имеет фикс. rebalance-post-overhead 30 с)")
ax.legend(loc="upper left", fontsize=9.5, frameon=False, ncol=2)
style(ax)
plt.tight_layout()
plt.savefig(OUT / "fig2_scaling_time.png", dpi=200, bbox_inches="tight")
plt.close()


# ============================================================
# Figure 7: n_trivial / m vs N — bigger fonts, log-N x-axis
# ============================================================
print("[fig7] n_trivial vs N — refresh")
fig, ax = plt.subplots(figsize=(10, 6), dpi=130)
target7 = ["lkh3-baseline", "lkh_v21_minsum", "lkh_v21_minsum_cap",
           "lkh_v21_minsum_depot2m_plus", "lkh-wrapper-v21"]
for s in target7:
    pts = []
    for r in all_rows:
        if r["solver"] != s:
            continue
        n = i(r, "node_count")
        ne = i(r, "n_empty", 0) or 0
        ns = i(r, "n_singleton", 0) or 0
        ntr = ne + ns
        m = i(r, "salesman_count")
        if n is not None and m and m > 0:
            pts.append((n, ntr / m * 100.0))
    if not pts:
        continue
    by_n = defaultdict(list)
    for n, p in pts:
        by_n[n].append(p)
    xs = sorted(by_n.keys())
    ys = [sum(by_n[n]) / len(by_n[n]) for n in xs]
    ax.plot(xs, ys, marker=MARK.get(s, "o"), color=COL.get(s, "black"),
            label=SOLVER_LABEL.get(s, s), linewidth=2.0, markersize=9,
            markeredgecolor="black", markeredgewidth=0.6)

ax.set_xscale("log")
ax.set_xlabel("Размер задачи N (log)")
ax.set_ylabel("Доля тривиальных маршрутов\n(n_empty + n_singleton) / m, %")
ax.set_title("Рисунок 7 — Доля тривиальных маршрутов как функция N\n"
             "(тривиальный = пустой [0, 0] или с одним клиентом [0, c, 0])")
ax.set_ylim(-5, 105)
ax.axhline(0, color="#27ae60", linestyle=":", linewidth=1.2, alpha=0.5)
ax.legend(loc="center left", bbox_to_anchor=(1.02, 0.5),
          fontsize=10.5, frameon=False)
style(ax)
plt.tight_layout()
plt.savefig(OUT / "fig7_n_trivial_vs_n.png", dpi=200, bbox_inches="tight")
plt.close()


# ============================================================
# Figure mtsp_problem: input + valid solution — DPI 200, big labels
# ============================================================
print("[fig_mtsp_problem] regenerate at DPI 200")
rng = np.random.default_rng(42)
n_clients = 30
m = 4
pts = rng.uniform(5, 95, size=(n_clients, 2))
depot = np.array([50.0, 50.0])
all_pts = np.vstack([depot, pts])

# Simple equal-angle partitioning around depot for valid solution
angles = np.arctan2(pts[:, 1] - depot[1], pts[:, 0] - depot[0])
order = np.argsort(angles)
chunks = np.array_split(order, m)
route_palette = ["#3498db", "#e74c3c", "#27ae60", "#9b59b6", "#f39c12"]

fig, (axL, axR) = plt.subplots(1, 2, figsize=(13, 6), dpi=130)

# Left: input
axL.scatter(pts[:, 0], pts[:, 1], s=60, c="#3498db",
            edgecolors="black", linewidths=0.6, zorder=3, label="клиенты")
axL.scatter(depot[0], depot[1], s=320, c="#e74c3c", marker="*",
            edgecolors="black", linewidths=1.0, zorder=5, label="депо")
axL.set_title("(а) Вход: $n = 30$ клиентов + 1 депо, $m = 4$ коммивояжёра",
              fontsize=12)
axL.set_xlabel("x"); axL.set_ylabel("y")
axL.set_aspect("equal"); axL.grid(True, linestyle="--", alpha=0.3)
axL.legend(loc="upper right", fontsize=11, frameon=True)
# Label depot
axL.annotate("депо", (depot[0], depot[1]),
             xytext=(8, -16), textcoords="offset points",
             fontsize=11, color="#c0392b", fontweight="bold")

# Right: solution
axR.scatter(pts[:, 0], pts[:, 1], s=50, c="#bdc3c7",
            edgecolors="black", linewidths=0.4, alpha=0.5, zorder=2)
axR.scatter(depot[0], depot[1], s=320, c="#e74c3c", marker="*",
            edgecolors="black", linewidths=1.0, zorder=5)
for ri, chunk in enumerate(chunks):
    col = route_palette[ri % len(route_palette)]
    route_pts = pts[chunk]
    # Route order = sweep order (already sorted by angle)
    coords = np.vstack([depot, route_pts, depot])
    axR.plot(coords[:, 0], coords[:, 1], color=col, linewidth=2.2,
             marker="o", markersize=5, markerfacecolor=col,
             markeredgecolor="black", markeredgewidth=0.4,
             label=f"маршрут $R_{ri+1}$ ({len(chunk)} клиентов)",
             alpha=0.85, zorder=3)
axR.set_title("(б) Допустимое решение: 4 маршрута,\n"
              "каждый стартует и финиширует в депо", fontsize=12)
axR.set_xlabel("x"); axR.set_ylabel("y")
axR.set_aspect("equal"); axR.grid(True, linestyle="--", alpha=0.3)
axR.legend(loc="lower left", fontsize=10, frameon=True,
           framealpha=0.9, ncol=2, columnspacing=0.8)

fig.suptitle("Задача mTSP: вход и допустимое решение "
             "($n = 30$ клиентов, $m = 4$, $F_{\\mathrm{sum}} = \\sum_k \\mathrm{len}(R_k)$, "
             "$F_{\\max} = \\max_k \\mathrm{len}(R_k)$)",
             fontsize=13)
plt.tight_layout()
plt.savefig(OUT / "fig_mtsp_problem.png", dpi=200, bbox_inches="tight")
plt.close()

print("\nDone. Refreshed figures in:", OUT)
for fname in ["fig_mtsp_problem.png", "fig1_pareto_n25k.png",
              "fig6_pareto_dominance_n25k.png",
              "fig2_scaling_time.png", "fig7_n_trivial_vs_n.png"]:
    p = OUT / fname
    if p.exists():
        print(f"  {fname:38s}  {p.stat().st_size // 1024} KB")
