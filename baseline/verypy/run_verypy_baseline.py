#!/usr/bin/env python3
"""Run VeRyPy classical CVRP heuristics as baselines for mTSP."""

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


# Available heuristics. Map of CLI name → (module, function_name, label).
# VeRyPy ships these classical heuristics in `verypy.classic_heuristics`.
HEURISTIC_REGISTRY = {
    "Sweep":    ("verypy.classic_heuristics.sweep",                 "sweep_init_routes_for_all_vehicles", "VeRyPy_Sweep"),
    "Savings":  ("verypy.classic_heuristics.parallel_savings",       "parallel_savings_init",              "VeRyPy_Savings"),
    "RFCS":     ("verypy.classic_heuristics.gillet_miller_sweep",    "gillett_miller_init",                "VeRyPy_RFCS"),
    "NN":       ("verypy.classic_heuristics.nearest_neighbor",       "nearest_neighbor_init",              "VeRyPy_NN"),
    "SCI":      ("verypy.classic_heuristics.cheapest_insertion",     "cheapest_insertion_init",            "VeRyPy_SCI"),
}


def emit_status(out_path: Path, status: str, **extra) -> None:
    payload = {"algorithm": extra.pop("algorithm", "VeRyPy"), "status": status, **extra}
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(payload, indent=2, ensure_ascii=False))


def build_distance_matrix(coords):
    """VeRyPy classical heuristics expect a numpy 2D array for distances."""
    try:
        import numpy as np
    except ImportError:
        # Fall back to nested lists; some heuristics support them.
        n = len(coords)
        matrix = [[0.0] * n for _ in range(n)]
        for i in range(n):
            xi, yi = coords[i]
            for j in range(i + 1, n):
                xj, yj = coords[j]
                d = math.hypot(xi - xj, yi - yj)
                matrix[i][j] = d
                matrix[j][i] = d
        return matrix

    n = len(coords)
    pts = np.array(coords, dtype=float)
    diff = pts[:, None, :] - pts[None, :, :]
    return np.sqrt((diff ** 2).sum(axis=2))


def normalise_routes(raw, n: int) -> list[list[int]]:
    """Normalise VeRyPy output (which can be a flat list or list-of-lists) into [[0,...,0], ...]."""
    if not raw:
        return []

    # Some heuristics return a single flat list with depot=0 separators: [0, c1, c2, 0, c3, c4, 0]
    if all(isinstance(x, int) for x in raw):
        routes: list[list[int]] = []
        current: list[int] = []
        for node in raw:
            if node == 0:
                if current:
                    routes.append([0, *current, 0])
                    current = []
            else:
                current.append(int(node))
        if current:
            routes.append([0, *current, 0])
        return routes

    # Otherwise list of lists
    routes = []
    for sub in raw:
        cleaned = [int(x) for x in sub if x != 0 and 1 <= int(x) < n]
        if cleaned:
            routes.append([0, *cleaned, 0])
    return routes


