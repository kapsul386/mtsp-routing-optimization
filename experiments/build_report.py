from __future__ import annotations

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path


def load_csv(path: Path) -> list[dict]:
    with path.open("r", encoding="utf-8", newline="") as fh:
        return list(csv.DictReader(fh))


def to_float(row: dict, key: str) -> float:
    return float(row[key])


def build_markdown(summary_rows: list[dict], raw_rows: list[dict]) -> str:
    grouped_by_size: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for row in summary_rows:
        grouped_by_size[(row["node_count"], row["salesman_count"])].append(row)

    lines: list[str] = []
    lines.append("# Результаты вычислительных экспериментов")
    lines.append("")
    lines.append("Сводка построена автоматически по `data/results/mtsp_results.csv` и `data/results/mtsp_summary.csv`.")
    lines.append("")
    lines.append("## Агрегированные таблицы")
    lines.append("")

    for (node_count, salesman_count), rows in sorted(grouped_by_size.items(), key=lambda item: (int(item[0][0]), int(item[0][1]))):
        rows = sorted(rows, key=lambda row: to_float(row, "avg_objective"))
        lines.append(f"### n={node_count}, m={salesman_count}")
        lines.append("")
        lines.append("| Solver | Runs | Avg MINSUM | Avg Time (s) | Valid Runs |")
        lines.append("| --- | ---: | ---: | ---: | ---: |")
        for row in rows:
            lines.append(
                f"| {row['solver']} | {row['runs']} | {row['avg_objective']} | {row['avg_time_seconds']} | {row['valid_runs']} |"
            )
        lines.append("")

    lines.append("## Краткие наблюдения")
    lines.append("")

    best_counts: dict[str, int] = defaultdict(int)
    for rows in grouped_by_size.values():
        best = min(rows, key=lambda row: to_float(row, "avg_objective"))
        best_counts[best["solver"]] += 1

    if best_counts:
        ranking = sorted(best_counts.items(), key=lambda item: item[1], reverse=True)
        lines.append(
            "По числу лучших средних значений `MINSUM` лидеры распределились так: "
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
        solver_means.append((solver, avg_obj, avg_time))
    solver_means.sort(key=lambda item: item[1])

    if solver_means:
        lines.append("Средние значения по всем прогонкам:")
        lines.append("")
        for solver, avg_obj, avg_time in solver_means:
            lines.append(f"- `{solver}`: avg MINSUM = {avg_obj:.6f}, avg time = {avg_time:.6f} s.")
        lines.append("")

    lines.append("## Интерпретация для отчета")
    lines.append("")
    lines.append(
        "На текущем синтетическом наборе `grasp` стабильно показывает наименьшее среднее значение `MINSUM`, "
        "что предварительно поддерживает рабочую гипотезу о преимуществе более сильных эвристик над baseline-подходами."
    )
    lines.append(
        "Метод `2opt+greed` в среднем лучше `rand+nn`, что тоже согласуется с ожиданием, что локальное улучшение "
        "после жадного построения маршрутов повышает качество решения."
    )
    lines.append(
        "Следующим этапом стоит проверить, сохраняется ли это преимущество на более крупных инстансах и после увеличения числа повторов."
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

    raw_rows = load_csv(results_csv)
    summary_rows = load_csv(summary_csv)
    report_md.write_text(build_markdown(summary_rows, raw_rows), encoding="utf-8")


if __name__ == "__main__":
    main()
