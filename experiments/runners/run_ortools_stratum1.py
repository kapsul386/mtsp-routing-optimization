"""Batch runner for OR-Tools on stratum-1 small MTSP instances.

Output CSVs match the format produced by run_benchmarks.py so the OR-Tools
results can be merged into the comparison table alongside lkh3-baseline,
lkh-wrapper-v20, lkh-wrapper-v21, etc.
"""

from __future__ import annotations

import csv
import json
import math
import sys
import time
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXPERIMENTS = ROOT / "experiments"
for p in (ROOT, EXPERIMENTS):
    if str(p) not in sys.path:
        sys.path.insert(0, str(p))

from mtsp_experiment_utils import ensure_instance_family, instance_family_sort_key  # noqa: E402

DIST_SCALE = 1000


def read_instance(path: Path) -> tuple[int, int, list[tuple[float, float]]]:
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    n, m = map(int, lines[0].split())
    coords = [tuple(map(float, line.split())) for line in lines[1:]]
    return n, m, coords


def build_distance_matrix(coords: list[tuple[float, float]]) -> list[list[int]]:
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


def route_length(route: list[int], coords: list[tuple[float, float]]) -> float:
    if len(route) < 2:
        return 0.0
    total = 0.0
    for a, b in zip(route, route[1:]):
        xa, ya = coords[a]
        xb, yb = coords[b]
        total += math.hypot(xa - xb, ya - yb)
    return total


def solve_one(
    coords: list[tuple[float, float]],
    m: int,
    time_limit_s: int,
    strategy_name: str = "GLS",
) -> tuple[float | None, list[list[int]], float, str]:
    from ortools.constraint_solver import pywrapcp, routing_enums_pb2

    n = len(coords)
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
        demand_cb_idx, 0, [capacity_each] * m, True, "Capacity",
    )

    search = pywrapcp.DefaultRoutingSearchParameters()
    search.first_solution_strategy = (
        routing_enums_pb2.FirstSolutionStrategy.PATH_CHEAPEST_ARC
    )
    search.local_search_metaheuristic = (
        routing_enums_pb2.LocalSearchMetaheuristic.GUIDED_LOCAL_SEARCH
    )
    if time_limit_s and time_limit_s > 0:
        search.time_limit.seconds = int(time_limit_s)
    if hasattr(search, "log_search"):
        search.log_search = False

    t0 = time.time()
    solution = routing.SolveWithParameters(search)
    elapsed = time.time() - t0

    if solution is None:
        return None, [], elapsed, "no_solution"

    routes: list[list[int]] = []
    for v in range(m):
        route = [0]
        idx = routing.Start(v)
        while not routing.IsEnd(idx):
            node = manager.IndexToNode(idx)
            if node != 0:
                route.append(node)
            idx = solution.Value(routing.NextVar(idx))
        route.append(0)
        routes.append(route)

    objective = sum(route_length(r, coords) for r in routes)
    return objective, routes, elapsed, "ok"


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--instance-dir", default="data/mtsp/stratum1_small")
    parser.add_argument("--allowed-node-counts", default="100,200,500,1000")
    parser.add_argument("--allowed-salesman-counts", default="")
    parser.add_argument("--instance-families", default="", help="comma-separated families to include (uniform, clustered-center, ...)")
    parser.add_argument("--time-limit", type=int, default=10)
    parser.add_argument("--results-csv", default="data/results/stratum1_ortools_results.csv")
    parser.add_argument("--summary-csv", default="data/results/stratum1_ortools_summary.csv")
    args = parser.parse_args()

    allowed_node_counts = {int(v) for v in args.allowed_node_counts.split(",") if v}
    allowed_salesman_counts = {int(v) for v in args.allowed_salesman_counts.split(",") if v.strip()}
    allowed_families = {v.strip() for v in args.instance_families.split(",") if v.strip()}
    instance_dir = ROOT / args.instance_dir
    instances = sorted(instance_dir.glob("*.txt"))

    rows: list[dict] = []
    for instance_path in instances:
        n, m, coords = read_instance(instance_path)
        if allowed_node_counts and n not in allowed_node_counts:
            continue
        if allowed_salesman_counts and m not in allowed_salesman_counts:
            continue
        if allowed_families:
            fam_row = ensure_instance_family({"instance": instance_path.name, "path": str(instance_path)})
            if str(fam_row.get("instance_family")) not in allowed_families:
                continue
        print(f"[ortools] {instance_path.name} (n={n}, m={m})", flush=True)

        try:
            objective, routes, elapsed, status = solve_one(coords, m, args.time_limit)
        except Exception as e:
            objective, routes, elapsed, status = None, [], 0.0, f"error:{type(e).__name__}"

        valid = objective is not None
        row = ensure_instance_family({
            "instance": instance_path.name,
            "path": str(instance_path.relative_to(ROOT)).replace("\\", "/"),
            "node_count": n,
            "salesman_count": m,
            "solver": "ortools-GLS",
            "objective": float(objective) if valid else "",
            "time_seconds": float(elapsed),
            "step_time_seconds": float(elapsed),
            "valid": valid,
            "status": status,
            "steps": json.dumps([], ensure_ascii=False),
            "routes": json.dumps(routes, ensure_ascii=False),
        })
        rows.append(row)

    results_csv = ROOT / args.results_csv
    summary_csv = ROOT / args.summary_csv
    results_csv.parent.mkdir(parents=True, exist_ok=True)

    with results_csv.open("w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=[
            "instance_family", "instance", "path", "node_count", "salesman_count",
            "solver", "objective", "time_seconds", "step_time_seconds",
            "valid", "status", "steps", "routes",
        ])
        w.writeheader()
        w.writerows(rows)

    grouped: dict[tuple, list[dict]] = defaultdict(list)
    for row in rows:
        grouped[(row["instance_family"], row["node_count"], row["salesman_count"], row["solver"])].append(row)
    summary_rows = []
    for (fam, n, m, solver), items in sorted(
        grouped.items(),
        key=lambda x: (instance_family_sort_key(x[0][0]), x[0][1], x[0][2], x[0][3]),
    ):
        valid_items = [it for it in items if it["valid"]]
        summary_rows.append({
            "instance_family": fam,
            "node_count": n,
            "salesman_count": m,
            "solver": solver,
            "runs": len(items),
            "avg_objective": round(sum(it["objective"] for it in valid_items) / len(valid_items), 6) if valid_items else "",
            "avg_time_seconds": round(sum(it["time_seconds"] for it in items) / len(items), 6),
            "avg_step_time_seconds": round(sum(it["step_time_seconds"] for it in items) / len(items), 6),
            "valid_runs": len(valid_items),
        })
    with summary_csv.open("w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=[
            "instance_family", "node_count", "salesman_count", "solver", "runs",
            "avg_objective", "avg_time_seconds", "avg_step_time_seconds", "valid_runs",
        ])
        w.writeheader()
        w.writerows(summary_rows)

    print(f"\nWrote {results_csv}")
    print(f"Wrote {summary_csv}")


if __name__ == "__main__":
    main()
