from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
if str(ROOT / "experiments") not in sys.path:
    sys.path.insert(0, str(ROOT / "experiments"))

from baseline_policy import allowed_baselines, build_policy, choose_best_candidate, select_tier
from mtsp_experiment_utils import ensure_instance_family, instance_family_sort_key
from python.cpp_updater import get_executable_path, recompiles_if_necessary
from python.mtsp_baselines.common import load_csv, normalize_display_path, resolve_path, write_csv
from python.mtsp_baselines.exact_mip import solve as solve_exact_mip
from python.mtsp_baselines.ortools_routing import solve as solve_ortools_routing
from python.mtsp_baselines.tsp_transform_lkh import solve as solve_tsp_transform_lkh


BASELINE_ADAPTERS = {
    "exact-mip": solve_exact_mip,
    "ortools-gls": solve_ortools_routing,
    "tsp-transform-lkh": solve_tsp_transform_lkh,
}


def is_true(value: object) -> bool:
    return str(value).lower() == "true"


def prepare_runtime(config: dict, policy) -> dict:
    configured = set(config.get("baselines", {}).keys())
    allowed = set(policy.tiers.get("small", [])) | set(policy.tiers.get("medium", [])) | set(policy.tiers.get("large", []))
    requested = configured & allowed

    runtime: dict[str, object] = {}
    if "tsp-transform-lkh" in requested:
        tsp_executable = get_executable_path("tsp")
        recompiles_if_necessary(exe_path=tsp_executable)
        runtime["tsp_executable"] = tsp_executable
    return runtime


def normalize_candidate_row(row: dict) -> dict:
    normalized = ensure_instance_family(row)
    normalized["path"] = normalize_display_path(resolve_path(str(normalized["path"])))
    normalized["baseline_solver"] = str(normalized["baseline_solver"])
    normalized["baseline_tier"] = str(normalized.get("baseline_tier", "") or "")
    normalized["status"] = str(normalized.get("status", "") or "")
    normalized["is_exact"] = is_true(normalized.get("is_exact", False)) if isinstance(normalized.get("is_exact"), str) else bool(normalized.get("is_exact", False))
    return normalized


def collect_instances(source_rows: list[dict]) -> dict[str, dict]:
    instances: dict[str, dict] = {}
    for row in source_rows:
        normalized_row = ensure_instance_family(row)
        absolute_path = resolve_path(str(normalized_row["path"]))
        display_path = normalize_display_path(absolute_path)
        instances[display_path] = {
            "instance_family": normalized_row["instance_family"],
            "instance": absolute_path.name,
            "display_path": display_path,
            "absolute_path": absolute_path,
            "node_count": int(normalized_row["node_count"]),
            "salesman_count": int(normalized_row["salesman_count"]),
        }
    return instances


def build_candidate_rows(source_rows: list[dict], config: dict, existing_rows: list[dict], policy, runtime: dict) -> list[dict]:
    instances = collect_instances(source_rows)
    baseline_cfgs = config.get("baselines", {})
    existing_by_key = {}
    for row in existing_rows:
        normalized = normalize_candidate_row(row)
        instance = instances.get(str(normalized["path"]))
        if instance is None:
            continue
        baseline_name = str(normalized["baseline_solver"])
        if baseline_name not in baseline_cfgs:
            continue
        if baseline_name not in allowed_baselines(instance["node_count"], policy):
            continue
        existing_by_key[(normalized["path"], normalized["baseline_solver"])] = normalized

    candidates_by_key = dict(existing_by_key)
    for item in sorted(instances.values(), key=lambda value: value["display_path"]):
        tier = select_tier(item["node_count"], policy)
        for baseline_name in allowed_baselines(item["node_count"], policy):
            if baseline_name not in baseline_cfgs:
                raise RuntimeError(f"Baseline `{baseline_name}` is allowed by policy but not configured.")
            if baseline_name not in BASELINE_ADAPTERS:
                raise RuntimeError(f"Baseline `{baseline_name}` is not implemented.")

            key = (item["display_path"], baseline_name)
            if key in candidates_by_key:
                candidates_by_key[key]["instance_family"] = item["instance_family"]
                candidates_by_key[key]["baseline_tier"] = tier
                continue

            result = BASELINE_ADAPTERS[baseline_name](item["absolute_path"], baseline_cfgs[baseline_name], runtime)
            candidates_by_key[key] = {
                "instance_family": item["instance_family"],
                "instance": item["instance"],
                "path": item["display_path"],
                "node_count": item["node_count"],
                "salesman_count": item["salesman_count"],
                "baseline_solver": baseline_name,
                "baseline_tier": tier,
                "objective": result["objective"] if result["valid"] else "",
                "time_seconds": result["time_seconds"],
                "valid": bool(result["valid"]),
                "status": result["status"],
                "is_exact": bool(result.get("is_exact", False)),
                "routes": result.get("routes", "[]"),
            }

    return [
        candidates_by_key[key]
        for key in sorted(
            candidates_by_key,
            key=lambda item: (
                item[0],
                item[1],
            ),
        )
    ]


