#!/usr/bin/env python3
"""Aggregate and compare results across all baselines + v21 in one table.

Walks through one or more baseline-result directories, classifies each JSON
by its `algorithm` field (or by directory layout), extracts standardized
metrics, and prints a comparison table grouped by (instance, m).

Supported result-shapes:
  • FILO2/OR-Tools/PyVRP/VeRyPy/HGS/VROOM: top-level JSON with `algorithm`,
    `metrics` (total_distance, max_route_distance, imbalance, routes_count),
    `elapsed_seconds`, `seed`, `m_requested`, `n_vertices`.
  • v21 from run_audit.py: nested under `runs/*.json`, with top-level
    `objective`, `valid`, and `metadata.final_max`, `metadata.salesman_count`,
    `metadata.total_elapsed_ms`, `metadata.seed`, `metadata.node_count`,
    `metadata.budget_ms`.

Output:
  • Per-(instance,m) table: solver, seeds n, mean MINSUM, mean max_route,
    mean imbalance, mean wall_s. CSV format optional via --csv.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path
from typing import Iterable


def find_result_jsons(roots: list[Path]) -> Iterable[Path]:
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*.json"):
            name = path.name
            # Skip route-only artifact files and run-summary files.
            if name.endswith("_routes.json"):
                continue
            if name == "summary.json":
                continue
            yield path


def classify(payload: dict, path: Path) -> str | None:
    """Return canonical solver name, or None if this file isn't a baseline result."""
    algo = payload.get("algorithm")
    if isinstance(algo, str):
        return algo
    # v21 format heuristic: has metadata + objective + routes
    if isinstance(payload.get("metadata"), dict) and "objective" in payload:
        # Try to infer solver from metadata or path. metadata may have "step" key in some variants.
        md = payload["metadata"]
        if "step" in md:
            return f"v21_{md['step']}"
        # Fall back to detecting from path: data/results/.../v21_minsum_cap/runs/*
        for part in path.parts:
            if part.startswith("v21_"):
                return part.replace("_n50k", "").replace("_n100k", "").split("_uniform")[0]
        return "lkh_v21_minsum_cap"  # default v21
    return None


def extract(payload: dict, solver: str) -> dict | None:
    """Pull out standardized fields. Returns None if inadequate data."""
    # v21 path
    if isinstance(payload.get("metadata"), dict) and "objective" in payload:
        md = payload["metadata"]
        obj = payload.get("objective")
        if obj is None:
            return None
        m = int(md.get("salesman_count") or md.get("num_agents") or 0)
        max_rt = float(md.get("final_max") or 0)
        wall = float(md.get("total_elapsed_ms") or 0) / 1000.0
        budget_s = float(md.get("budget_ms") or 0) / 1000.0
        n = int(md.get("node_count") or 0)
        seed = int(md.get("seed") or 0)
        avg = obj / m if m else 0
        imbal = max_rt / avg if avg else 0
        # run_audit.py JSONs don't store source_instance; infer from filename stem.
        # Format: <instance_stem>__seed<NNN>.json
        inferred_stem = re.sub(r"__seed\d+$", "", payload.get("__path_stem", "")) or _infer_instance(payload, n, m)
        return {
            "solver": solver,
            "n": n,
            "m": m,
            "seed": seed,
            "obj": obj,
            "max_rt": max_rt,
            "imbal": imbal,
            "wall": wall,
            "budget_s": budget_s,
            "valid": payload.get("valid"),
            "instance_stem": inferred_stem,
        }

    # Common adapter path
    metrics = payload.get("metrics") or {}
    if not metrics or payload.get("status") != "ok" and not metrics.get("total_distance"):
        return None
    return {
        "solver": solver,
        "n": int(payload.get("n_vertices") or 0),
        "m": int(payload.get("m_requested") or 0),
        "seed": int(payload.get("seed") or 0),
        "obj": float(metrics.get("total_distance") or 0),
        "max_rt": float(metrics.get("max_route_distance") or 0),
        "imbal": float(metrics.get("imbalance") or 0),
        "wall": float(payload.get("elapsed_seconds") or 0),
        "budget_s": float(payload.get("time_limit_seconds") or 0),
        "valid": metrics.get("valid_cover"),
        "instance_stem": payload.get("source_instance") or "?",
    }


def _infer_instance(payload: dict, n: int, m: int) -> str:
    return f"unknown_n{n}_m{m}"


def normalise_instance_label(stem: str) -> str:
    """Trim filesystem cruft to a consistent instance label like 'uniform_n50k_m5'."""
    s = Path(stem).stem if "/" in stem or "\\" in stem else stem
    s = s.replace("_r01", "")
    s = re.sub(r"__seed\d+$", "", s)
    return s


