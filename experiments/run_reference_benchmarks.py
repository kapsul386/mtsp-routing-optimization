from __future__ import annotations

import argparse
import csv
import json
import math
import time
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_csv(path: Path) -> list[dict]:
    with path.open("r", encoding="utf-8", newline="") as fh:
        return list(csv.DictReader(fh))


def write_csv(path: Path, rows: list[dict], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def resolve_path(path_str: str) -> Path:
    path = Path(path_str)
    return path if path.is_absolute() else ROOT / path


def read_instance(path: Path) -> tuple[int, int, list[tuple[float, float]]]:
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    node_count, salesman_count = map(int, lines[0].split())
    coords = [tuple(map(float, line.split())) for line in lines[1:]]
    if len(coords) != node_count:
        raise ValueError(f"{path}: expected {node_count} coordinates, got {len(coords)}")
    return node_count, salesman_count, coords


def euclidean(coords: list[tuple[float, float]], a: int, b: int) -> float:
    ax, ay = coords[a]
    bx, by = coords[b]
    return math.hypot(ax - bx, ay - by)


def route_length(coords: list[tuple[float, float]], route: list[int]) -> float:
    return sum(euclidean(coords, left, right) for left, right in zip(route, route[1:]))


def validate_routes(routes: list[list[int]], node_count: int) -> bool:
    if any(not route or route[0] != 0 or route[-1] != 0 for route in routes):
        return False
    visited = [node for route in routes for node in route[1:-1]]
    return sorted(visited) == list(range(1, node_count))


def build_distance_matrix(coords: list[tuple[float, float]], scale: int) -> list[list[int]]:
    matrix: list[list[int]] = []
    for i in range(len(coords)):
        row = [int(round(scale * euclidean(coords, i, j))) for j in range(len(coords))]
        matrix.append(row)
    return matrix


def enum_value(enum_cls: object, name: str) -> int:
    try:
        return getattr(enum_cls, name)
    except AttributeError as exc:
        raise ValueError(f"Unknown OR-Tools enum value: {name}") from exc


def set_duration_seconds(duration: object, seconds: float) -> None:
    whole_seconds = int(seconds)
    nanos = int(round((seconds - whole_seconds) * 1_000_000_000))
    duration.seconds = whole_seconds
    duration.nanos = nanos


def solve_instance_with_ortools(instance_path: Path, reference_cfg: dict) -> dict:
    try:
        from ortools.constraint_solver import pywrapcp, routing_enums_pb2
    except ImportError as exc:
        raise RuntimeError(
            "OR-Tools is not installed. Install it with `python -m pip install ortools` "
            "before running experiments/run_reference_benchmarks.py."
        ) from exc

    node_count, salesman_count, coords = read_instance(instance_path)
    scale = int(reference_cfg.get("distance_scale", 1000))
    distance_matrix = build_distance_matrix(coords, scale)

    manager = pywrapcp.RoutingIndexManager(node_count, salesman_count, 0)
    routing = pywrapcp.RoutingModel(manager)

    def transit_callback(from_index: int, to_index: int) -> int:
        from_node = manager.IndexToNode(from_index)
        to_node = manager.IndexToNode(to_index)
        return distance_matrix[from_node][to_node]

    transit_callback_index = routing.RegisterTransitCallback(transit_callback)
    routing.SetArcCostEvaluatorOfAllVehicles(transit_callback_index)

    search_parameters = pywrapcp.DefaultRoutingSearchParameters()
    search_parameters.first_solution_strategy = enum_value(
        routing_enums_pb2.FirstSolutionStrategy,
        reference_cfg.get("first_solution_strategy", "PARALLEL_CHEAPEST_INSERTION"),
    )
    search_parameters.local_search_metaheuristic = enum_value(
        routing_enums_pb2.LocalSearchMetaheuristic,
        reference_cfg.get("local_search_metaheuristic", "GUIDED_LOCAL_SEARCH"),
    )
    search_parameters.log_search = bool(reference_cfg.get("log_search", False))
    set_duration_seconds(search_parameters.time_limit, float(reference_cfg.get("time_limit_seconds", 5)))

    started_at = time.perf_counter()
    assignment = routing.SolveWithParameters(search_parameters)
    elapsed = time.perf_counter() - started_at
    if assignment is None:
        return {
            "instance": instance_path.name,
            "path": str(instance_path),
            "node_count": node_count,
            "salesman_count": salesman_count,
            "solver": reference_cfg.get("name", "ortools-gls"),
            "objective": "",
            "time_seconds": round(elapsed, 6),
            "valid": False,
            "routes": "[]",
            "status": "no_solution",
        }

    routes: list[list[int]] = []
    for vehicle_id in range(salesman_count):
        index = routing.Start(vehicle_id)
        route = [0]
        while not routing.IsEnd(index):
            next_index = assignment.Value(routing.NextVar(index))
            next_node = manager.IndexToNode(next_index)
            route.append(next_node)
            index = next_index
        routes.append(route)

    objective = sum(route_length(coords, route) for route in routes)
    valid = validate_routes(routes, node_count)

    return {
        "instance": instance_path.name,
        "path": str(instance_path),
        "node_count": node_count,
        "salesman_count": salesman_count,
        "solver": reference_cfg.get("name", "ortools-gls"),
        "objective": round(objective, 6),
        "time_seconds": round(elapsed, 6),
        "valid": valid,
        "routes": json.dumps(routes, ensure_ascii=False),
        "status": "ok" if valid else "invalid",
    }


def build_reference_rows(source_rows: list[dict], reference_cfg: dict, existing_rows: list[dict]) -> list[dict]:
    existing_by_path = {row["path"]: row for row in existing_rows}
    instances: dict[str, dict] = {}
    for row in source_rows:
        resolved_path = resolve_path(row["path"])
        instances[str(resolved_path)] = {
            "absolute_path": resolved_path,
            "display_path": row["path"],
        }

    merged_by_path = dict(existing_by_path)
    for item in sorted(instances.values(), key=lambda value: str(value["absolute_path"])):
        if item["display_path"] in merged_by_path:
            continue
        solved = solve_instance_with_ortools(item["absolute_path"], reference_cfg)
        solved["path"] = item["display_path"]
        merged_by_path[item["display_path"]] = solved
    return [merged_by_path[key] for key in sorted(merged_by_path)]


def build_comparison_rows(source_rows: list[dict], reference_rows: list[dict]) -> list[dict]:
    reference_by_path = {row["path"]: row for row in reference_rows}
    rows = []

    for row in source_rows:
        reference_row = reference_by_path[row["path"]]
        solver_objective = float(row["objective"])
        if reference_row["objective"] == "":
            gap = ""
            gap_percent = ""
        else:
            reference_objective = float(reference_row["objective"])
            gap = round(solver_objective - reference_objective, 6)
            gap_percent = round((gap / reference_objective) * 100.0, 6) if reference_objective else 0.0

        rows.append(
            {
                "instance": row["instance"],
                "path": row["path"],
                "node_count": row["node_count"],
                "salesman_count": row["salesman_count"],
                "solver": row["solver"],
                "objective": row["objective"],
                "time_seconds": row["time_seconds"],
                "valid": row["valid"],
                "reference_solver": reference_row["solver"],
                "reference_objective": reference_row["objective"],
                "reference_time_seconds": reference_row["time_seconds"],
                "reference_valid": reference_row["valid"],
                "objective_gap": gap,
                "relative_gap_percent": gap_percent,
            }
        )

    return rows


def aggregate_comparison_rows(rows: list[dict]) -> list[dict]:
    grouped: dict[tuple[int, int, str], list[dict]] = defaultdict(list)
    for row in rows:
        grouped[(int(row["node_count"]), int(row["salesman_count"]), row["solver"])].append(row)

    summary = []
    for (node_count, salesman_count, solver), items in sorted(grouped.items()):
        objectives = [float(item["objective"]) for item in items]
        times = [float(item["time_seconds"]) for item in items]
        reference_objectives = [float(item["reference_objective"]) for item in items if item["reference_objective"] != ""]
        reference_times = [float(item["reference_time_seconds"]) for item in items]
        gaps = [float(item["objective_gap"]) for item in items if item["objective_gap"] != ""]
        gap_percents = [float(item["relative_gap_percent"]) for item in items if item["relative_gap_percent"] != ""]
        summary.append(
            {
                "node_count": node_count,
                "salesman_count": salesman_count,
                "solver": solver,
                "runs": len(items),
                "avg_objective": round(sum(objectives) / len(objectives), 6),
                "avg_time_seconds": round(sum(times) / len(times), 6),
                "reference_solver": items[0]["reference_solver"],
                "avg_reference_objective": round(sum(reference_objectives) / len(reference_objectives), 6)
                if reference_objectives else "",
                "avg_reference_time_seconds": round(sum(reference_times) / len(reference_times), 6),
                "avg_objective_gap": round(sum(gaps) / len(gaps), 6) if gaps else "",
                "avg_relative_gap_percent": round(sum(gap_percents) / len(gap_percents), 6) if gap_percents else "",
                "valid_runs": sum(1 for item in items if str(item["valid"]).lower() == "true"),
                "reference_valid_runs": sum(1 for item in items if str(item["reference_valid"]).lower() == "true"),
                "better_than_reference_runs": sum(
                    1 for item in items
                    if item["objective_gap"] != "" and float(item["objective_gap"]) < 0.0
                ),
            }
        )
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description="Run an external OR-Tools reference baseline for mTSP.")
    parser.add_argument(
        "--config",
        default="experiments/reference_config.json",
        help="Path to the external reference experiment config JSON.",
    )
    args = parser.parse_args()

    config = json.loads(resolve_path(args.config).read_text(encoding="utf-8"))
    source_rows = load_csv(resolve_path(config["source_results_csv"]))
    reference_results_csv = resolve_path(config["reference_results_csv"])
    reference_source_csv = (
        resolve_path(config["reference_results_source_csv"])
        if "reference_results_source_csv" in config
        else reference_results_csv
    )
    existing_rows = load_csv(reference_source_csv) if reference_source_csv.exists() else []

    reference_rows = build_reference_rows(source_rows, config["reference"], existing_rows)
    comparison_rows = build_comparison_rows(source_rows, reference_rows)
    summary_rows = aggregate_comparison_rows(comparison_rows)

    write_csv(
        reference_results_csv,
        reference_rows,
        [
            "instance",
            "path",
            "node_count",
            "salesman_count",
            "solver",
            "objective",
            "time_seconds",
            "valid",
            "routes",
            "status",
        ],
    )
    write_csv(
        resolve_path(config["comparison_csv"]),
        comparison_rows,
        [
            "instance",
            "path",
            "node_count",
            "salesman_count",
            "solver",
            "objective",
            "time_seconds",
            "valid",
            "reference_solver",
            "reference_objective",
            "reference_time_seconds",
            "reference_valid",
            "objective_gap",
            "relative_gap_percent",
        ],
    )
    write_csv(
        resolve_path(config["summary_csv"]),
        summary_rows,
        [
            "node_count",
            "salesman_count",
            "solver",
            "runs",
            "avg_objective",
            "avg_time_seconds",
            "reference_solver",
            "avg_reference_objective",
            "avg_reference_time_seconds",
            "avg_objective_gap",
            "avg_relative_gap_percent",
            "valid_runs",
            "reference_valid_runs",
            "better_than_reference_runs",
        ],
    )


if __name__ == "__main__":
    main()