def build_best_rows(candidate_rows: list[dict]) -> list[dict]:
    grouped: dict[str, list[dict]] = defaultdict(list)
    for row in candidate_rows:
        grouped[str(row["path"])].append(row)

    best_rows: list[dict] = []
    for path_key, rows in sorted(grouped.items()):
        best = choose_best_candidate(rows)
        representative = rows[0]
        if best is None:
            best_rows.append(
                {
                    "instance_family": representative["instance_family"],
                    "instance": representative["instance"],
                    "path": representative["path"],
                    "node_count": representative["node_count"],
                    "salesman_count": representative["salesman_count"],
                    "baseline_tier": representative["baseline_tier"],
                    "best_baseline_solver": "",
                    "best_baseline_objective": "",
                    "best_baseline_time_seconds": "",
                    "best_baseline_valid": False,
                    "best_baseline_status": "no_valid_baseline",
                    "best_baseline_is_exact": False,
                }
            )
            continue

        best_rows.append(
            {
                "instance_family": best["instance_family"],
                "instance": best["instance"],
                "path": best["path"],
                "node_count": best["node_count"],
                "salesman_count": best["salesman_count"],
                "baseline_tier": best["baseline_tier"],
                "best_baseline_solver": best["baseline_solver"],
                "best_baseline_objective": best["objective"],
                "best_baseline_time_seconds": best["time_seconds"],
                "best_baseline_valid": best["valid"],
                "best_baseline_status": best["status"],
                "best_baseline_is_exact": best["is_exact"],
            }
        )
    return best_rows


def build_comparison_rows(source_rows: list[dict], best_rows: list[dict]) -> list[dict]:
    best_by_path = {str(row["path"]): row for row in best_rows}
    rows: list[dict] = []
    for row in source_rows:
        normalized_row = ensure_instance_family(row)
        display_path = normalize_display_path(resolve_path(str(normalized_row["path"])))
        best_row = best_by_path.get(display_path)
        if best_row is None:
            continue

        solver_valid = is_true(normalized_row["valid"])
        baseline_valid = is_true(best_row["best_baseline_valid"])
        if solver_valid and baseline_valid and str(best_row["best_baseline_objective"]) not in ("", "None"):
            solver_objective = float(normalized_row["objective"])
            baseline_objective = float(best_row["best_baseline_objective"])
            objective_gap = round(solver_objective - baseline_objective, 6)
            relative_gap_percent = round((objective_gap / baseline_objective) * 100.0, 6) if baseline_objective else 0.0
        else:
            objective_gap = ""
            relative_gap_percent = ""

        rows.append(
            {
                "instance_family": normalized_row["instance_family"],
                "instance": normalized_row["instance"],
                "path": display_path,
                "node_count": normalized_row["node_count"],
                "salesman_count": normalized_row["salesman_count"],
                "solver": normalized_row["solver"],
                "objective": normalized_row["objective"],
                "time_seconds": normalized_row["time_seconds"],
                "valid": normalized_row["valid"],
                "best_baseline_solver": best_row["best_baseline_solver"],
                "best_baseline_objective": best_row["best_baseline_objective"],
                "best_baseline_time_seconds": best_row["best_baseline_time_seconds"],
                "best_baseline_valid": best_row["best_baseline_valid"],
                "best_baseline_status": best_row["best_baseline_status"],
                "objective_gap": objective_gap,
                "relative_gap_percent": relative_gap_percent,
            }
        )
    return rows


