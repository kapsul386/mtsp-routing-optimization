from __future__ import annotations

import time
from tempfile import TemporaryDirectory
from pathlib import Path

from python.tsp_runner import run_process, write_euclidean_instance
from python.validate import validate_tour

from .common import MtspInstanceData, objective_minsum, read_mtsp_instance, routes_to_json, validate_routes


def _rotation_starts(customer_count: int, exhaustive_rotation_limit: int, rotation_samples: int) -> list[int]:
    if customer_count <= 1:
        return [0]
    if customer_count <= exhaustive_rotation_limit:
        return list(range(customer_count))

    sample_count = max(1, min(rotation_samples, customer_count))
    starts = {0}
    for idx in range(sample_count):
        starts.add((idx * customer_count) // sample_count)
    return sorted(starts)


def _split_order_exact(order: list[int], instance: MtspInstanceData, route_count: int) -> tuple[float, list[list[int]]]:
    customer_count = len(order)
    if customer_count == 0:
        return 0.0, []

    depot_cost_from = [0.0] * customer_count
    depot_cost_to = [0.0] * customer_count
    edge_prefix = [0.0] * customer_count
    for idx, customer in enumerate(order):
        cx, cy = instance.coords[customer]
        dx, dy = instance.coords[0]
        depot_distance = ((cx - dx) ** 2 + (cy - dy) ** 2) ** 0.5
        depot_cost_from[idx] = depot_distance
        depot_cost_to[idx] = depot_distance
        if idx > 0:
            px, py = instance.coords[order[idx - 1]]
            edge_prefix[idx] = edge_prefix[idx - 1] + (((cx - px) ** 2 + (cy - py) ** 2) ** 0.5)

    used_routes = min(route_count, customer_count)
    dp = [[float("inf")] * customer_count for _ in range(used_routes)]
    parent = [[-1] * customer_count for _ in range(used_routes)]

    for end in range(customer_count):
        dp[0][end] = depot_cost_from[0] + edge_prefix[end] + depot_cost_to[end]

    for route_idx in range(1, used_routes):
        best_value = float("inf")
        best_start = -1
        for end in range(route_idx, customer_count):
            start = end
            candidate = dp[route_idx - 1][start - 1] - edge_prefix[start] + depot_cost_from[start]
            if candidate < best_value:
                best_value = candidate
                best_start = start
            dp[route_idx][end] = edge_prefix[end] + depot_cost_to[end] + best_value
            parent[route_idx][end] = best_start

    segments: list[list[int]] = []
    end = customer_count - 1
    for route_idx in range(used_routes - 1, 0, -1):
        start = parent[route_idx][end]
        if start < 0:
            raise RuntimeError("Could not reconstruct TSP-transform split.")
        segments.append(order[start:end + 1])
        end = start - 1
    segments.append(order[:end + 1])
    segments.reverse()

    routes = [[0] + segment + [0] for segment in segments]
    return dp[used_routes - 1][customer_count - 1], routes


def _best_split_routes(order: list[int], instance: MtspInstanceData, cfg: dict) -> tuple[float, list[list[int]]]:
    exhaustive_rotation_limit = int(cfg.get("exhaustive_rotation_limit", 256))
    rotation_samples = int(cfg.get("rotation_samples", 16))
    best_objective = float("inf")
    best_routes: list[list[int]] = []

    for start in _rotation_starts(len(order), exhaustive_rotation_limit, rotation_samples):
        rotated = order[start:] + order[:start]
        objective, routes = _split_order_exact(rotated, instance, instance.salesman_count)
        if objective < best_objective:
            best_objective = objective
            best_routes = routes

    empty_routes = [[0, 0] for _ in range(max(0, instance.salesman_count - len(best_routes)))]
    return best_objective, best_routes + empty_routes


def solve(instance_path: Path, cfg: dict, runtime: dict) -> dict:
    instance = read_mtsp_instance(instance_path)
    customer_nodes = list(range(1, instance.node_count))

    started_at = time.perf_counter()
    if not customer_nodes:
        routes = [[0, 0] for _ in range(instance.salesman_count)]
        objective = 0.0
        valid = True
        status = "ok"
    elif len(customer_nodes) == 1:
        routes = [[0, customer_nodes[0], 0]] + [[0, 0] for _ in range(max(0, instance.salesman_count - 1))]
        objective = objective_minsum(instance.coords, routes)
        valid = True
        status = "ok"
    else:
        with TemporaryDirectory(prefix="mtsp_tsp_transform_lkh_") as temp_dir:
            temp_dir_path = Path(temp_dir)
            tsp_instance_path = temp_dir_path / "transform_instance.txt"
            write_euclidean_instance(
                tsp_instance_path,
                [instance.coords[node] for node in customer_nodes],
            )
            tsp_output = run_process(
                runtime["tsp_executable"],
                cfg.get("solver_args", ["--step", "lkh", "--start", "0", "--rounds", "24", "--seed", "42"]),
                input_file=tsp_instance_path,
            )

        tsp_route = list(tsp_output.get("route", []))
        tsp_ok, tsp_msg = validate_tour(tsp_route, len(customer_nodes))
        if not tsp_ok:
            raise RuntimeError(f"TSP transform returned an invalid tour for {instance_path}: {tsp_msg}")

        giant_cycle = [customer_nodes[local_idx] for local_idx in tsp_route[:-1]]
        _split_objective, routes = _best_split_routes(giant_cycle, instance, cfg)
        objective = objective_minsum(instance.coords, routes)
        valid = validate_routes(routes, instance.node_count)
        status = "ok" if valid else "invalid"

    elapsed = time.perf_counter() - started_at
    return {
        "instance": instance_path.name,
        "path": str(instance_path),
        "node_count": instance.node_count,
        "salesman_count": instance.salesman_count,
        "objective": round(objective, 6),
        "time_seconds": round(elapsed, 6),
        "valid": valid,
        "status": status,
        "is_exact": False,
        "routes": routes_to_json(routes),
    }