def run(args: argparse.Namespace) -> int:
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    n, instance_m, coords = read_mtsp_instance(args.input)
    m = args.m if args.m is not None else instance_m
    instance_name = Path(args.input).stem

    if args.heuristic not in HEURISTIC_REGISTRY:
        print(f"Unknown heuristic '{args.heuristic}'. Available: {list(HEURISTIC_REGISTRY)}", file=sys.stderr)
        return 2

    module_name, func_name, algo_name = HEURISTIC_REGISTRY[args.heuristic]
    result_path = out_dir / f"{instance_name}_m{m}_seed{args.seed}_{args.heuristic}.json"

    try:
        import importlib
        module = importlib.import_module(module_name)
        heuristic_fn = getattr(module, func_name)
    except (ImportError, AttributeError) as e:
        emit_status(
            result_path, "deps_missing",
            algorithm=algo_name,
            error=f"{type(e).__name__}: {e}",
            hint="pip install git+https://github.com/yorak/VeRyPy.git",
            n_vertices=n, m_requested=m, seed=args.seed,
        )
        return 2

    capacity_each = math.ceil((n - 1) / m)
    distance_matrix = build_distance_matrix(coords)
    try:
        import numpy as np
        demands = np.array([0] + [1] * (n - 1), dtype=float)
    except ImportError:
        demands = [0] + [1] * (n - 1)

    t0 = time.time()
    try:
        # VeRyPy classical heuristics signature varies; we try common patterns.
        # parallel_savings_init(D, d, C, L=None, minimize_K=False)
        # sweep_init_routes_for_all_vehicles(coordinates, D, d, C, L=None, ...)
        # nearest_neighbor_init(D, d, C, L=None, emerging_route_count=1, ...)
        # cheapest_insertion_init(D, d, C, L=None, emerging_route_count=1, try_interrupted=True)
        # gillett_miller_init(coordinates, D, d, C, L=None, ...)
        if args.heuristic in ("Sweep", "RFCS"):
            raw_solution = heuristic_fn(coords, distance_matrix, demands, capacity_each)
        elif args.heuristic in ("NN", "SCI"):
            # These often allow specifying route count via emerging_route_count.
            try:
                raw_solution = heuristic_fn(distance_matrix, demands, capacity_each, emerging_route_count=m)
            except TypeError:
                raw_solution = heuristic_fn(distance_matrix, demands, capacity_each)
        else:
            raw_solution = heuristic_fn(distance_matrix, demands, capacity_each)
    except Exception as e:
        emit_status(
            result_path, "solver_error",
            algorithm=algo_name,
            error=f"{type(e).__name__}: {e}",
            n_vertices=n, m_requested=m, seed=args.seed,
        )
        return 3
    elapsed = time.time() - t0

    raw_routes = normalise_routes(raw_solution, n)
    if not raw_routes:
        emit_status(
            result_path, "no_solution",
            algorithm=algo_name,
            elapsed_seconds=elapsed,
            n_vertices=n, m_requested=m, seed=args.seed,
        )
        return 3

    raw_metrics = compute_metrics(raw_routes, coords, m)
    final_routes = raw_routes
    actions: list[str] = []
    if args.postprocess == "exact-m":
        final_routes, actions = postprocess_to_m_routes(raw_routes, m, coords)
    final_metrics = compute_metrics(final_routes, coords, m)

    routes_path = out_dir / f"{instance_name}_m{m}_seed{args.seed}_{args.heuristic}_routes.json"
    routes_path.write_text(json.dumps(final_routes, separators=(",", ":")), encoding="utf-8")

    payload = {
        "algorithm": algo_name,
        "status": "ok",
        "source_instance": str(args.input),
        "n_vertices": n,
        "n_customers": n - 1,
        "m_requested": m,
        "capacity": capacity_each,
        "seed": args.seed,
        "heuristic": args.heuristic,
        "elapsed_seconds": elapsed,
        "postprocess": args.postprocess,
        "postprocess_actions": actions,
        "raw_metrics": raw_metrics,
        "metrics": final_metrics,
        "routes_path": str(routes_path),
    }
    result_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({k: payload[k] for k in ("status", "elapsed_seconds", "metrics")}, indent=2))
    print(f"Saved result to {result_path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="VeRyPy classical heuristic baselines for mTSP")
    parser.add_argument("--input", required=True)
    parser.add_argument("--m", type=int)
    parser.add_argument("--seed", type=int, default=0,
                        help="VeRyPy heuristics are deterministic; seed kept for output naming")
    parser.add_argument("--heuristic", choices=list(HEURISTIC_REGISTRY.keys()), default="Savings")
    parser.add_argument("--output-dir", default=str(REPO_ROOT / "data" / "results" / "baselines" / "verypy"))
    parser.add_argument("--postprocess", choices=("none", "exact-m"), default="exact-m")
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