def fmt(x, w=14, p=1):
    if x is None or x == 0:
        return f"{'?':>{w}}"
    return f"{x:>{w},.{p}f}"


def main() -> int:
    parser = argparse.ArgumentParser(description="Cross-baseline result aggregator")
    parser.add_argument("paths", nargs="+", help="Result directories (can be multiple)")
    parser.add_argument("--csv", action="store_true", help="Emit CSV instead of human-readable table")
    parser.add_argument("--solver-filter", default="", help="Only solvers whose name contains this substring")
    parser.add_argument("--min-seeds", type=int, default=1, help="Skip groups with fewer than N seeds")
    args = parser.parse_args()

    roots = [Path(p) for p in args.paths]
    files = list(find_result_jsons(roots))
    print(f"Scanning {len(files)} result JSONs from {len(roots)} root(s)...", file=sys.stderr)

    rows: list[dict] = []
    for f in files:
        try:
            payload = json.loads(f.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            continue
        # Stash file stem for adapters that don't record source_instance (e.g. run_audit).
        payload["__path_stem"] = f.stem
        solver = classify(payload, f)
        if solver is None:
            continue
        if args.solver_filter and args.solver_filter not in solver:
            continue
        rec = extract(payload, solver)
        if rec is None:
            continue
        rec["instance_stem"] = normalise_instance_label(rec["instance_stem"])
        rec["source_path"] = str(f)
        rows.append(rec)

    if not rows:
        print("No valid records found.", file=sys.stderr)
        return 1

    # Group by (instance, m, solver)
    groups: dict[tuple[str, int, str], list[dict]] = defaultdict(list)
    for r in rows:
        groups[(r["instance_stem"], r["m"], r["solver"])].append(r)

    if args.csv:
        print("instance,m,solver,n_seeds,obj_mean,obj_std,obj_cv_pct,max_rt_mean,imbal_mean,wall_mean")
        for (inst, m, solver), recs in sorted(groups.items()):
            if len(recs) < args.min_seeds:
                continue
            objs = [r["obj"] for r in recs]
            maxs = [r["max_rt"] for r in recs]
            imbs = [r["imbal"] for r in recs]
            walls = [r["wall"] for r in recs]
            obj_mean = statistics.mean(objs)
            obj_std = statistics.stdev(objs) if len(objs) > 1 else 0
            print(",".join([
                inst, str(m), solver, str(len(recs)),
                f"{obj_mean:.2f}", f"{obj_std:.2f}",
                f"{100*obj_std/obj_mean:.3f}" if obj_mean else "0",
                f"{statistics.mean(maxs):.2f}",
                f"{statistics.mean(imbs):.4f}",
                f"{statistics.mean(walls):.2f}",
            ]))
        return 0

    # Human readable: group by (instance, m), then list solvers within
    by_instance: dict[tuple[str, int], dict[str, list[dict]]] = defaultdict(lambda: defaultdict(list))
    for (inst, m, solver), recs in groups.items():
        by_instance[(inst, m)][solver] = recs

    print("=" * 110)
    for (inst, m) in sorted(by_instance):
        solvers = by_instance[(inst, m)]
        print(f"\n  Instance: {inst:<30} m={m}")
        print("  " + "-" * 106)
        print(f"  {'solver':<28}{'seeds':>6}{'obj_mean':>14}{'obj_cv':>8}{'max_rt':>14}{'imbal':>8}{'wall_s':>10}{'budget':>10}")
        for solver in sorted(solvers, key=lambda s: -statistics.mean([r["obj"] for r in solvers[s]])):
            recs = solvers[solver]
            if len(recs) < args.min_seeds:
                continue
            objs = [r["obj"] for r in recs]
            obj_mean = statistics.mean(objs)
            obj_cv = (statistics.stdev(objs) / obj_mean * 100) if (len(objs) > 1 and obj_mean) else 0
            max_mean = statistics.mean([r["max_rt"] for r in recs])
            imb_mean = statistics.mean([r["imbal"] for r in recs])
            wall_mean = statistics.mean([r["wall"] for r in recs])
            budget_mean = statistics.mean([r["budget_s"] for r in recs])
            print(f"  {solver:<28}{len(recs):>6}{fmt(obj_mean)}{obj_cv:>7.2f}%{fmt(max_mean)}{imb_mean:>7.2f} {fmt(wall_mean,10)}{fmt(budget_mean,10,0)}")
    print("\n" + "=" * 110)
    return 0


if __name__ == "__main__":
    sys.exit(main())
