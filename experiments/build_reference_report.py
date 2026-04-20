from __future__ import annotations

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def resolve_path(path_str: str) -> Path:
    path = Path(path_str)
    return path if path.is_absolute() else ROOT / path


def load_csv(path: Path) -> list[dict]:
    with path.open("r", encoding="utf-8", newline="") as fh:
        return list(csv.DictReader(fh))


def maybe_float(value: str) -> float | None:
    if value == "":
        return None
    return float(value)


def display_value(value: str) -> str:
    return value if value != "" else "n/a"


def is_true(value: object) -> bool:
    return str(value).lower() == "true"


def build_markdown(summary_rows: list[dict], comparison_rows: list[dict]) -> str:
    grouped_by_size: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for row in summary_rows:
        grouped_by_size[(row["node_count"], row["salesman_count"])].append(row)

    lines: list[str] = []
    lines.append("# Сравнение с внешним reference baseline")
    lines.append("")
    lines.append(
        "Этот отчет сравнивает наши mTSP-эвристики с внешним решением OR-Tools (`GUIDED_LOCAL_SEARCH`) "
        "на том же наборе инстансов."
    )
    lines.append("")
    lines.append(
        "Используется один канонический reference CSV: `data/results/mtsp_reference_results.csv`."
    )
    lines.append("")
    lines.append("## Агрегированные таблицы")
    lines.append("")

    for (node_count, salesman_count), rows in sorted(grouped_by_size.items(), key=lambda item: (int(item[0][0]), int(item[0][1]))):
        rows = sorted(
            rows,
            key=lambda row: maybe_float(row["avg_objective_gap"])
            if maybe_float(row["avg_objective_gap"]) is not None else float("inf"),
        )
        lines.append(f"### n={node_count}, m={salesman_count}")
        lines.append("")
        lines.append("| Solver | Runs | Our | OR-Tools | Gap | Gap % | Time (our/ref) | Valid Runs |")
        lines.append("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
        for row in rows:
            lines.append(
                f"| {row['solver']} | {display_value(row['runs'])} | {display_value(row['avg_objective'])} | "
                f"{display_value(row['avg_reference_objective'])} | {display_value(row['avg_objective_gap'])} | "
                f"{display_value(row['avg_relative_gap_percent'])} | "
                f"{display_value(row['avg_time_seconds'])} / {display_value(row['avg_reference_time_seconds'])} | "
                f"{display_value(row['valid_runs'])} |"
            )
        lines.append("")

    lines.append("## Краткие наблюдения")
    lines.append("")

    best_gap_counts: dict[str, int] = defaultdict(int)
    for rows in grouped_by_size.values():
        valid_rows = [row for row in rows if maybe_float(row["avg_objective_gap"]) is not None]
        if not valid_rows:
            continue
        best = min(valid_rows, key=lambda row: float(row["avg_objective_gap"]))
        best_gap_counts[best["solver"]] += 1

    if best_gap_counts:
        ranking = sorted(best_gap_counts.items(), key=lambda item: item[1], reverse=True)
        lines.append(
            "По минимальному среднему gap к OR-Tools лидеры распределились так: "
            + ", ".join(f"`{solver}` - {count}" for solver, count in ranking)
            + "."
        )
        lines.append("")

    overall_grouped: dict[str, list[dict]] = defaultdict(list)
    for row in comparison_rows:
        if is_true(row["valid"]) and is_true(row["reference_valid"]) and row["objective_gap"] != "":
            overall_grouped[row["solver"]].append(row)

    if overall_grouped:
        lines.append("Средние значения по всем сопоставимым валидным прогонам:")
        lines.append("")
        solver_means = []
        for solver, rows in overall_grouped.items():
            avg_gap = sum(float(row["objective_gap"]) for row in rows) / len(rows)
            avg_gap_percent = sum(float(row["relative_gap_percent"]) for row in rows) / len(rows)
            avg_our = sum(float(row["objective"]) for row in rows) / len(rows)
            avg_ref = sum(float(row["reference_objective"]) for row in rows) / len(rows)
            avg_time = sum(float(row["time_seconds"]) for row in rows) / len(rows)
            avg_ref_time = sum(float(row["reference_time_seconds"]) for row in rows) / len(rows)
            solver_means.append((solver, avg_gap, avg_gap_percent, avg_our, avg_ref, avg_time, avg_ref_time))
        solver_means.sort(key=lambda item: item[1])

        for solver, avg_gap, avg_gap_percent, avg_our, avg_ref, avg_time, avg_ref_time in solver_means:
            lines.append(
                f"- `{solver}`: our = {avg_our:.6f} | OR-Tools = {avg_ref:.6f} | "
                f"gap = {avg_gap:.6f} | time = {avg_time:.6f} / {avg_ref_time:.6f} s "
                f"(gap % = {avg_gap_percent:.6f})."
            )
        lines.append("")

    lines.append("## Интерпретация")
    lines.append("")
    lines.append(
        "Главный формат сравнения здесь фиксирован: `our | OR-Tools | gap | time`."
    )
    lines.append(
        "Если новая версия `lkh-wrapper` уменьшает gap на том же canonical reference CSV, "
        "значит улучшение действительно содержательное, а не случайное."
    )
    lines.append("")

    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build an mTSP report against the external OR-Tools baseline.")
    parser.add_argument(
        "--config",
        default="experiments/reference_config.json",
        help="Path to the external reference experiment config JSON.",
    )
    args = parser.parse_args()

    config = json.loads(resolve_path(args.config).read_text(encoding="utf-8"))
    summary_rows = load_csv(resolve_path(config["summary_csv"]))
    comparison_rows = load_csv(resolve_path(config["comparison_csv"]))
    report_path = resolve_path(config["report_md"])
    report_path.write_text(build_markdown(summary_rows, comparison_rows), encoding="utf-8")


if __name__ == "__main__":
    main()
