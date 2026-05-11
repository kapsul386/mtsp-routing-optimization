"""Build the final unified stratum-1 comparison table.

Inputs: enriched per-instance result CSVs for N=100..200 and N=500..1000.
Outputs: data/results/stratum1_final_report.md with multiple metric tables.

Metrics per (family, n, m, solver):
  - sum    — average sum-of-routes (MINSUM objective)
  - max    — average max-route length (MINMAX-relevant)
  - balance — average max/avg ratio over non-empty routes
  - n_empty — average count of empty routes ([0,0])
  - time   — average wall-clock seconds
  - n_singleton — count of routes with exactly 1 customer

Solver columns ordered for presentation. Bold = best in row for given metric.
"""

from __future__ import annotations

import csv
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


SOURCES = [
    ROOT / "data" / "results" / "stratum1_modular_n100_200_results_enriched.csv",
    ROOT / "data" / "results" / "stratum1_modular_n500_1000_results_enriched.csv",
]


SOLVER_ORDER = [
    "2opt+greed",
    "lkh3-baseline",
    "lkh3-baseline-minmax",
    "lkh-wrapper-v21",
    "lkh_v21_minsum",
    "lkh_v21_minsum_cap",
    "lkh_v21_minsum_depot2m_plus",
    "lkh_v21_minmax",
]

PRIMARY_V21 = "lkh_v21_minsum_depot2m_plus"
LKH3_REF = "lkh3-baseline"


def load_rows() -> list[dict]:
    rows = []
    for src in SOURCES:
        if not src.exists():
            print(f"[skip] {src} missing")
            continue
        with src.open("r", encoding="utf-8") as fh:
            for row in csv.DictReader(fh):
                rows.append(row)
        print(f"[load] {src}")
    return rows


def aggregate(rows: list[dict]) -> dict:
    """Average each numeric metric per (family, n, m, solver)."""
    grouped: dict[tuple, list[dict]] = defaultdict(list)
    for row in rows:
        if not row.get("valid") or row["valid"] == "False":
            continue
        key = (
            row["instance_family"],
            int(row["node_count"]),
            int(row["salesman_count"]),
            row["solver"],
        )
        grouped[key].append(row)

    agg: dict[tuple, dict[str, float]] = {}
    for key, items in grouped.items():
        def avg(field: str) -> float:
            vals = [float(it[field]) for it in items if it.get(field) not in ("", None)]
            return sum(vals) / len(vals) if vals else 0.0
        agg[key] = {
            "sum_length":      avg("sum_length"),
            "max_length":      avg("max_length"),
            "min_length":      avg("min_length"),
            "balance_max_avg": avg("balance_max_avg"),
            "n_empty":         avg("n_empty"),
            "n_singleton":     avg("n_singleton"),
            "time_seconds":    avg("time_seconds"),
            "n_runs":          len(items),
        }
    return agg


def fam_key(fam: str) -> int:
    order = {"uniform": 0, "clustered-center": 1, "clustered-offset-depot": 2, "mixed-outliers": 3}
    return order.get(fam, 99)


def fmt_value(v: float, fmt: str, is_best: bool) -> str:
    s = format(v, fmt)
    return f"**{s}**" if is_best else s


