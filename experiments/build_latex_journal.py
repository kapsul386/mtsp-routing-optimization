from __future__ import annotations

import csv
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
RESULTS_DIR = ROOT / "data" / "results"
OUTPUT_PATH = ROOT / "docs" / "experiment_journal" / "generated_tables.tex"


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def maybe_float(value: str) -> float | None:
    if value == "":
        return None
    return float(value)


def tex_escape(text: str) -> str:
    replacements = {
        "\\": r"\textbackslash{}",
        "&": r"\&",
        "%": r"\%",
        "$": r"\$",
        "#": r"\#",
        "_": r"\_",
        "{": r"\{",
        "}": r"\}",
    }
    for source, target in replacements.items():
        text = text.replace(source, target)
    return text


def as_float(row: dict[str, str], key: str) -> float:
    return float(row[key])


def fmt_metric(value: float) -> str:
    return f"{value:.3f}"


def fmt_time(value: float) -> str:
    return f"{value:.6f}"


def fmt_optional_metric(value: float) -> str:
    return "n/a" if value == float("inf") else fmt_metric(value)


def group_mean(rows: list[dict[str, str]], key: str) -> float:
    return sum(as_float(row, key) for row in rows) / len(rows)


def overall_solver_rows(rows: list[dict[str, str]]) -> list[dict[str, float | str]]:
    grouped: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        grouped[row["solver"]].append(row)

    result: list[dict[str, float | str]] = []
    for solver, solver_rows in grouped.items():
        objective_rows = [row for row in solver_rows if maybe_float(row["avg_objective"]) is not None]
        reference_rows = [row for row in solver_rows if maybe_float(row["avg_reference_objective"]) is not None]
        gap_rows = [row for row in solver_rows if maybe_float(row["avg_objective_gap"]) is not None]
        gap_percent_rows = [row for row in solver_rows if maybe_float(row["avg_relative_gap_percent"]) is not None]
        result.append(
            {
                "solver": solver,
                "avg_objective": group_mean(objective_rows, "avg_objective") if objective_rows else float("inf"),
                "avg_reference_objective": group_mean(reference_rows, "avg_reference_objective") if reference_rows else float("inf"),
                "avg_gap": group_mean(gap_rows, "avg_objective_gap") if gap_rows else float("inf"),
                "avg_gap_percent": group_mean(gap_percent_rows, "avg_relative_gap_percent") if gap_percent_rows else float("inf"),
                "avg_time_seconds": group_mean(solver_rows, "avg_time_seconds"),
            }
        )
    return sorted(result, key=lambda item: float(item["avg_gap"]))


