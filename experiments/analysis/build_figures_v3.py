"""Final fix-up pass for figures: rebuild fig7 and fig8 correctly, drop fig9.

Changes from v2:
  - fig7: legend moved out of plot area
  - fig8: gap computed PER (family, n, m), then averaged — not mixing scales
  - fig9: removed (methodologically weak proxy)

Other figures unchanged.
"""

from __future__ import annotations
import csv, json, math
from pathlib import Path
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "data" / "results"
FIG = RESULTS / "figures"
csv.field_size_limit(10**8)

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


def load_rows(path):
    if not path.exists(): return []
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


def style(ax):
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.grid(True, linestyle="--", alpha=0.4)


def enrich_inline(rows):
    out = []
    for row in rows:
        if not row.get("valid") or row.get("valid") == "False": continue
        if row.get("balance_max_avg", "") in ("", None):
            try:
                routes = json.loads(row.get("routes", "[]"))
                path = ROOT / row["path"]
                if not path.exists(): out.append(row); continue
                with path.open("r", encoding="utf-8") as fh:
                    lines = [ln.strip() for ln in fh if ln.strip()]
                coords = [tuple(map(float, ln.split())) for ln in lines[1:]]
                lengths, empty, singletons = [], 0, 0
                for r in routes:
                    if len(r) <= 2: empty += 1; lengths.append(0.0); continue
                    if len(r) == 3: singletons += 1
                    lengths.append(sum(math.hypot(coords[a][0]-coords[b][0], coords[a][1]-coords[b][1])
                                        for a, b in zip(r, r[1:])))
                nz = [l for l in lengths if l > 0]
                row = dict(row)
                row["sum_length"] = sum(lengths)
                row["max_length"] = max(lengths) if lengths else 0
                mean_l = sum(nz)/len(nz) if nz else 0
                row["balance_max_avg"] = max(lengths)/mean_l if mean_l > 0 else 0
                row["n_empty"] = empty
                row["n_singleton"] = singletons
            except Exception: pass
        out.append(row)
    return out


s1_a = load_rows(RESULTS / "stratum1_modular_n100_200_results_enriched.csv")
s1_b = load_rows(RESULTS / "stratum1_modular_n500_1000_results_enriched.csv")
v25 = enrich_inline(load_rows(RESULTS / "verify_n25000_results.csv"))
v50 = enrich_inline(load_rows(RESULTS / "verify_n50000_v21_only_results.csv"))
all_rows = s1_a + s1_b + v25 + v50
print(f"[load] total {len(all_rows)} valid rows")

budgets = {100: 10, 200: 10, 500: 30, 1000: 30, 25000: 150, 50000: 250}


# ==================================================================
# Figure 7-v3: n_trivial as % of m, legend outside plot
# ==================================================================
fig, ax = plt.subplots(figsize=(9, 5.5), dpi=130)
target7 = ["lkh3-baseline", "lkh_v21_minsum", "lkh_v21_minsum_cap",
           "lkh_v21_minsum_depot2m_plus", "lkh-wrapper-v21"]
for s in target7:
    pts = []
    for r in all_rows:
        if r["solver"] != s: continue
        n = i(r, "node_count")
        ne = i(r, "n_empty", 0); ns = i(r, "n_singleton", 0)
        ntr = (ne or 0) + (ns or 0)
        m = i(r, "salesman_count")
        if n is not None and m and m > 0:
            pts.append((n, ntr / m * 100.0))
    if not pts: continue
    by_n = defaultdict(list)
    for n, p in pts: by_n[n].append(p)
    xs = sorted(by_n.keys())
    ys = [sum(by_n[n])/len(by_n[n]) for n in xs]
    ax.plot(xs, ys, marker=MARK.get(s, "o"), color=COL.get(s, "black"),
            label=SOLVER_LABEL.get(s, s), linewidth=1.6, markersize=8)
ax.set_xscale("log")
ax.set_xlabel("Размер задачи N (log)")
ax.set_ylabel("Доля тривиальных маршрутов\n(n_empty + n_singleton) / m, %")
ax.set_title("Рисунок 7 — Доля тривиальных маршрутов как функция N\n"
             "(тривиальный = пустой [0,0] или с одним клиентом [0,c,0])")
ax.set_ylim(-5, 100)
# Legend OUTSIDE on the right
ax.legend(loc="center left", bbox_to_anchor=(1.02, 0.5), fontsize=9, frameon=False)
style(ax)
plt.tight_layout()
plt.savefig(FIG / "fig7_n_trivial_vs_n.png", dpi=200, bbox_inches="tight")
plt.close()


# ==================================================================
# Figure 8-v3: heatmap with PER-(family, n, m) gap, then averaged
# ==================================================================
solvers8 = ["lkh3-baseline", "lkh3-baseline-minmax", "lkh-wrapper-v21",
            "lkh_v21_minsum", "lkh_v21_minsum_cap",
            "lkh_v21_minsum_depot2m_plus", "lkh_v21_minmax"]
n_targets = [200, 1000, 25000]

# Step 1: aggregate per (n, m, family, solver) — average sum
cell = defaultdict(lambda: defaultdict(list))
for r in all_rows:
    sl = f(r, "sum_length")
    if not sl: continue
    n = i(r, "node_count"); m = i(r, "salesman_count")
    fam = r.get("instance_family", "")
    s = r["solver"]
    if n and m and fam:
        cell[(n, m, fam)][s].append(sl)
cell_avg = {(n, m, fam): {s: sum(v)/len(v) for s, v in d.items()}
            for (n, m, fam), d in cell.items()}

# Step 2: per cell — best across solvers — per-cell gaps
cell_gap = defaultdict(dict)  # [(n, m, fam)][solver] = gap%
for key, d in cell_avg.items():
    if not d: continue
    best = min(d.values())
    for s, val in d.items():
        cell_gap[key][s] = (val - best) / best * 100.0 if best > 0 else 0