def emit_table_for_metric(
    agg: dict,
    metric: str,
    fmt: str = ".1f",
    higher_is_better: bool = False,
    invalid_value: str = "-",
) -> list[str]:
    keys_sorted = sorted(agg.keys(), key=lambda k: (fam_key(k[0]), k[1], k[2]))
    seen_solvers = sorted({k[3] for k in agg}, key=lambda s: (
        SOLVER_ORDER.index(s) if s in SOLVER_ORDER else 999
    ))

    rows_per_cell = defaultdict(dict)
    for key, vals in agg.items():
        rows_per_cell[(key[0], key[1], key[2])][key[3]] = vals[metric]

    lines = []
    header = ["family", "n", "m"] + seen_solvers
    lines.append("| " + " | ".join(header) + " |")
    lines.append("|" + "|".join(["---"] * len(header)) + "|")

    cell_keys = sorted(rows_per_cell.keys(), key=lambda k: (fam_key(k[0]), k[1], k[2]))
    for cell in cell_keys:
        cell_vals = rows_per_cell[cell]
        nonzero_vals = [v for v in cell_vals.values() if v > 0]
        if not nonzero_vals:
            continue
        best = max(nonzero_vals) if higher_is_better else min(nonzero_vals)
        row = [cell[0], str(cell[1]), str(cell[2])]
        for s in seen_solvers:
            v = cell_vals.get(s)
            if v is None or v == 0.0:
                row.append(invalid_value)
            else:
                row.append(fmt_value(v, fmt, abs(v - best) < 1e-6))
        lines.append("| " + " | ".join(row) + " |")
    return lines


