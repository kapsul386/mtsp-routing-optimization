from __future__ import annotations

import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path


def load_csv(path: Path) -> list[dict]:
    with path.open("r", encoding="utf-8", newline="") as fh:
        return list(csv.DictReader(fh))


def build_markdown(summary_rows: list[dict], raw_rows: list[dict]) -> str:
    grouped: dict[str, list[dict]] = defaultdict(list)
    for row in summary_rows:
        grouped[row["node_count"]].append(row)

    lines: list[str] = []
    lines.append("# Результаты вычислительных экспериментов TSP")
    lines.append("")
    lines.append("Сводка построена автоматически по `data/results/tsp_results.csv` и `data/results/tsp_summary.csv`.")
    lines.append("")

    for node_count in sorted(grouped.keys(), key=int):
        rows = sorted(grouped[node_count], key=lambda row: float(row["avg_length"]))
        lines.append(f"## n={node_count}")
        lines.append("")
        lines.append("| Solver | Runs | Avg Length | Avg Time (s) | Avg Step Time (s) |")
        lines.append("| --- | ---: | ---: | ---: | ---: |")
        for row in rows:
            lines.append(
                f"| {row['solver']} | {row['runs']} | {row['avg_length']} | {row['avg_time_seconds']} | {row['avg_step_time_seconds']} |"
            )
        lines.append("")

    best_counts: dict[str, int] = defaultdict(int)
    for rows in grouped.values():
        best = min(rows, key=lambda row: float(row["avg_length"]))
        best_counts[best["solver"]] += 1

    ranking = sorted(best_counts.items(), key=lambda item: item[1], reverse=True)
    lines.append("## Краткие наблюдения")
    lines.append("")
    lines.append(
        "По числу лучших средних значений длины тура лидеры распределились так: "
        + ", ".join(f"`{solver}` - {count}" for solver, count in ranking)
        + "."
    )
    lines.append("")

    raw_grouped: dict[str, list[dict]] = defaultdict(list)
    for row in raw_rows:
        raw_grouped[row["solver"]].append(row)
    lines.append("Средние значения по всем прогонкам:")
    lines.append("")
    solver_means = []
    for solver, rows in raw_grouped.items():
        solver_means.append(
            (
                solver,
                sum(float(row["length"]) for row in rows) / len(rows),
                sum(float(row["time_seconds"]) for row in rows) / len(rows)
            )
        )
    solver_means.sort(key=lambda item: item[1])
    for solver, avg_length, avg_time in solver_means:
        lines.append(f"- `{solver}`: avg length = {avg_length:.6f}, avg time = {avg_time:.6f} s.")
    lines.append("")
    lines.append(
        "Эти результаты можно использовать как отдельную TSP-линейку в отчете: "
        "она показывает, как ведут себя эвристики и метаэвристики на базовой задаче перед переходом к `mTSP`."
    )
    lines.append("")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build markdown report for TSP benchmarks.")
    parser.add_argument("--config", default="experiments/tsp_config.json", help="Path to TSP benchmark config.")
    args = parser.parse_args()

    config = json.loads(Path(args.config).read_text(encoding="utf-8"))
    summary_rows = load_csv(Path(config["summary_csv"]))
    raw_rows = load_csv(Path(config["results_csv"]))
    Path(config["report_md"]).write_text(build_markdown(summary_rows, raw_rows), encoding="utf-8")


if __name__ == "__main__":
    main()
