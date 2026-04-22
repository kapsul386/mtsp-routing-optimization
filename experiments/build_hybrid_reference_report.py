from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path

from mtsp_experiment_utils import ensure_instance_families, instance_family_sort_key


ROOT = Path(__file__).resolve().parents[1]


def resolve_path(path_str: str) -> Path:
    path = Path(path_str)
    return path if path.is_absolute() else ROOT / path


def load_csv(path: Path) -> list[dict]:
    with path.open("r", encoding="utf-8-sig", newline="") as fh:
        return list(csv.DictReader(fh))


def maybe_float(value: object) -> float | None:
    if value in ("", None):
        return None
    return float(value)


def display_value(value: object) -> str:
    return str(value) if value not in ("", None) else "n/a"


def is_true(value: object) -> bool:
    return str(value).lower() == "true"


def group_key(row: dict) -> tuple[str, str, str]:
    return row["instance_family"], row["node_count"], row["salesman_count"]


def solver_key(row: dict) -> tuple[str, str, str, str]:
    return row["instance_family"], row["node_count"], row["salesman_count"], row["solver"]


def _baseline_usage_label(rows: list[dict]) -> str:
    counts = Counter(
        str(row["best_baseline_solver"])
        for row in rows
        if str(row.get("best_baseline_solver", "")).strip()
    )
    if not counts:
        return "n/a"
    return ", ".join(f"{name} x{count}" for name, count in counts.most_common())


def build_markdown(summary_rows: list[dict], comparison_rows: list[dict], best_rows: list[dict], candidate_rows: list[dict]) -> str:
    summary_rows = ensure_instance_families(summary_rows)
    comparison_rows = ensure_instance_families(comparison_rows)
    best_rows = ensure_instance_families(best_rows)
    candidate_rows = ensure_instance_families(candidate_rows)

    grouped_summary: dict[tuple[str, str, str], list[dict]] = defaultdict(list)
    for row in summary_rows:
        grouped_summary[group_key(row)].append(row)

    comparison_by_solver: dict[tuple[str, str, str, str], list[dict]] = defaultdict(list)
    for row in comparison_rows:
        comparison_by_solver[solver_key(row)].append(row)

    lines: list[str] = []
    lines.append("# Hybrid Baseline Report")
    lines.append("")
    lines.append(
        "This report compares the selected mTSP solvers against the best successful baseline per instance. "
        "The baseline pool is tiered by `n`: exact MIP for small cases, OR-Tools Routing for medium cases, "
        "and TSP-transform + LKH for large cases."
    )
    lines.append("")
    lines.append("## Aggregated Tables")
    lines.append("")

    for (instance_family, node_count, salesman_count), rows in sorted(
        grouped_summary.items(),
        key=lambda item: (
            instance_family_sort_key(item[0][0]),
            int(item[0][1]),
            int(item[0][2]),
        ),
    ):
        rows = sorted(
            rows,
            key=lambda row: (
                maybe_float(row["avg_relative_gap_percent"])
                if maybe_float(row["avg_relative_gap_percent"]) is not None
                else float("inf"),
                maybe_float(row["avg_objective"])
                if maybe_float(row["avg_objective"]) is not None
                else float("inf"),
            ),
        )
        lines.append(f"### {instance_family} | n={node_count}, m={salesman_count}")
        lines.append("")
        lines.append("| Solver | Runs | Our | Best baseline | Gap % | Time (our/best) | Baseline usage | Better runs |")
        lines.append("| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |")
        for row in rows:
            usage = _baseline_usage_label(comparison_by_solver.get(solver_key(row), []))
            lines.append(
                f"| {row['solver']} | {display_value(row['runs'])} | {display_value(row['avg_objective'])} | "
                f"{display_value(row['avg_best_baseline_objective'])} | {display_value(row['avg_relative_gap_percent'])} | "
                f"{display_value(row['avg_time_seconds'])} / {display_value(row['avg_best_baseline_time_seconds'])} | "
                f"{usage} | {display_value(row['better_than_best_baseline_runs'])} |"
            )
        lines.append("")

    lines.append("## Key Observations")
    lines.append("")

    if best_rows:
        baseline_usage = Counter(
            str(row["best_baseline_solver"])
            for row in best_rows
            if str(row.get("best_baseline_solver", "")).strip()
        )
        if baseline_usage:
            lines.append(
                "Best baseline usage across instances: "
                + ", ".join(f"`{name}` - {count}" for name, count in baseline_usage.most_common())
                + "."
            )
            lines.append("")

        exact_best = sum(is_true(row.get("best_baseline_is_exact", False)) for row in best_rows)
        if exact_best:
            lines.append(f"Exact MIP was the winning baseline on {exact_best} instances.")
            lines.append("")

    solver_groups: dict[str, list[dict]] = defaultdict(list)
    for row in comparison_rows:
        solver_groups[str(row["solver"])].append(row)

    solver_means = []
    for solver, rows in solver_groups.items():
        comparable_rows = [
            row for row in rows
            if is_true(row["valid"]) and is_true(row["best_baseline_valid"]) and maybe_float(row["relative_gap_percent"]) is not None
        ]
        if not comparable_rows:
            continue
        avg_gap_percent = sum(float(row["relative_gap_percent"]) for row in comparable_rows) / len(comparable_rows)
        avg_gap = sum(float(row["objective_gap"]) for row in comparable_rows) / len(comparable_rows)
        avg_our_time = sum(float(row["time_seconds"]) for row in comparable_rows) / len(comparable_rows)
        avg_best_time = sum(float(row["best_baseline_time_seconds"]) for row in comparable_rows) / len(comparable_rows)
        solver_means.append((solver, avg_gap_percent, avg_gap, avg_our_time, avg_best_time, len(comparable_rows)))

    solver_means.sort(key=lambda item: item[1])
    if solver_means:
        lines.append("Overall comparable means:")
        lines.append("")
        for solver, avg_gap_percent, avg_gap, avg_our_time, avg_best_time, count in solver_means:
            lines.append(
                f"- `{solver}`: gap = {avg_gap:.6f} | gap % = {avg_gap_percent:.6f} | "
                f"time = {avg_our_time:.6f} / {avg_best_time:.6f} s over {count} runs."
            )
        lines.append("")

    candidate_status = Counter(
        f"{row['baseline_solver']}::{row['status']}"
        for row in candidate_rows
    )
    if candidate_status:
        lines.append("Baseline candidate statuses:")
        lines.append("")
        for name, count in candidate_status.most_common():
            lines.append(f"- `{name}`: {count}")
        lines.append("")

    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a markdown report for the hybrid baseline comparison.")
    parser.add_argument(
        "--config",
        default="experiments/hybrid_reference_config.json",
        help="Path to the hybrid baseline config JSON.",
    )
    args = parser.parse_args()

    config = json.loads(resolve_path(args.config).read_text(encoding="utf-8-sig"))
    summary_rows = load_csv(resolve_path(config["summary_csv"]))
    comparison_rows = load_csv(resolve_path(config["comparison_csv"]))
    best_rows = load_csv(resolve_path(config["best_baseline_csv"]))
    candidate_rows = load_csv(resolve_path(config["baseline_candidates_csv"]))
    report_path = resolve_path(config["report_md"])
    report_path.write_text(
        build_markdown(summary_rows, comparison_rows, best_rows, candidate_rows),
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