def aggregate_comparison_rows(rows: list[dict]) -> list[dict]:
    grouped: dict[tuple[str, int, int, str], list[dict]] = defaultdict(list)
    for row in rows:
        normalized_row = ensure_instance_family(row)
        grouped[
            (
                str(normalized_row["instance_family"]),
                int(normalized_row["node_count"]),
                int(normalized_row["salesman_count"]),
                str(normalized_row["solver"]),
            )
        ].append(normalized_row)

    summary_rows: list[dict] = []
    for (instance_family, node_count, salesman_count, solver), items in sorted(
        grouped.items(),
        key=lambda item: (
            instance_family_sort_key(item[0][0]),
            item[0][1],
            item[0][2],
            item[0][3],
        ),
    ):
        valid_solver_rows = [item for item in items if is_true(item["valid"]) and str(item["objective"]) not in ("", "None")]
        comparable_rows = [
            item for item in items
            if is_true(item["valid"]) and is_true(item["best_baseline_valid"]) and str(item["objective_gap"]) not in ("", "None")
        ]
        avg_objective = (
            round(sum(float(item["objective"]) for item in valid_solver_rows) / len(valid_solver_rows), 6)
            if valid_solver_rows else ""
        )
        avg_time = round(sum(float(item["time_seconds"]) for item in items) / len(items), 6)
        avg_baseline_objective = (
            round(sum(float(item["best_baseline_objective"]) for item in comparable_rows) / len(comparable_rows), 6)
            if comparable_rows else ""
        )
        avg_baseline_time = (
            round(sum(float(item["best_baseline_time_seconds"]) for item in comparable_rows) / len(comparable_rows), 6)
            if comparable_rows else ""
        )
        avg_gap = (
            round(sum(float(item["objective_gap"]) for item in comparable_rows) / len(comparable_rows), 6)
            if comparable_rows else ""
        )
        avg_gap_percent = (
            round(sum(float(item["relative_gap_percent"]) for item in comparable_rows) / len(comparable_rows), 6)
            if comparable_rows else ""
        )

        summary_rows.append(
            {
                "instance_family": instance_family,
                "node_count": node_count,
                "salesman_count": salesman_count,
                "solver": solver,
                "runs": len(items),
                "valid_runs": len(valid_solver_rows),
                "best_baseline_valid_runs": len(comparable_rows),
                "avg_objective": avg_objective,
                "avg_time_seconds": avg_time,
                "avg_best_baseline_objective": avg_baseline_objective,
                "avg_best_baseline_time_seconds": avg_baseline_time,
                "avg_objective_gap": avg_gap,
                "avg_relative_gap_percent": avg_gap_percent,
                "better_than_best_baseline_runs": sum(float(item["objective_gap"]) < 0.0 for item in comparable_rows),
            }
        )
    return summary_rows


def main() -> None:
    parser = argparse.ArgumentParser(description="Run a hybrid mTSP baseline pipeline and compare against the best baseline.")
    parser.add_argument("--config", default="experiments/hybrid_reference_config.json", help="Path to hybrid baseline config.")
    args = parser.parse_args()

    config_path = resolve_path(args.config)
    config = json.loads(config_path.read_text(encoding="utf-8-sig"))
    policy = build_policy(config["policy"])
    runtime = prepare_runtime(config, policy)

    source_rows = load_csv(resolve_path(config["source_results_csv"]))
    existing_candidates = []
    candidates_path = resolve_path(config["baseline_candidates_csv"])
    if bool(config.get("reuse_existing_candidates", True)) and candidates_path.exists():
        existing_candidates = load_csv(candidates_path)

    candidate_rows = build_candidate_rows(source_rows, config, existing_candidates, policy, runtime)
    best_rows = build_best_rows(candidate_rows)
    comparison_rows = build_comparison_rows(source_rows, best_rows)
    summary_rows = aggregate_comparison_rows(comparison_rows)

    write_csv(
        candidates_path,
        candidate_rows,
        [
            "instance_family",
            "instance",
            "path",
            "node_count",
            "salesman_count",
            "baseline_solver",
            "baseline_tier",
            "objective",
            "time_seconds",
            "valid",
            "status",
            "is_exact",
            "routes",
        ],
    )
    write_csv(
        resolve_path(config["best_baseline_csv"]),
        best_rows,
        [
            "instance_family",
            "instance",
            "path",
            "node_count",
            "salesman_count",
            "baseline_tier",
            "best_baseline_solver",
            "best_baseline_objective",
            "best_baseline_time_seconds",
            "best_baseline_valid",
            "best_baseline_status",
            "best_baseline_is_exact",
        ],
    )
    write_csv(
        resolve_path(config["comparison_csv"]),
        comparison_rows,
        [
            "instance_family",
            "instance",
            "path",
            "node_count",
            "salesman_count",
            "solver",
            "objective",
            "time_seconds",
            "valid",
            "best_baseline_solver",
            "best_baseline_objective",
            "best_baseline_time_seconds",
            "best_baseline_valid",
            "best_baseline_status",
            "objective_gap",
            "relative_gap_percent",
        ],
    )
    write_csv(
        resolve_path(config["summary_csv"]),
        summary_rows,
        [
            "instance_family",
            "node_count",
            "salesman_count",
            "solver",
            "runs",
            "valid_runs",
            "best_baseline_valid_runs",
            "avg_objective",
            "avg_time_seconds",
            "avg_best_baseline_objective",
            "avg_best_baseline_time_seconds",
            "avg_objective_gap",
            "avg_relative_gap_percent",
            "better_than_best_baseline_runs",
        ],
    )


if __name__ == "__main__":
    main()
