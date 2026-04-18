from __future__ import annotations

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path


def load_csv(path: Path) -> list[dict]:
    with path.open("r", encoding="utf-8", newline="") as fh:
        return list(csv.DictReader(fh))


def maybe_load_csv(path: Path | None) -> list[dict]:
    if path is None or not path.exists():
        return []
    return load_csv(path)


def to_float(row: dict, key: str) -> float:
    return float(row[key])


def format_time_pair(our_time: str, reference_time: str | None) -> str:
    if reference_time in (None, ""):
        return our_time
    return f"{our_time} / {reference_time}"


def build_markdown(summary_rows: list[dict], raw_rows: list[dict], reference_rows: list[dict]) -> str:
    grouped_by_size: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for row in summary_rows:
        grouped_by_size[(row["node_count"], row["salesman_count"])].append(row)

    reference_by_key = {
        (row["node_count"], row["salesman_count"], row["solver"]): row
        for row in reference_rows
    }

    lines: list[str] = []
    lines.append("# Результаты вычислительных экспериментов")
    lines.append("")
    lines.append("Сводка построена автоматически по CSV-артефактам экспериментов.")
    lines.append("")
    lines.append("## Агрегированные таблицы")
    lines.append("")

    for (node_count, salesman_count), rows in sorted(grouped_by_size.items(), key=lambda item: (int(item[0][0]), int(item[0][1]))):
        rows = sorted(rows, key=lambda row: to_float(row, "avg_objective"))
        lines.append(f"### n={node_count}, m={salesman_count}")
        lines.append("")

        has_reference = any(
            (row["node_count"], row["salesman_count"], row["solver"]) in reference_by_key
            for row in rows
        )
        if has_reference:
            lines.append("| Solver | Runs | Our | OR-Tools | Gap | Time (our/ref) | Valid Runs |")
            lines.append("| --- | ---: | ---: | ---: | ---: | ---: | ---: |")
        else:
            lines.append("| Solver | Runs | Our | Time (s) | Valid Runs |")
            lines.append("| --- | ---: | ---: | ---: | ---: |")

        for row in rows:
            reference_row = reference_by_key.get((row["node_count"], row["salesman_count"], row["solver"]))
            if reference_row is None:
                lines.append(
                    f"| {row['solver']} | {row['runs']} | {row['avg_objective']} | {row['avg_time_seconds']} | {row['valid_runs']} |"
                )
                continue

            lines.append(
                f"| {row['solver']} | {row['runs']} | {row['avg_objective']} | "
                f"{reference_row['avg_reference_objective']} | {reference_row['avg_objective_gap']} | "
                f"{format_time_pair(row['avg_time_seconds'], reference_row.get('avg_reference_time_seconds'))} | "
                f"{row['valid_runs']} |"
            )
        lines.append("")

    lines.append("## Краткие наблюдения")
    lines.append("")

    best_counts: dict[str, int] = defaultdict(int)
    for rows in grouped_by_size.values():
        def score(row: dict) -> float:
            reference_row = reference_by_key.get((row["node_count"], row["salesman_count"], row["solver"]))
            if reference_row is not None and reference_row["avg_objective_gap"] != "":
                return float(reference_row["avg_objective_gap"])
            return to_float(row, "avg_objective")

        best = min(rows, key=score)
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
        raw_grouped[row["solver"]].append(row)

    solver_means = []
    for solver, rows in raw_grouped.items():
        avg_obj = sum(float(row["objective"]) for row in rows) / len(rows)
        avg_time = sum(float(row["time_seconds"]) for row in rows) / len(rows)
        solver_reference_rows = [
            reference_row for reference_row in reference_rows
            if reference_row["solver"] == solver and reference_row["avg_objective_gap"] != ""
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
        lines.append("Средние значения по всем прогонам:")
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

    lines.append("## Интерпретация для отчета")
    lines.append("")
    lines.append(
        "Основной ориентир теперь задается не только внутренним сравнением между нашими методами, "
        "но и единым зафиксированным reference CSV от OR-Tools."
    )
    lines.append(
        "Базовый формат чтения строк в отчетах теперь такой: `our | OR-Tools | gap | time`."
    )
    lines.append(
        "Это позволяет честно оценивать прогресс новых версий `lkh-wrapper` относительно одного и того же внешнего baseline."
    )
    lines.append("")

    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build a report-friendly markdown summary from mTSP CSV outputs.")
    parser.add_argument("--config", default="experiments/config.json", help="Path to experiment config JSON.")
    args = parser.parse_args()

    config = json.loads(Path(args.config).read_text(encoding="utf-8"))
    results_csv = Path(config["results_csv"])
    summary_csv = Path(config["summary_csv"])
    report_md = Path(config["report_md"])
    reference_summary_csv = Path(config["reference_summary_csv"]) if "reference_summary_csv" in config else None

    raw_rows = load_csv(results_csv)
    summary_rows = load_csv(summary_csv)
    reference_rows = maybe_load_csv(reference_summary_csv)
    report_md.write_text(build_markdown(summary_rows, raw_rows, reference_rows), encoding="utf-8")


if __name__ == "__main__":
    main()