def emit_gap_table(agg: dict, ref_solver: str, target_solver: str) -> list[str]:
    keys_sorted = sorted(agg.keys(), key=lambda k: (fam_key(k[0]), k[1], k[2]))
    cell_data: dict[tuple, dict] = defaultdict(dict)
    for key, vals in agg.items():
        cell_data[(key[0], key[1], key[2])][key[3]] = vals
    lines = [
        f"| family | n | m | sum_{ref_solver} | sum_{target_solver} | gap% | max_{ref_solver} | max_{target_solver} | gap_max% | balance_{ref_solver} | balance_{target_solver} | n_empty_{ref_solver} | n_empty_{target_solver} |",
        "|---|---|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    cell_keys = sorted(cell_data.keys(), key=lambda k: (fam_key(k[0]), k[1], k[2]))
    for cell in cell_keys:
        ref = cell_data[cell].get(ref_solver, {})
        tgt = cell_data[cell].get(target_solver, {})
        if not ref or not tgt:
            continue
        ref_sum, tgt_sum = ref.get("sum_length", 0), tgt.get("sum_length", 0)
        ref_max, tgt_max = ref.get("max_length", 0), tgt.get("max_length", 0)
        ref_bal, tgt_bal = ref.get("balance_max_avg", 0), tgt.get("balance_max_avg", 0)
        ref_emp, tgt_emp = ref.get("n_empty", 0), tgt.get("n_empty", 0)
        gap_sum = (tgt_sum - ref_sum) / ref_sum * 100 if ref_sum else 0
        gap_max = (tgt_max - ref_max) / ref_max * 100 if ref_max else 0
        sign_sum = "+" if gap_sum >= 0 else ""
        sign_max = "+" if gap_max >= 0 else ""
        lines.append(
            f"| {cell[0]} | {cell[1]} | {cell[2]} | "
            f"{ref_sum:.1f} | {tgt_sum:.1f} | {sign_sum}{gap_sum:.1f}% | "
            f"{ref_max:.1f} | {tgt_max:.1f} | {sign_max}{gap_max:.1f}% | "
            f"{ref_bal:.2f} | {tgt_bal:.2f} | {ref_emp:.1f} | {tgt_emp:.1f} |"
        )
    return lines


def emit_win_count(agg: dict, metric: str, higher_is_better: bool = False) -> dict[str, int]:
    cells = defaultdict(dict)
    for key, vals in agg.items():
        cells[(key[0], key[1], key[2])][key[3]] = vals[metric]
    wins = defaultdict(int)
    for cell, sv in cells.items():
        nonzero = [(s, v) for s, v in sv.items() if v > 0]
        if not nonzero:
            continue
        best = max(nonzero, key=lambda x: x[1])[0] if higher_is_better else min(nonzero, key=lambda x: x[1])[0]
        wins[best] += 1
    return wins


def main() -> None:
    rows = load_rows()
    if not rows:
        print("No data")
        return
    agg = aggregate(rows)

    out = ROOT / "data" / "results" / "stratum1_final_report.md"
    lines = ["# Stratum-1 (N=100..1000) — final multi-metric comparison", ""]
    lines.append("**Solvers:**")
    lines.append("")
    for s in SOLVER_ORDER:
        marker = " 🌟 (primary v21)" if s == PRIMARY_V21 else ""
        lines.append(f"- `{s}`{marker}")
    lines.append("")
    lines.append("**Metrics computed:**")
    lines.append("")
    lines.append("- `sum` — average sum-of-routes (the MINSUM objective)")
    lines.append("- `max` — length of the longest route (smaller is fairer)")
    lines.append("- `balance` — `max / mean` over non-empty routes (1.0 = perfect balance)")
    lines.append("- `n_empty` — average count of empty routes (`[0,0]`) — flag for degenerate solutions")
    lines.append("- `time` — wall-clock seconds")
    lines.append("")
    lines.append("Bold = best in row.")
    lines.append("")
    lines.append("---")
    lines.append("")

    # Sum table
    lines.append("## 1. SUM length (MINSUM objective)")
    lines.append("")
    lines += emit_table_for_metric(agg, "sum_length", ".1f", higher_is_better=False)
    lines.append("")

    # Max table
    lines.append("## 2. MAX route length (MINMAX-relevant)")
    lines.append("")
    lines += emit_table_for_metric(agg, "max_length", ".1f", higher_is_better=False)
    lines.append("")

    # Balance
    lines.append("## 3. Balance ratio (max / mean) — closer to 1.0 = more balanced")
    lines.append("")
    lines += emit_table_for_metric(agg, "balance_max_avg", ".2f", higher_is_better=False)
    lines.append("")

    # n_empty
    lines.append("## 4. Number of empty routes (degenerate-solution flag)")
    lines.append("")
    lines.append("Higher = solver leaves more 'idle' trucks. LKH-3 default produces these naturally.")
    lines.append("")
    lines += emit_table_for_metric(agg, "n_empty", ".1f", higher_is_better=False)
    lines.append("")

    # Time
    lines.append("## 5. Wall-clock time (seconds)")
    lines.append("")
    lines += emit_table_for_metric(agg, "time_seconds", ".2f", higher_is_better=False)
    lines.append("")

    # Head-to-head
    lines.append("---")
    lines.append("")
    lines.append(f"## 6. Head-to-head: `{LKH3_REF}` vs `{PRIMARY_V21}` (primary v21)")
    lines.append("")
    lines.append("`gap%` = (target − ref) / ref × 100 — negative = primary v21 wins.")
    lines.append("")
    lines += emit_gap_table(agg, LKH3_REF, PRIMARY_V21)
    lines.append("")

    # Win counts
    lines.append("---")
    lines.append("")
    lines.append("## 7. Win counts")
    lines.append("")
    sum_wins = emit_win_count(agg, "sum_length", higher_is_better=False)
    max_wins = emit_win_count(agg, "max_length", higher_is_better=False)
    bal_wins = emit_win_count(agg, "balance_max_avg", higher_is_better=False)
    n_cells = sum(sum_wins.values())
    lines.append(f"Total cells: {n_cells}")
    lines.append("")
    lines.append("**SUM-objective wins:**")
    for s, c in sorted(sum_wins.items(), key=lambda x: -x[1]):
        lines.append(f"- `{s}`: {c} ({c/n_cells*100:.1f}%)")
    lines.append("")
    lines.append("**MAX-objective wins (lower max = better):**")
    for s, c in sorted(max_wins.items(), key=lambda x: -x[1]):
        lines.append(f"- `{s}`: {c} ({c/n_cells*100:.1f}%)")
    lines.append("")
    lines.append("**Balance wins (closer to 1.0 = better):**")
    for s, c in sorted(bal_wins.items(), key=lambda x: -x[1]):
        lines.append(f"- `{s}`: {c} ({c/n_cells*100:.1f}%)")
    lines.append("")

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"\nWrote {out}")
    print(f"\nSUM wins:  {dict(sum_wins)}")
    print(f"MAX wins:  {dict(max_wins)}")
    print(f"Balance wins: {dict(bal_wins)}")


if __name__ == "__main__":
    main()
