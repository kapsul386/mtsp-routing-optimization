from __future__ import annotations

import time
from pathlib import Path

from .common import objective_minsum, read_mtsp_instance, routes_to_json, validate_routes


def _enum_value(enum_cls: object, name: str) -> int:
    try:
        return getattr(enum_cls, name)
    except AttributeError as exc:
        raise ValueError(f"Unknown OR-Tools enum value: {name}") from exc


def _set_duration_seconds(duration: object, seconds: float) -> None:
    whole_seconds = int(seconds)
    nanos = int(round((seconds - whole_seconds) * 1_000_000_000))
    duration.seconds = whole_seconds
    duration.nanos = nanos


def solve(instance_path: Path, cfg: dict, runtime: dict) -> dict:
    try:
        from ortools.constraint_solver import pywrapcp, routing_enums_pb2
    except ImportError as exc:
        raise RuntimeError(
            "OR-Tools is not installed. Install it with `python -m pip install ortools` "
            "before running the hybrid baseline pipeline."
        ) from exc

    instance = read_mtsp_instance(instance_path)
    scale = int(cfg.get("distance_scale", 1000))

    manager = pywrapcp.RoutingIndexManager(instance.node_count, instance.salesman_count, 0)
    routing = pywrapcp.RoutingModel(manager)

    def transit_callback(from_index: int, to_index: int) -> int:
        from_node = manager.IndexToNode(from_index)
        to_node = manager.IndexToNode(to_index)
        ax, ay = instance.coords[from_node]
        bx, by = instance.coords[to_node]
        dx = ax - bx
        dy = ay - by
        return int(round(scale * ((dx * dx + dy * dy) ** 0.5)))

    transit_callback_index = routing.RegisterTransitCallback(transit_callback)
    routing.SetArcCostEvaluatorOfAllVehicles(transit_callback_index)

    search_parameters = pywrapcp.DefaultRoutingSearchParameters()
    search_parameters.first_solution_strategy = _enum_value(
        routing_enums_pb2.FirstSolutionStrategy,
        str(cfg.get("first_solution_strategy", "PARALLEL_CHEAPEST_INSERTION")),
    )
    search_parameters.local_search_metaheuristic = _enum_value(
        routing_enums_pb2.LocalSearchMetaheuristic,
        str(cfg.get("local_search_metaheuristic", "GUIDED_LOCAL_SEARCH")),
    )
    search_parameters.log_search = bool(cfg.get("log_search", False))
    _set_duration_seconds(search_parameters.time_limit, float(cfg.get("time_limit_seconds", 5.0)))

    started_at = time.perf_counter()
    assignment = routing.SolveWithParameters(search_parameters)
    elapsed = time.perf_counter() - started_at
    if assignment is None:
        return {
            "instance": instance_path.name,
            "path": str(instance_path),
            "node_count": instance.node_count,
            "salesman_count": instance.salesman_count,
            "objective": None,
            "time_seconds": round(elapsed, 6),
            "valid": False,
            "status": "no_solution",
            "is_exact": False,
            "routes": "[]",
        }

    routes: list[list[int]] = []
    for vehicle_id in range(instance.salesman_count):
        index = routing.Start(vehicle_id)
        route = [0]
        while not routing.IsEnd(index):
            next_index = assignment.Value(routing.NextVar(index))
            next_node = manager.IndexToNode(next_index)
            route.append(next_node)
            index = next_index
        routes.append(route)

    valid = validate_routes(routes, instance.node_count)
    objective = objective_minsum(instance.coords, routes)
    return {
        "instance": instance_path.name,
        "path": str(instance_path),
        "node_count": instance.node_count,
        "salesman_count": instance.salesman_count,
        "objective": round(objective, 6),
        "time_seconds": round(elapsed, 6),
        "valid": valid,
        "status": "ok" if valid else "invalid",
        "is_exact": False,
        "routes": routes_to_json(routes),
    }
