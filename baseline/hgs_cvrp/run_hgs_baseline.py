#!/usr/bin/env python3
"""Run HGS-CVRP (Vidal's Hybrid Genetic Search) as a CVRP-style baseline for mTSP."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
FILO2_DIR = REPO_ROOT / "baseline" / "filo2withoutcode"
sys.path.insert(0, str(FILO2_DIR))

from mtsp_to_cvrp import read_mtsp_instance, write_cvrp  # noqa: E402
from parse_filo2_solution import compute_metrics, postprocess_to_m_routes  # noqa: E402


# HGS-CVRP solution-file format (per their README): line "Route #k: c1 c2 c3 ..."
# Customer indices are 1-based. Last line is "Cost <total>".
ROUTE_RE = re.compile(r"^\s*Route\s*#\s*(\d+)\s*:\s*(?P<body>[\d\s]+)\s*$", re.IGNORECASE)
COST_RE = re.compile(r"^\s*Cost\s+([\d.]+)\s*$", re.IGNORECASE)


def emit_status(out_path: Path, status: str, **extra) -> None:
    payload = {"algorithm": extra.pop("algorithm", "HGS-CVRP"), "status": status, **extra}
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(payload, indent=2, ensure_ascii=False))


def default_hgs_bin() -> Path:
    candidates = [
        REPO_ROOT / "external" / "HGS-CVRP" / "build" / "hgs",
        REPO_ROOT / "external" / "HGS-CVRP" / "build" / "hgs.exe",
        REPO_ROOT / "external" / "HGS-CVRP" / "Release" / "hgs.exe",
    ]
    for c in candidates:
        if c.exists():
            return c
    return candidates[0]


def parse_hgs_solution(sol_path: Path) -> tuple[list[list[int]], float | None]:
    """Parse HGS-CVRP .sol file. Customers are 1-based; we convert to depot-wrapped routes."""
    routes: list[list[int]] = []
    cost: float | None = None
    if not sol_path.exists():
        return routes, cost
    for line in sol_path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = ROUTE_RE.match(line)
        if m:
            customers = [int(t) for t in m.group("body").split() if t.strip()]
            if customers:
                routes.append([0, *customers, 0])
            continue
        m = COST_RE.match(line)
        if m:
            try:
                cost = float(m.group(1))
            except ValueError:
                pass
    return routes, cost


def run(args: argparse.Namespace) -> int:
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    n, instance_m, coords = read_mtsp_instance(args.input)
    m = args.m if args.m is not None else instance_m
    instance_name = Path(args.input).stem
    result_path = out_dir / f"{instance_name}_m{m}_seed{args.seed}.json"

    hgs_bin = Path(args.hgs_bin) if args.hgs_bin else default_hgs_bin()

    if not hgs_bin.exists():
        emit_status(
            result_path, "binary_not_built",
            algorithm="HGS-CVRP",
            error=f"HGS binary not found at {hgs_bin}",
            hint="bash baseline/hgs_cvrp/install_hgs.sh && bash baseline/hgs_cvrp/build_hgs.sh",
            n_vertices=n, m_requested=m, seed=args.seed,
        )
        return 2

    # Build CVRP file (reuse FILO2's converter; output to data/cvrp_tmp/).
    tmp_dir = REPO_ROOT / "data" / "cvrp_tmp"
    tmp_dir.mkdir(parents=True, exist_ok=True)
    cvrp_path = tmp_dir / f"{instance_name}_m{m}.vrp"
    capacity = write_cvrp(coords, m, cvrp_path, name=instance_name)

    sol_path = out_dir / "raw" / f"{instance_name}_m{m}_seed{args.seed}.sol"
    sol_path.parent.mkdir(parents=True, exist_ok=True)

    # HGS-CVRP CLI per README: hgs <instance> <solution> -seed N -t S -veh m
    cmd = [
        str(hgs_bin),
        str(cvrp_path),
        str(sol_path),
        "-seed", str(args.seed),
        "-t", str(args.time_limit),
        "-veh", str(m),
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

    raw_routes, hgs_cost = parse_hgs_solution(sol_path)
    if not raw_routes:
        # Try parsing routes from stdout (some HGS variants print solutions inline).
        from parse_filo2_solution import parse_routes_from_text
        raw_routes = parse_routes_from_text(completed.stdout + "\n" + completed.stderr, n)

    if not raw_routes:
        emit_status(
            result_path, "no_solution_parsed",
            algorithm="HGS-CVRP",
            elapsed_seconds=elapsed,
            return_code=completed.returncode,
            stdout_tail=completed.stdout[-2000:],
            stderr_tail=completed.stderr[-2000:],
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

    payload = {
        "algorithm": "HGS-CVRP",
        "status": "ok" if completed.returncode == 0 else "ok_nonzero_rc",
        "source_instance": str(args.input),
        "converted_instance": str(cvrp_path),
        "solution_source": str(sol_path),
        "n_vertices": n,
        "n_customers": n - 1,
        "m_requested": m,
        "capacity": capacity,
        "time_limit_seconds": int(args.time_limit),
        "seed": args.seed,
        "elapsed_seconds": elapsed,
        "return_code": completed.returncode,
        "hgs_reported_cost": hgs_cost,
        "postprocess": args.postprocess,
        "postprocess_actions": actions,
        "raw_metrics": raw_metrics,
        "metrics": final_metrics,
        "routes_path": str(routes_path),
        "command": cmd,
        "stdout_tail": completed.stdout[-3000:],
        "stderr_tail": completed.stderr[-3000:],
    }
    result_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({k: payload[k] for k in ("status", "elapsed_seconds", "return_code", "metrics")}, indent=2))
    print(f"Saved result to {result_path}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="HGS-CVRP baseline for mTSP")
    parser.add_argument("--input", required=True)
    parser.add_argument("--m", type=int)
    parser.add_argument("--time-limit", type=int, default=60)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--output-dir", default=str(REPO_ROOT / "data" / "results" / "baselines" / "hgs_cvrp"))
    parser.add_argument("--hgs-bin", help="Path to HGS binary (default: external/HGS-CVRP/build/hgs)")
    parser.add_argument("--postprocess", choices=("none", "exact-m"), default="exact-m")
    parser.add_argument("--extra-arg", action="append", default=[],
                        help="Pass-through arguments to HGS binary (repeat as needed)")
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
