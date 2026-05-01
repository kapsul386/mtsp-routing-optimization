#!/usr/bin/env python3
"""Run Google OR-Tools Routing Solver as an external CVRP-style baseline for mTSP."""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
FILO2_DIR = REPO_ROOT / "baseline" / "filo2withoutcode"
sys.path.insert(0, str(FILO2_DIR))

from mtsp_to_cvrp import read_mtsp_instance  # noqa: E402
from parse_filo2_solution import compute_metrics, postprocess_to_m_routes  # noqa: E402


STRATEGIES = {
    "GLS":          ("PATH_CHEAPEST_ARC",          "GUIDED_LOCAL_SEARCH"),
    "PARALLEL_GLS": ("PARALLEL_CHEAPEST_INSERTION","GUIDED_LOCAL_SEARCH"),
    "SAVINGS_GLS":  ("SAVINGS",                    "GUIDED_LOCAL_SEARCH"),
    "TABU":         ("PATH_CHEAPEST_ARC",          "TABU_SEARCH"),
    "SA":           ("PATH_CHEAPEST_ARC",          "SIMULATED_ANNEALING"),
}

DIST_SCALE = 1000  # OR-Tools requires integer costs; scale floats by 1e3 for precision.


def emit_status(out_path: Path, status: str, **extra) -> None:
    """Write a result JSON when we cannot run the solver (deps missing, bad args, etc.)."""
    payload = {
        "algorithm": extra.pop("algorithm", "OR-Tools_unknown"),
        "status": status,
        **extra,
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({"status": status, **{k: v for k, v in extra.items() if k in ("error", "hint")}}, indent=2))


def build_distance_matrix(coords) -> list[list[int]]:
    n = len(coords)
    matrix = [[0] * n for _ in range(n)]
    for i in range(n):
        xi, yi = coords[i]
        for j in range(i + 1, n):
            xj, yj = coords[j]
            d = int(round(math.hypot(xi - xj, yi - yj) * DIST_SCALE))
            matrix[i][j] = d
            matrix[j][i] = d
    return matrix


def run(args: argparse.Namespace) -> int:
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    n, instance_m, coords = read_mtsp_instance(args.input)
    m = args.m if args.m is not None else instance_m
    instance_name = Path(args.input).stem
    result_path = out_dir / f"{instance_name}_m{m}_seed{args.seed}_{args.strategy}.json"

    algo_name = f"OR-Tools_{args.strategy}"

    # Lazy import so missing deps produce a clean status JSON, not a crash.
    try:
        from ortools.constraint_solver import pywrapcp, routing_enums_pb2
    except ImportError as e:
        emit_status(
            result_path,
            "deps_missing",
            algorithm=algo_name,
            error=str(e),
            hint="pip install ortools",
            n_vertices=n,
            m_requested=m,
            seed=args.seed,
            strategy=args.strategy,
        )
        return 2

    if args.strategy not in STRATEGIES:
        emit_status(
            result_path,
            "invalid",
            algorithm=algo_name,
            error=f"unknown strategy {args.strategy}",
            available=list(STRATEGIES.keys()),
        )
        return 2

    first_sol_name, metaheuristic_name = STRATEGIES[args.strategy]

    # Build OR-Tools data model (depot=0, demand=1 per customer, capacity=ceil((n-1)/m)).
    distance_matrix = build_distance_matrix(coords)
    demands = [0] + [1] * (n - 1)
    capacity_each = math.ceil((n - 1) / m)

    manager = pywrapcp.RoutingIndexManager(n, m, 0)
    routing = pywrapcp.RoutingModel(manager)

    def dist_cb(from_idx, to_idx):
        return distance_matrix[manager.IndexToNode(from_idx)][manager.IndexToNode(to_idx)]
    transit_cb_idx = routing.RegisterTransitCallback(dist_cb)
    routing.SetArcCostEvaluatorOfAllVehicles(transit_cb_idx)

    def demand_cb(from_idx):
        return demands[manager.IndexToNode(from_idx)]
    demand_cb_idx = routing.RegisterUnaryTransitCallback(demand_cb)
    routing.AddDimensionWithVehicleCapacity(
        demand_cb_idx,
        0,
        [capacity_each] * m,
        True,
        "Capacity",
    )

    search = pywrapcp.DefaultRoutingSearchParameters()
    search.first_solution_strategy = getattr(
        routing_enums_pb2.FirstSolutionStrategy, first_sol_name
    )
    search.local_search_metaheuristic = getattr(
        routing_enums_pb2.LocalSearchMetaheuristic, metaheuristic_name
    )
    if args.time_limit and args.time_limit > 0:
        search.time_limit.seconds = int(args.time_limit)
    if hasattr(search, "log_search"):
        search.log_search = False

    t0 = time.time()
    solution = routing.SolveWithParameters(search)
    elapsed = time.time() - t0

    if solution is None:
        emit_status(
            result_path,
            "no_solution",
            algorithm=algo_name,
            elapsed_seconds=elapsed,
            n_vertices=n,
            m_requested=m,
            seed=args.seed,
            strategy=args.strategy,
        )
        return 3

    raw_routes: list[list[int]] = []
    for v in range(m):
        route = [0]
        idx = routing.Start(v)
        while not routing.IsEnd(idx):
            node = manager.IndexToNode(idx)
            if node != 0:
                route.append(node)
            idx = solution.Value(routing.NextVar(idx))
        route.append(0)
        if len(route) > 2:
            raw_routes.append(route)

    raw_metrics = compute_metrics(raw_routes, coords, m) if raw_routes else None
    final_routes = raw_routes
    actions: list[str] = []
    if args.postprocess == "exact-m":
        final_routes, actions = postprocess_to_m_routes(raw_routes, m, coords)
    final_metrics = compute_metrics(final_routes, coords, m) if final_routes else None

    routes_path = out_dir / f"{instance_name}_m{m}_seed{args.seed}_{args.strategy}_routes.json"
    routes_path.write_text(json.dumps(final_routes, separators=(",", ":")), encoding="utf-8")

    payload = {
        "algorithm": algo_name,
        "status": "ok",
        "source_instance": str(args.input),
        "n_vertices": n,
        "n_customers": n - 1,
        "m_requested": m,
        "capacity": capacity_each,
        "time_limit_seconds": int(args.time_limit) if args.time_limit else 0,
        "seed": args.seed,
        "strategy": args.strategy,
        "first_solution_strategy": first_sol_name,
        "local_search_metaheuristic": metaheuristic_name,
        "elapsed_seconds": elapsed,
        "postprocess": args.postprocess,
        "postprocess_actions": actions,
        "raw_metrics": raw_metrics,
        "metrics": final_metrics,
        "routes_path": str(routes_path),
    }
    result_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")

    print(json.dumps(
        {k: payload[k] for k in ("status", "elapsed_seconds", "metrics")},
        indent=2,
    ))
    print(f"Saved result to {result_path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="OR-Tools Routing Solver baseline for mTSP")
    parser.add_argument("--input", required=True, help="Path to mTSP instance file")
    parser.add_argument("--m", type=int, help="Number of salesmen (default: from instance)")
    parser.add_argument("--time-limit", type=int, default=300, help="Time limit in seconds")
    parser.add_argument("--seed", type=int, default=0, help="Random seed (passed to OR-Tools where supported)")
    parser.add_argument("--output-dir", default=str(REPO_ROOT / "data" / "results" / "baselines" / "ortools"))
    parser.add_argument("--strategy", choices=list(STRATEGIES.keys()), default="GLS")
    parser.add_argument("--postprocess", choices=("none", "exact-m"), default="exact-m")
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
