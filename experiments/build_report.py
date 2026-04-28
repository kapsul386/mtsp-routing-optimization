from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import defaultdict
from pathlib import Path

from mtsp_experiment_utils import ensure_instance_families, instance_family_sort_key


ROOT = Path(__file__).resolve().parents[1]


def configure_csv_field_limit() -> None:
    limit = sys.maxsize
    while True:
        try:
            csv.field_size_limit(limit)
            return
        except OverflowError:
            limit //= 10


def resolve_path(path_str: str) -> Path:
    path = Path(path_str)
    return path if path.is_absolute() else ROOT / path


def load_csv(path: Path) -> list[dict]:
    with path.open("r", encoding="utf-8", newline="") as fh:
        return list(csv.DictReader(fh))


def maybe_load_csv(path: Path | None) -> list[dict]:
    if path is None or not path.exists():
        return []
    return load_csv(path)


def maybe_float(value: str) -> float | None:
    if value == "":
        return None
    return float(value)


def display_value(value: str) -> str:
    return value if value != "" else "n/a"


def format_time_pair(our_time: str, reference_time: str | None) -> str:
    if reference_time in (None, ""):
        return our_time
    return f"{our_time} / {reference_time}"


def is_true(value: object) -> bool:
    return str(value).lower() == "true"


def report_key(row: dict) -> tuple[str, str, str, str]:
    return row["instance_family"], row["node_count"], row["salesman_count"], row["solver"]