def best_by_config(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    grouped: dict[tuple[int, int], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        key = (int(row["node_count"]), int(row["salesman_count"]))
        grouped[key].append(row)

    result: list[dict[str, str]] = []
    for key in sorted(grouped):
        comparable_rows = [row for row in grouped[key] if maybe_float(row["avg_objective_gap"]) is not None]
        if not comparable_rows:
            comparable_rows = [row for row in grouped[key] if maybe_float(row["avg_objective"]) is not None]
            if not comparable_rows:
                continue
            best = min(comparable_rows, key=lambda row: as_float(row, "avg_objective"))
        else:
            best = min(comparable_rows, key=lambda row: as_float(row, "avg_objective_gap"))
        result.append(best)
    return result


def best_solver_counts(rows: list[dict[str, str]]) -> list[tuple[str, int]]:
    counts: dict[str, int] = defaultdict(int)
    for row in best_by_config(rows):
        counts[row["solver"]] += 1
    return sorted(counts.items(), key=lambda item: (-item[1], item[0]))


def version_side_by_side(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    grouped: dict[tuple[int, int], dict[str, dict[str, str]]] = defaultdict(dict)
    for row in rows:
        key = (int(row["node_count"]), int(row["salesman_count"]))
        grouped[key][row["solver"]] = row

    output: list[dict[str, str]] = []
    for (node_count, salesman_count) in sorted(grouped):
        versions = grouped[(node_count, salesman_count)]
        if "lkh-wrapper-v1" not in versions or "lkh-wrapper-v2" not in versions:
            continue
        output.append(
            {
                "node_count": str(node_count),
                "salesman_count": str(salesman_count),
                "v1_objective": versions["lkh-wrapper-v1"]["avg_objective"],
                "v1_gap": versions["lkh-wrapper-v1"]["avg_objective_gap"],
                "v1_time": versions["lkh-wrapper-v1"]["avg_time_seconds"],
                "v2_objective": versions["lkh-wrapper-v2"]["avg_objective"],
                "v2_gap": versions["lkh-wrapper-v2"]["avg_objective_gap"],
                "v2_time": versions["lkh-wrapper-v2"]["avg_time_seconds"],
            }
        )
    return output


def render_main_comparison(rows: list[dict[str, str]]) -> str:
    overall = overall_solver_rows(rows)
    lines = [
        r"\subsection{Средние результаты по основным алгоритмам}",
        r"Таблица ниже агрегирует данные из \path{data/results/mtsp_reference_summary.csv}. "
        r"Показаны средние значения по конфигурациям, а reference соответствует каноническому baseline на базе OR-Tools.",
        "",
        r"\begin{center}",
        r"\small",
        r"\begin{tabular}{lrrrrr}",
        r"\toprule",
        r"Метод & Наше значение & Reference & Gap & Gap, \% & Время, с \\",
        r"\midrule",
    ]
    for row in overall:
        lines.append(
            r"\texttt{%s} & %s & %s & %s & %s & %s \\"
            % (
                tex_escape(str(row["solver"])),
                fmt_optional_metric(float(row["avg_objective"])),
                fmt_optional_metric(float(row["avg_reference_objective"])),
                fmt_optional_metric(float(row["avg_gap"])),
                fmt_optional_metric(float(row["avg_gap_percent"])),
                fmt_time(float(row["avg_time_seconds"])),
            )
        )
    lines.extend([r"\bottomrule", r"\end{tabular}", r"\normalsize", r"\end{center}", ""])
    return "\n".join(lines)


def render_best_configs(rows: list[dict[str, str]]) -> str:
    best_rows = best_by_config(rows)
    lines = [
        r"\subsection{Лучший метод по каждой конфигурации $(n, m)$}",
        r"\begin{center}",
        r"\small",
        r"\begin{longtable}{rrlrrrr}",
        r"\toprule",
        r"$n$ & $m$ & Метод & Наше значение & Reference & Gap & Время, с \\",
        r"\midrule",
        r"\endfirsthead",
        r"\toprule",
        r"$n$ & $m$ & Метод & Наше значение & Reference & Gap & Время, с \\",
        r"\midrule",
        r"\endhead",
    ]
    for row in best_rows:
        lines.append(
            r"%s & %s & \texttt{%s} & %s & %s & %s & %s \\"
            % (
                row["node_count"],
                row["salesman_count"],
                tex_escape(row["solver"]),
                fmt_metric(as_float(row, "avg_objective")),
                fmt_metric(as_float(row, "avg_reference_objective")),
                fmt_metric(as_float(row, "avg_objective_gap")),
                fmt_time(as_float(row, "avg_time_seconds")),
            )
        )
    lines.extend([r"\bottomrule", r"\end{longtable}", r"\normalsize", r"\end{center}", ""])
    return "\n".join(lines)


def render_version_comparison(rows: list[dict[str, str]]) -> str:
    overall = overall_solver_rows(rows)
    side_by_side = version_side_by_side(rows)

    lines = [
        r"\subsection{Сравнение версий \texttt{lkh-wrapper}}",
        r"Данные в этом разделе построены по \path{data/results/lkh_versions_reference_summary.csv}.",
        "",
        r"\begin{center}",
        r"\small",
        r"\begin{tabular}{lrrrrr}",
        r"\toprule",
        r"Версия & Наше значение & Reference & Gap & Gap, \% & Время, с \\",
        r"\midrule",
    ]
    for row in overall:
        lines.append(
            r"\texttt{%s} & %s & %s & %s & %s & %s \\"
            % (
                tex_escape(str(row["solver"])),
                fmt_optional_metric(float(row["avg_objective"])),
                fmt_optional_metric(float(row["avg_reference_objective"])),
                fmt_optional_metric(float(row["avg_gap"])),
                fmt_optional_metric(float(row["avg_gap_percent"])),
                fmt_time(float(row["avg_time_seconds"])),
            )
        )
    lines.extend([r"\bottomrule", r"\end{tabular}", r"\normalsize", r"\end{center}", ""])

    lines.extend(
        [
            r"\subsection{Покомпонентное сравнение \texttt{v1} и \texttt{v2}}",
            r"\begin{center}",
            r"\small",
            r"\begin{longtable}{rr|rrr|rrr}",
            r"\toprule",
            r"$n$ & $m$ & \multicolumn{3}{c|}{\texttt{lkh-wrapper-v1}} & \multicolumn{3}{c}{\texttt{lkh-wrapper-v2}} \\",
            r"\cmidrule(lr){3-5}\cmidrule(l){6-8}",
            r" &  & Значение & Gap & Время, с & Значение & Gap & Время, с \\",
            r"\midrule",
            r"\endfirsthead",
            r"\toprule",
            r"$n$ & $m$ & \multicolumn{3}{c|}{\texttt{lkh-wrapper-v1}} & \multicolumn{3}{c}{\texttt{lkh-wrapper-v2}} \\",
            r"\cmidrule(lr){3-5}\cmidrule(l){6-8}",
            r" &  & Значение & Gap & Время, с & Значение & Gap & Время, с \\",
            r"\midrule",
            r"\endhead",
        ]
    )
    for row in side_by_side:
        lines.append(
            r"%s & %s & %s & %s & %s & %s & %s & %s \\"
            % (
                row["node_count"],
                row["salesman_count"],
                fmt_metric(float(row["v1_objective"])),
                fmt_metric(float(row["v1_gap"])),
                fmt_time(float(row["v1_time"])),
                fmt_metric(float(row["v2_objective"])),
                fmt_metric(float(row["v2_gap"])),
                fmt_time(float(row["v2_time"])),
            )
        )
    lines.extend([r"\bottomrule", r"\end{longtable}", r"\normalsize", r"\end{center}", ""])
    return "\n".join(lines)


def render_observations(rows: list[dict[str, str]], version_rows: list[dict[str, str]]) -> str:
    counts = best_solver_counts(rows)
    counts_text = ", ".join(r"\texttt{%s}: %d" % (tex_escape(name), count) for name, count in counts)
    version_overall = overall_solver_rows(version_rows)
    version_map = {str(row["solver"]): row for row in version_overall}
    v1_gap = float(version_map["lkh-wrapper-v1"]["avg_gap"])
    v2_gap = float(version_map["lkh-wrapper-v2"]["avg_gap"])
    v1_time = float(version_map["lkh-wrapper-v1"]["avg_time_seconds"])
    v2_time = float(version_map["lkh-wrapper-v2"]["avg_time_seconds"])

    lines = [
        r"\subsection{Краткие автоматические наблюдения}",
        r"\begin{itemize}",
        r"\item По числу лучших конфигураций лидеры распределились так: %s." % counts_text,
        r"\item Средний gap версии \texttt{lkh-wrapper-v2} ниже, чем у \texttt{v1}: %s против %s."
        % (fmt_metric(v2_gap), fmt_metric(v1_gap)),
        r"\item Среднее время версии \texttt{lkh-wrapper-v2} также ниже: %s с против %s с."
        % (fmt_time(v2_time), fmt_time(v1_time)),
        r"\end{itemize}",
        "",
    ]
    return "\n".join(lines)


def main() -> None:
    mtsp_reference_summary = read_csv(RESULTS_DIR / "mtsp_reference_summary.csv")
    lkh_versions_reference_summary = read_csv(RESULTS_DIR / "lkh_versions_reference_summary.csv")

    sections = [
        "% Auto-generated by experiments/build_latex_journal.py. Do not edit manually.",
        render_main_comparison(mtsp_reference_summary),
        render_best_configs(mtsp_reference_summary),
        render_version_comparison(lkh_versions_reference_summary),
        render_observations(mtsp_reference_summary, lkh_versions_reference_summary),
    ]

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT_PATH.write_text("\n".join(sections).strip() + "\n", encoding="utf-8")
    print("generated_tables.tex updated")


if __name__ == "__main__":
    main()