# Step 3: average gaps per (n, solver) over all (m, family) for given n
gap_matrix = defaultdict(lambda: defaultdict(list))  # [n][solver] = list of gaps
for (n, m, fam), gaps in cell_gap.items():
    if n not in n_targets: continue
    for s, g in gaps.items():
        gap_matrix[n][s].append(g)

# Also: balance & time still need per-row averages, no fix needed there
balance_matrix = defaultdict(lambda: defaultdict(list))
time_matrix = defaultdict(lambda: defaultdict(list))
for r in all_rows:
    n = i(r, "node_count"); s = r["solver"]
    if n in n_targets:
        b = f(r, "balance_max_avg")
        t = f(r, "time_seconds")
        if b: balance_matrix[n][s].append(b)
        if t: time_matrix[n][s].append(t)

# Build matrix [solver][n*3 metrics]
fig, axes = plt.subplots(1, 3, figsize=(15, 5), dpi=130)

# Gap heatmap
mat_gap = []
for s in solvers8:
    row = []
    for n in n_targets:
        vals = gap_matrix[n].get(s, [])
        v = sum(vals)/len(vals) if vals else float("nan")
        row.append(v)
    mat_gap.append(row)
mat_gap = np.array(mat_gap)
mat_gap_disp = np.clip(mat_gap, 0, 30)

ax = axes[0]
im = ax.imshow(mat_gap_disp, cmap="RdYlGn_r", aspect="auto", vmin=0, vmax=30)
ax.set_xticks(range(len(n_targets)))
ax.set_xticklabels([f"N={n}" for n in n_targets])
ax.set_yticks(range(len(solvers8)))
ax.set_yticklabels([SOLVER_LABEL.get(s, s) for s in solvers8], fontsize=9)
ax.set_title("GAP к лучшему по SUM (на ту же ячейку), %", fontsize=10)
for ri in range(len(solvers8)):
    for ci in range(len(n_targets)):
        v = mat_gap[ri, ci]
        if not np.isnan(v):
            ax.text(ci, ri, f"+{v:.1f}", ha="center", va="center", fontsize=8.5,
                    color="black" if v < 18 else "white")
plt.colorbar(im, ax=ax, fraction=0.046, pad=0.04)

# Balance heatmap
mat_bal = []
for s in solvers8:
    row = []
    for n in n_targets:
        vals = balance_matrix[n].get(s, [])
        v = sum(vals)/len(vals) if vals else float("nan")
        row.append(v)
    mat_bal.append(row)
mat_bal = np.array(mat_bal)
mat_bal_disp = np.clip(mat_bal, 1, 7)

ax = axes[1]
im = ax.imshow(mat_bal_disp, cmap="RdYlGn_r", aspect="auto", vmin=1, vmax=7)
ax.set_xticks(range(len(n_targets)))
ax.set_xticklabels([f"N={n}" for n in n_targets])
ax.set_yticks(range(len(solvers8)))
ax.set_yticklabels([])
ax.set_title("balance_max_avg (1.0 = идеал)", fontsize=10)
for ri in range(len(solvers8)):
    for ci in range(len(n_targets)):
        v = mat_bal[ri, ci]
        if not np.isnan(v):
            ax.text(ci, ri, f"{v:.2f}", ha="center", va="center", fontsize=8.5,
                    color="black" if v < 4.5 else "white")
plt.colorbar(im, ax=ax, fraction=0.046, pad=0.04)

# Time/budget heatmap
mat_t = []
for s in solvers8:
    row = []
    for n in n_targets:
        vals = time_matrix[n].get(s, [])
        v = sum(vals)/len(vals) if vals else float("nan")
        row.append(v / budgets[n] if not math.isnan(v) and v else float("nan"))
    mat_t.append(row)
mat_t = np.array(mat_t)
mat_t_disp = np.clip(mat_t, 0, 13)

ax = axes[2]
im = ax.imshow(mat_t_disp, cmap="RdYlGn_r", aspect="auto", vmin=0, vmax=13)
ax.set_xticks(range(len(n_targets)))
ax.set_xticklabels([f"N={n}" for n in n_targets])
ax.set_yticks(range(len(solvers8)))
ax.set_yticklabels([])
ax.set_title("time / budget (1.0 = в budget; >1 = превышение)", fontsize=10)
# Mark budget=1 line implicitly with text colour
for ri in range(len(solvers8)):
    for ci in range(len(n_targets)):
        v = mat_t[ri, ci]
        if not np.isnan(v):
            mark = "" if v <= 1.05 else " ⚠"
            ax.text(ci, ri, f"{v:.2f}{mark}", ha="center", va="center", fontsize=8.5,
                    color="black" if v < 8 else "white")
plt.colorbar(im, ax=ax, fraction=0.046, pad=0.04)

fig.suptitle("Рисунок 8 — Многомерный профиль решателей\n"
             "(GAP — относительный к лучшему в той же ячейке (n, m, family), без смешения геометрий;\n"
             "зелёное = лучше; красное = хуже; для time/budget значение 1.0 = ровно в budget)",
             fontsize=10.5)
plt.tight_layout()
plt.savefig(FIG / "fig8_solver_profile.png", dpi=200, bbox_inches="tight")
plt.close()


# Remove fig9 (methodologically weak)
old_fig9 = FIG / "fig9_catchup_n25k.png"
if old_fig9.exists():
    old_fig9.unlink()
    print("Removed fig9 (methodologically weak)")

print("\nFinal figures:")
for fname in sorted(FIG.iterdir()):
    print(f"  {fname.name}  ({fname.stat().st_size//1024} KB)")