def build_markdown(summary_rows: list[dict], raw_rows: list[dict], reference_rows: list[dict]) -> str:
    summary_rows = ensure_instance_families(summary_rows)
    raw_rows = ensure_instance_families(raw_rows)
    reference_rows = ensure_instance_families(reference_rows)

    grouped_by_config: dict[tuple[str, str, str], list[dict]] = defaultdict(list)
    for row in summary_rows:
        grouped_by_config[(row["instance_family"], row["node_count"], row["salesman_count"])].append(row)

    reference_by_key = {report_key(row): row for row in reference_rows}

    lines: list[str] = []
    lines.append("# Результаты вычислительных экспериментов")
    lines.append("")
    lines.append("Сводка построена автоматически по CSV-артефактам экспериментов.")
    lines.append("")
    lines.append("## Агрегированные таблицы")
    lines.append("")

    for (instance_family, node_count, salesman_count), rows in sorted(
        grouped_by_config.items(),
        key=lambda item: (
            instance_family_sort_key(item[0][0]),
            int(item[0][1]),
            int(item[0][2]),
        ),
    ):
        def score(row: dict) -> float:
            reference_row = reference_by_key.get(report_key(row))
            gap = maybe_float(reference_row["avg_objective_gap"]) if reference_row is not None else None
            if gap is not None:
                return gap
            objective = maybe_float(row["avg_objective"])
            return objective if objective is not None else float("inf")

        rows = sorted(rows, key=score)
        lines.append(f"### {instance_family} | n={node_count}, m={salesman_count}")
        lines.append("")

        has_reference = any(report_key(row) in reference_by_key for row in rows)
        if has_reference:
            lines.append("| Solver | Runs | Our | OR-Tools | Gap | Time (our/ref) | Valid Runs |")
            lines.append("| --- | ---: | ---: | ---: | ---: | ---: | ---: |")
        else:
            lines.append("| Solver | Runs | Our | Time (s) | Valid Runs |")
            lines.append("| --- | ---: | ---: | ---: | ---: |")

        for row in rows:
            reference_row = reference_by_key.get(report_key(row))
            if reference_row is None:
                lines.append(
                    f"| {row['solver']} | {display_value(row['runs'])} | {display_value(row['avg_objective'])} | "
                    f"{display_value(row['avg_time_seconds'])} | {display_value(row['valid_runs'])} |"
                )
                continue

            lines.append(
                f"| {row['solver']} | {display_value(row['runs'])} | {display_value(row['avg_objective'])} | "
                f"{display_value(reference_row['avg_reference_objective'])} | {display_value(reference_row['avg_objective_gap'])} | "
                f"{format_time_pair(display_value(row['avg_time_seconds']), reference_row.get('avg_reference_time_seconds'))} | "
                f"{display_value(row['valid_runs'])} |"
            )
        lines.append("")

    lines.append("## Краткие наблюдения")
    lines.append("")

    best_counts: dict[str, int] = defaultdict(int)
    for rows in grouped_by_config.values():
        best = min(
            rows,
            key=lambda row: (
                maybe_float(reference_by_key.get(report_key(row), {}).get("avg_objective_gap", ""))
                if reference_by_key.get(report_key(row)) is not None and
                maybe_float(reference_by_key[report_key(row)]["avg_objective_gap"]) is not None
                else (maybe_float(row["avg_objective"]) if maybe_float(row["avg_objective"]) is not None else float("inf"))
            ),
        )
        best_counts[best["solver"]] += 1

    if best_counts:
        ranking = sorted(best_counts.items(), key=lambda item: item[1], reverse=True)
        lines.append(
            "По числу лучших средних результатов лидеры распределились так: "
            + ", ".join(f"`{solver}` - {count}" for solver, count in ranking)
            + "."
        )
        lines.append("")

    raw_grouped: dict[str, list[dict]] = defaultdict(list)
    for row in raw_rows:
        if is_true(row["valid"]):
            raw_grouped[row["solver"]].append(row)

    solver_means = []
    for solver, rows in raw_grouped.items():
        avg_obj = sum(float(row["objective"]) for row in rows) / len(rows)
        avg_time = sum(float(row["time_seconds"]) for row in rows) / len(rows)
        solver_reference_rows = [
            reference_row for reference_row in reference_rows
            if reference_row["solver"] == solver and maybe_float(reference_row["avg_objective_gap"]) is not None
        ]
        if solver_reference_rows:
            avg_reference = sum(float(row["avg_reference_objective"]) for row in solver_reference_rows) / len(solver_reference_rows)
            avg_gap = sum(float(row["avg_objective_gap"]) for row in solver_reference_rows) / len(solver_reference_rows)
            avg_reference_time = (
                sum(float(row["avg_reference_time_seconds"]) for row in solver_reference_rows) / len(solver_reference_rows)
            )
        else:
            avg_reference = None
            avg_gap = None
            avg_reference_time = None

        solver_means.append((solver, avg_obj, avg_reference, avg_gap, avg_time, avg_reference_time))

    solver_means.sort(key=lambda item: item[3] if item[3] is not None else item[1])

    if solver_means:
        lines.append("Средние значения по всем валидным прогонам:")
        lines.append("")
        for solver, avg_obj, avg_reference, avg_gap, avg_time, avg_reference_time in solver_means:
            if avg_reference is None or avg_gap is None or avg_reference_time is None:
                lines.append(f"- `{solver}`: our = {avg_obj:.6f} | time = {avg_time:.6f} s.")
            else:
                lines.append(
                    f"- `{solver}`: our = {avg_obj:.6f} | OR-Tools = {avg_reference:.6f} | "
                    f"gap = {avg_gap:.6f} | time = {avg_time:.6f} / {avg_reference_time:.6f} s."
                )
        lines.append("")

    lines.append("## Интерпретация для отчёта")
    lines.append("")
    lines.append(
        "Основной ориентир теперь задаётся не только внутренним сравнением между нашими методами, "
        "но и единым зафиксированным reference CSV от OR-Tools."
    )
    lines.append(
        "Агрегация теперь ведётся отдельно по семействам инстансов, поэтому `uniform`, `clustered-*` и "
        "`mixed-outliers` не смешиваются в одной строке сводки."
    )
    lines.append(
        "Базовый формат чтения строк в отчётах теперь такой: `family | our | OR-Tools | gap | time`."
    )
    lines.append("")

    return "\n".join(lines)


def main() -> None:
    configure_csv_field_limit()
    parser = argparse.ArgumentParser(description="Build a report-friendly markdown summary from mTSP CSV outputs.")
    parser.add_argument("--config", default="experiments/config.json", help="Path to experiment config JSON.")
    args = parser.parse_args()

    config = json.loads(resolve_path(args.config).read_text(encoding="utf-8"))
    results_csv = resolve_path(config["results_csv"])
    summary_csv = resolve_path(config["summary_csv"])
    report_md = resolve_path(config["report_md"])
    reference_summary_csv = resolve_path(config["reference_summary_csv"]) if "reference_summary_csv" in config else None

    raw_rows = load_csv(results_csv)
    summary_rows = load_csv(summary_csv)
    reference_rows = maybe_load_csv(reference_summary_csv)
    report_md.write_text(build_markdown(summary_rows, raw_rows, reference_rows), encoding="utf-8")


if __name__ == "__main__":
    main()
