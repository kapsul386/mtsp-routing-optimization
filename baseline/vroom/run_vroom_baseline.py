#!/usr/bin/env python3
"""Run VROOM as a VRP baseline for mTSP."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from math import ceil
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
FILO2_DIR = REPO_ROOT / "baseline" / "filo2withoutcode"
sys.path.insert(0, str(FILO2_DIR))
sys.path.insert(0, str(SCRIPT_DIR))

from mtsp_to_cvrp import read_mtsp_instance  # noqa: E402
from parse_filo2_solution import compute_metrics, postprocess_to_m_routes  # noqa: E402
from mtsp_to_vroom_json import build_vroom_payload  # noqa: E402


def emit_status(out_path: Path, status: str, **extra) -> None:
    payload = {"algorithm": extra.pop("algorithm", "VROOM"), "status": status, **extra}
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(payload, indent=2, ensure_ascii=False))


def default_vroom_bin() -> Path:
    candidates = [
        REPO_ROOT / "external" / "vroom" / "bin" / "vroom",
        REPO_ROOT / "external" / "vroom" / "bin" / "vroom.exe",
        REPO_ROOT / "external" / "vroom" / "build" / "vroom",
        REPO_ROOT / "external" / "vroom" / "build" / "vroom.exe",
    ]
    for c in candidates:
        if c.exists():
            return c
    return candidates[0]


def parse_vroom_output(payload: dict, n: int) -> list[list[int]]:
    """Convert VROOM output (list of routes with steps) into [[0, c1, c2, ..., 0], ...]."""
    routes: list[list[int]] = []
    for vroom_route in payload.get("routes", []):
        path = [0]
        for step in vroom_route.get("steps", []):
            if step.get("type") in ("start", "end"):
                continue
            loc = step.get("location_index")
            if loc is None or loc == 0:
                continue
            if 1 <= loc < n:
                path.append(int(loc))
        path.append(0)
        if len(path) > 2:
            routes.append(path)
    return routes


def run(args: argparse.Namespace) -> int:
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    n, instance_m, coords = read_mtsp_instance(args.input)
    m = args.m if args.m is not None else instance_m
    instance_name = Path(args.input).stem
    result_path = out_dir / f"{instance_name}_m{m}_seed{args.seed}.json"

    vroom_bin = Path(args.vroom_bin) if args.vroom_bin else default_vroom_bin()

    if not vroom_bin.exists():
        emit_status(
            result_path, "binary_not_built",
            algorithm="VROOM",
            error=f"VROOM binary not found at {vroom_bin}",
            hint="bash baseline/vroom/install_vroom.sh && bash baseline/vroom/build_vroom.sh",
            n_vertices=n, m_requested=m, seed=args.seed,
        )
        return 2

    capacity_each = ceil((n - 1) / m)

    # Build VROOM input JSON.
    raw_dir = out_dir / "raw" / f"{instance_name}_m{m}_seed{args.seed}"
    raw_dir.mkdir(parents=True, exist_ok=True)
    input_json = raw_dir / "input.json"
    output_json = raw_dir / "output.json"

    payload = build_vroom_payload(coords, m, capacity_each)
    input_json.write_text(json.dumps(payload, separators=(",", ":")), encoding="utf-8")

    # VROOM CLI: -i input.json -o output.json [-l N (limit threads)] [-x N (explore parameter)]
    cmd = [
        str(vroom_bin),
        "-i", str(input_json),
        "-o", str(output_json),
        *args.extra_arg,
    ]

    t0 = time.time()
    completed = subprocess.run(
        cmd,
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    elapsed = time.time() - t0

    if not output_json.exists():
        emit_status(
            result_path, "no_output",
            algorithm="VROOM",
            return_code=completed.returncode,
            elapsed_seconds=elapsed,
            stdout_tail=completed.stdout[-2000:],
            stderr_tail=completed.stderr[-2000:],
            command=cmd,
        )
        return 3

    try:
        out_payload = json.loads(output_json.read_text(encoding="utf-8"))
    except Exception as e:
        emit_status(
            result_path, "parse_error",
            algorithm="VROOM",
            error=str(e),
            stdout_tail=completed.stdout[-2000:],
            stderr_tail=completed.stderr[-2000:],
        )
        return 3

    raw_routes = parse_vroom_output(out_payload, n)
    if not raw_routes:
        emit_status(
            result_path, "no_solution_parsed",
            algorithm="VROOM",
            elapsed_seconds=elapsed,
            return_code=completed.returncode,
            command=cmd,
            n_vertices=n, m_requested=m, seed=args.seed,
        )
        return 3

    raw_metrics = compute_metrics(raw_routes, coords, m)
    final_routes = raw_routes
    actions: list[str] = []
    if args.postprocess == "exact-m":
        final_routes, actions = postprocess_to_m_routes(raw_routes, m, coords)
    final_metrics = compute_metrics(final_routes, coords, m)

    routes_path = out_dir / f"{instance_name}_m{m}_seed{args.seed}_routes.json"
    routes_path.write_text(json.dumps(final_routes, separators=(",", ":")), encoding="utf-8")

    summary = out_payload.get("summary", {})

    result = {
        "algorithm": "VROOM",
        "status": "ok" if completed.returncode == 0 else "ok_nonzero_rc",
        "source_instance": str(args.input),
        "n_vertices": n,
        "n_customers": n - 1,
        "m_requested": m,
        "capacity": capacity_each,
        "seed": args.seed,
        "time_limit_seconds": 0,  # VROOM is not time-limit driven
        "elapsed_seconds": elapsed,
        "return_code": completed.returncode,
        "vroom_summary": {
            "cost": summary.get("cost"),
            "duration": summary.get("duration"),
            "computing_times": summary.get("computing_times"),
            "unassigned": summary.get("unassigned"),
            "routes": summary.get("routes"),
        },
        "postprocess": args.postprocess,
        "postprocess_actions": actions,
        "raw_metrics": raw_metrics,
        "metrics": final_metrics,
        "routes_path": str(routes_path),
        "command": cmd,
        "stdout_tail": completed.stdout[-3000:],
        "stderr_tail": completed.stderr[-3000:],
    }
    result_path.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("status", "elapsed_seconds", "return_code", "metrics")}, indent=2))
    print(f"Saved result to {result_path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="VROOM VRP baseline for mTSP")
    parser.add_argument("--input", required=True)
    parser.add_argument("--m", type=int)
    parser.add_argument("--seed", type=int, default=0,
                        help="VROOM is mostly deterministic; seed kept for output naming")
    parser.add_argument("--output-dir", default=str(REPO_ROOT / "data" / "results" / "baselines" / "vroom"))
    parser.add_argument("--vroom-bin")
    parser.add_argument("--postprocess", choices=("none", "exact-m"), default="exact-m")
    parser.add_argument("--extra-arg", action="append", default=[],
                        help="Pass-through args to VROOM binary (repeat as needed)")
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
