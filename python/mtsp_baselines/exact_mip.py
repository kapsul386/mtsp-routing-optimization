from __future__ import annotations

import math
import time
from pathlib import Path

from .common import objective_minsum, read_mtsp_instance, routes_to_json, validate_routes


def _status_name(pywraplp: object, status: int) -> str:
    status_names = {
        getattr(pywraplp.Solver, "OPTIMAL", None): "optimal",
        getattr(pywraplp.Solver, "FEASIBLE", None): "feasible",
        getattr(pywraplp.Solver, "INFEASIBLE", None): "infeasible",
        getattr(pywraplp.Solver, "ABNORMAL", None): "abnormal",
        getattr(pywraplp.Solver, "NOT_SOLVED", None): "not_solved",
        getattr(pywraplp.Solver, "UNBOUNDED", None): "unbounded",
    }
    return status_names.get(status, f"status_{status}")


def _create_solver(pywraplp: object, cfg: dict) -> tuple[object, str]:
    backends = [str(name) for name in cfg.get("backend_preferences", ["SCIP", "CBC"])]
    for backend in backends:
        solver = pywraplp.Solver.CreateSolver(backend)
        if solver is not None:
            return solver, backend
    raise RuntimeError(
        "Could not create an exact MIP backend. Tried: " + ", ".join(backends)
    )


def _distance_scale(coords: list[tuple[float, float]], scale: int, left: int, right: int) -> int:
    ax, ay = coords[left]
    bx, by = coords[right]
    return int(round(scale * math.hypot(ax - bx, ay - by)))


def _extract_routes(x_vars: dict[tuple[int, int], object], node_count: int, salesman_count: int) -> list[list[int]]:
    outgoing: dict[int, list[int]] = {node: [] for node in range(node_count)}
    for (left, right), variable in x_vars.items():
        if variable.solution_value() > 0.5:
            outgoing[left].append(right)

    starts = list(outgoing.get(0, []))
    routes: list[list[int]] = []
    for start in starts:
        route = [0, start]
        current = start
        seen = {0, start}
        while current != 0:
            next_nodes = outgoing.get(current, [])
            if len(next_nodes) != 1:
                raise RuntimeError("Exact MIP returned an invalid successor structure.")
            next_node = next_nodes[0]
            route.append(next_node)
            if next_node == 0:
                break
            if next_node in seen:
                raise RuntimeError("Exact MIP returned a cyclic customer route.")
            seen.add(next_node)
            current = next_node
        routes.append(route)

    while len(routes) < salesman_count:
        routes.append([0, 0])
    return routes


def solve(instance_path: Path, cfg: dict, runtime: dict) -> dict:
    try:
        from ortools.linear_solver import pywraplp
    except ImportError as exc:
        raise RuntimeError(
            "OR-Tools is not installed. Install it with `python -m pip install ortools` "
            "before running the hybrid baseline pipeline."
        ) from exc

    instance = read_mtsp_instance(instance_path)
    customer_count = instance.node_count - 1
    if customer_count <= 0:
        routes = [[0, 0] for _ in range(instance.salesman_count)]
        return {
            "instance": instance_path.name,
            "path": str(instance_path),
            "node_count": instance.node_count,
            "salesman_count": instance.salesman_count,
            "objective": 0.0,
            "time_seconds": 0.0,
            "valid": True,
            "status": "optimal",
            "is_exact": True,
            "routes": routes_to_json(routes),
        }
    if instance.salesman_count > customer_count:
        routes = [[0, customer, 0] for customer in range(1, instance.node_count)]
        routes.extend([[0, 0] for _ in range(instance.salesman_count - customer_count)])
        objective = objective_minsum(instance.coords, routes)
        return {
            "instance": instance_path.name,
            "path": str(instance_path),
            "node_count": instance.node_count,
            "salesman_count": instance.salesman_count,
            "objective": round(objective, 6),
            "time_seconds": 0.0,
            "valid": True,
            "status": "optimal_trivial",
            "is_exact": True,
            "routes": routes_to_json(routes),
        }

    solver, backend = _create_solver(pywraplp, cfg)
    if bool(cfg.get("log_search", False)):
        solver.EnableOutput()
    solver.SetTimeLimit(int(round(float(cfg.get("time_limit_seconds", 30.0)) * 1000)))

    scale = int(cfg.get("distance_scale", 1000))
    customers = range(1, instance.node_count)
    order_bound = max(1, customer_count)

    x_vars: dict[tuple[int, int], object] = {}
    for left in range(instance.node_count):
        for right in range(instance.node_count):
            if left == right:
                continue
            x_vars[(left, right)] = solver.IntVar(0.0, 1.0, f"x_{left}_{right}")

    order_vars = {
        customer: solver.NumVar(1.0, float(order_bound), f"u_{customer}")
        for customer in customers
    }

    for customer in customers:
        solver.Add(sum(x_vars[(customer, right)] for right in range(instance.node_count) if right != customer) == 1)
        solver.Add(sum(x_vars[(left, customer)] for left in range(instance.node_count) if left != customer) == 1)

    solver.Add(sum(x_vars[(0, right)] for right in customers) == instance.salesman_count)
    solver.Add(sum(x_vars[(left, 0)] for left in customers) == instance.salesman_count)

    for left in customers:
        for right in customers:
            if left == right:
                continue
            solver.Add(order_vars[left] - order_vars[right] + order_bound * x_vars[(left, right)] <= order_bound - 1)

    objective = solver.Objective()
    for (left, right), variable in x_vars.items():
        objective.SetCoefficient(variable, _distance_scale(instance.coords, scale, left, right))
    objective.SetMinimization()

    started_at = time.perf_counter()
    status = solver.Solve()
    elapsed = time.perf_counter() - started_at

    if status not in (pywraplp.Solver.OPTIMAL, pywraplp.Solver.FEASIBLE):
        return {
            "instance": instance_path.name,
            "path": str(instance_path),
            "node_count": instance.node_count,
            "salesman_count": instance.salesman_count,
            "objective": "",
            "time_seconds": round(elapsed, 6),
            "valid": False,
            "status": f"{_status_name(pywraplp, status)}:{backend}",
            "is_exact": False,
            "routes": "[]",
        }

    routes = _extract_routes(x_vars, instance.node_count, instance.salesman_count)
    valid = validate_routes(routes, instance.node_count)
    route_objective = objective_minsum(instance.coords, routes) if valid else 0.0
    return {
        "instance": instance_path.name,
        "path": str(instance_path),
        "node_count": instance.node_count,
        "salesman_count": instance.salesman_count,
        "objective": round(route_objective, 6) if valid else "",
        "time_seconds": round(elapsed, 6),
        "valid": valid,
        "status": f"{_status_name(pywraplp, status)}:{backend}" if valid else f"invalid:{backend}",
        "is_exact": status == pywraplp.Solver.OPTIMAL and valid,
        "routes": routes_to_json(routes if valid else []),
    }
