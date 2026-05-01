#!/usr/bin/env python3
"""Run PyVRP (HGS Python wrapper) as a CVRP-style baseline for mTSP.

Uses pyvrp.read() on a generated CVRPLIB .vrp file (same path as FILO2/HGS)
rather than the Model() builder API — sidesteps pitfalls with manual coord
scaling and ensures Euclidean distances match the other baselines exactly.
"""

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

from mtsp_to_cvrp import read_mtsp_instance, write_cvrp  # noqa: E402
from parse_filo2_solution import compute_metrics, postprocess_to_m_routes  # noqa: E402


def emit_status(out_path: Path, status: str, **extra) -> None:
    payload = {"algorithm": extra.pop("algorithm", "PyVRP"), "status": status, **extra}
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(payload, indent=2, ensure_ascii=False))


def run(args: argparse.Namespace) -> int:
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    n, instance_m, coords = read_mtsp_instance(args.input)
    m = args.m if args.m is not None else instance_m
    instance_name = Path(args.input).stem
    result_path = out_dir / f"{instance_name}_m{m}_seed{args.seed}.json"

    try:
        from pyvrp import read as pyvrp_read, solve as pyvrp_solve
        from pyvrp.stop import MaxRuntime, MaxIterations
    except ImportError as e:
        emit_status(
            result_path, "deps_missing",
            algorithm="PyVRP",
            error=str(e),
            hint="pip install pyvrp",
            n_vertices=n, m_requested=m, seed=args.seed,
        )
        return 2

    # Build the same CVRPLIB .vrp file FILO2/HGS use, then let PyVRP read it.
    tmp_dir = REPO_ROOT / "data" / "cvrp_tmp"
    tmp_dir.mkdir(parents=True, exist_ok=True)
    cvrp_path = tmp_dir / f"{instance_name}_m{m}.vrp"
    capacity_each = write_cvrp(coords, m, cvrp_path, name=instance_name)

    # PyVRP reads CVRPLIB; round_func="round" matches Euclidean rounding.
    instance = pyvrp_read(str(cvrp_path), round_func="round")

    if args.time_limit and args.time_limit > 0:
        stop = MaxRuntime(args.time_limit)
    elif args.iters and args.iters > 0:
        stop = MaxIterations(args.iters)
    else:
        stop = MaxRuntime(60)

    t0 = time.time()
    try:
        result = pyvrp_solve(instance, stop=stop, seed=args.seed, display=False)
    except Exception as e:
        emit_status(
            result_path, "solver_error",
            algorithm="PyVRP",
            error=f"{type(e).__name__}: {e}",
            n_vertices=n, m_requested=m, seed=args.seed,
        )
        return 3
    elapsed = time.time() - t0

    best = result.best
    if best is None or not best.is_feasible():
        emit_status(
            result_path, "infeasible",
            algorithm="PyVRP",
            elapsed_seconds=elapsed,
            n_vertices=n, m_requested=m, seed=args.seed,
            cost=str(getattr(best, "distance", lambda: None)()) if best else None,
        )
        return 3

    # Extract routes. After pyvrp.read on CVRPLIB, location indices match the .vrp
    # file's 1-based numbering: depot is location 0 (CVRPLIB index 1) and clients
    # are 1..n-1 (CVRPLIB indices 2..n). PyVRP's Route.visits() returns these
    # location indices directly, so they map 1:1 onto our mTSP coords.
    raw_routes: list[list[int]] = []
    for r in best.routes():
        client_idx = list(r.visits())
        path = [0, *(int(c) for c in client_idx), 0]
        if len(path) > 2:
            raw_routes.append(path)

    raw_metrics = compute_metrics(raw_routes, coords, m) if raw_routes else None
    final_routes = raw_routes
    actions: list[str] = []
    if args.postprocess == "exact-m":
        final_routes, actions = postprocess_to_m_routes(raw_routes, m, coords)
    final_metrics = compute_metrics(final_routes, coords, m) if final_routes else None

    routes_path = out_dir / f"{instance_name}_m{m}_seed{args.seed}_routes.json"
    routes_path.write_text(json.dumps(final_routes, separators=(",", ":")), encoding="utf-8")

    payload = {
        "algorithm": "PyVRP",
        "status": "ok",
        "source_instance": str(args.input),
        "converted_instance": str(cvrp_path),
        "n_vertices": n,
        "n_customers": n - 1,
        "m_requested": m,
        "capacity": capacity_each,
        "time_limit_seconds": int(args.time_limit) if args.time_limit else 0,
        "iters": args.iters or 0,
        "seed": args.seed,
        "elapsed_seconds": elapsed,
        "pyvrp_distance": float(best.distance()) if hasattr(best, "distance") else None,
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
    parser = argparse.ArgumentParser(description="PyVRP (HGS) baseline for mTSP")
    parser.add_argument("--input", required=True)
    parser.add_argument("--m", type=int)
    parser.add_argument("--time-limit", type=int, default=60)
    parser.add_argument("--iters", type=int, default=0)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--output-dir", default=str(REPO_ROOT / "data" / "results" / "baselines" / "pyvrp"))
    parser.add_argument("--postprocess", choices=("none", "exact-m"), default="exact-m")
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
