"""
N-seed × M-instance audit harness for v21 (day-1 baseline / day-3 re-bench).

Runs a fixed seed range against a list of instances using a per-instance time
budget that depends on node count, writing one JSON file per run plus a
summary.json with mean/std/min/max per instance.

Usage:
    python experiments/run_audit.py \
        --solver lkh_v21_minsum \
        --instances data/mtsp/generated_multifamily/uniform_n10000_m5_r01.txt \
                    data/mtsp/generated_multifamily/uniform_n50000_m5_r01.txt \
                    data/mtsp/generated_multifamily/uniform_n100000_m5_r01.txt \
        --seeds 10 \
        --budget-by-n 10000:60000,50000:180000,100000:380000 \
        --out-dir data/results/audit/baseline \
        --tag baseline

Outputs:
    <out-dir>/runs/<instance_stem>__seed<N>.json     # full solver JSON
    <out-dir>/summary.json                            # aggregated stats

Resume: per-run JSON files that already exist and parse OK are skipped, so
re-running the same command tops up missing seeds without redoing finished work.
"""
from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
import time
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from python.cpp_updater import get_executable_path
from python.mtsp_runner import run_solver as run_mtsp_solver


def parse_budget_map(spec: str) -> list[tuple[int, int]]:
    """Parse '10000:60000,50000:180000,100000:380000' -> sorted [(n_threshold, ms), ...].

    For an instance with node_count n, the chosen budget is the entry with the
    largest threshold <= n. (So an n=12000 instance uses the 10000 budget.)
    """
    pairs: list[tuple[int, int]] = []
    for chunk in spec.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        n_str, ms_str = chunk.split(":", 1)
        pairs.append((int(n_str), int(ms_str)))
    pairs.sort(key=lambda p: p[0])
    if not pairs:
        raise ValueError("--budget-by-n must contain at least one entry")
    return pairs


def budget_for_n(n: int, budget_map: list[tuple[int, int]], default_ms: int) -> int:
    """Largest-threshold-<=n match; falls back to default_ms if n is below all thresholds."""
    chosen = default_ms
    for threshold, ms in budget_map:
        if n >= threshold:
            chosen = ms
        else:
            break
    return chosen


def read_instance_header(path: Path) -> tuple[int, int]:
    with path.open("r", encoding="utf-8") as fh:
        first = fh.readline().strip()
    n, m = first.split()
    return int(n), int(m)


def per_run_path(out_dir: Path, instance_path: Path, seed: int) -> Path:
    return out_dir / "runs" / f"{instance_path.stem}__seed{seed:03d}.json"


def load_existing_run(p: Path) -> Optional[dict]:
    if not p.exists() or p.stat().st_size == 0:
        return None
    try:
        with p.open("r", encoding="utf-8") as fh:
            obj = json.load(fh)
        if not isinstance(obj, dict) or "objective" not in obj:
            return None
        return obj
    except (json.JSONDecodeError, OSError):
        return None


def aggregate_instance(
    instance_path: Path,
    n: int,
    m: int,
    budget_ms: int,
    runs: list[tuple[int, dict]],
) -> dict:
    valid_runs = [(seed, r) for seed, r in runs if r.get("valid")]
    objectives = [float(r["objective"]) for _, r in valid_runs]
    times = [float(r["time"]) for _, r in valid_runs]

    summary: dict = {
        "instance": instance_path.name,
        "node_count": n,
        "salesman_count": m,
        "budget_ms": budget_ms,
        "seeds": [seed for seed, _ in runs],
        "valid": [bool(r.get("valid")) for _, r in runs],
        "objectives": [
            float(r["objective"]) if r.get("valid") else None for _, r in runs
        ],
        "times": [float(r["time"]) for _, r in runs],
        "valid_count": len(valid_runs),
        "total_count": len(runs),
    }
    if objectives:
        mean = statistics.fmean(objectives)
        sd = statistics.stdev(objectives) if len(objectives) > 1 else 0.0
        summary.update(
            {
                "mean_objective": mean,
                "std_objective": sd,
                "min_objective": min(objectives),
                "max_objective": max(objectives),
                "cv_percent": (sd / mean * 100.0) if mean > 0 else float("nan"),
                "mean_time_seconds": statistics.fmean(times),
            }
        )
    else:
        summary.update(
            {
                "mean_objective": None,
                "std_objective": None,
                "min_objective": None,
                "max_objective": None,
                "cv_percent": None,
                "mean_time_seconds": None,
            }
        )
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--solver", default="lkh_v21_minsum", help="Solver name registered in SolverFactory.")
    parser.add_argument(
        "--instances",
        nargs="+",
        required=True,
        help="Instance file paths (relative to repo root or absolute).",
    )
    parser.add_argument("--seeds", type=int, default=10, help="Number of seeds (1..N).")
    parser.add_argument(
        "--budget-by-n",
        default="10000:60000,50000:180000,100000:380000",
        help="Comma-separated n_threshold:ms entries; per-instance budget = largest threshold <= n.",
    )
    parser.add_argument("--default-budget-ms", type=int, default=60_000, help="Budget for instances below all thresholds.")
    parser.add_argument("--threads", type=int, default=0, help="Override num_threads (0 = let AutoTune decide).")
    parser.add_argument("--out-dir", required=True, help="Output directory; per-run JSONs land in <out>/runs.")
    parser.add_argument("--tag", default=None, help="Free-text tag stored in summary.json (e.g. baseline / candidate).")
    parser.add_argument("--smoke", action="store_true", help="Sanity mode: 1 seed × 1 instance × forced 5s budget.")
    args = parser.parse_args()

    out_dir = Path(args.out_dir)
    if not out_dir.is_absolute():
        out_dir = ROOT / out_dir
    (out_dir / "runs").mkdir(parents=True, exist_ok=True)

    budget_map = parse_budget_map(args.budget_by_n)

    # Resolve instance paths.
    instance_paths: list[Path] = []
    for raw in args.instances:
        p = Path(raw)
        if not p.is_absolute():
            p = ROOT / p
        if not p.exists():
            print(f"[error] instance not found: {p}", file=sys.stderr)
            return 2
        instance_paths.append(p)

    if args.smoke:
        instance_paths = instance_paths[:1]
        seeds = [1]
    else:
        seeds = list(range(1, args.seeds + 1))

    executable = get_executable_path("mtsp")
    if not Path(executable).exists():
        print(f"[error] mtsp executable not found: {executable}", file=sys.stderr)
        return 2

    summary: dict = {
        "tag": args.tag,
        "solver": args.solver,
        "executable": str(executable),
        "seeds": seeds,
        "budget_by_n": args.budget_by_n,
        "instances": [],
        "started_at": time.time(),
    }

    wall_start = time.monotonic()
    total_runs = len(instance_paths) * len(seeds)
    run_idx = 0

    for instance_path in instance_paths:
        n, m = read_instance_header(instance_path)
        budget_ms = 5_000 if args.smoke else budget_for_n(n, budget_map, args.default_budget_ms)
        per_seed: list[tuple[int, dict]] = []

        for seed in seeds:
            run_idx += 1
            out_path = per_run_path(out_dir, instance_path, seed)
            existing = load_existing_run(out_path)
            if existing is not None:
                print(f"[{run_idx}/{total_runs}] skip (cached): {out_path.name}")
                per_seed.append((seed, existing))
                continue

            solver_args = [
                "--step", args.solver,
                "--seed", str(seed),
                "--time-budget-ms", str(budget_ms),
            ]
            if args.threads > 0:
                solver_args += ["--threads", str(args.threads)]

            print(
                f"[{run_idx}/{total_runs}] run: {instance_path.name} seed={seed} "
                f"budget_ms={budget_ms} solver={args.solver}",
                flush=True,
            )
            t_start = time.monotonic()
            try:
                output = run_mtsp_solver(Path(executable), instance_path, solver_args)
            except RuntimeError as e:
                print(f"[error] solver failed: {e}", file=sys.stderr)
                return 3
            t_end = time.monotonic()

            with out_path.open("w", encoding="utf-8") as fh:
                json.dump(output, fh, ensure_ascii=False)

            print(
                f"           done: obj={output.get('objective', 'N/A')} "
                f"valid={output.get('valid')} wall_s={t_end - t_start:.1f}",
                flush=True,
            )
            per_seed.append((seed, output))

        summary["instances"].append(aggregate_instance(instance_path, n, m, budget_ms, per_seed))

    summary["wall_time_seconds"] = time.monotonic() - wall_start
    summary["finished_at"] = time.time()

    summary_path = out_dir / "summary.json"
    with summary_path.open("w", encoding="utf-8") as fh:
        json.dump(summary, fh, ensure_ascii=False, indent=2)

    print(f"\nWrote summary: {summary_path}")
    print("Per-instance:")
    for ent in summary["instances"]:
        if ent["mean_objective"] is None:
            print(f"  {ent['instance']:<55s} no valid runs")
            continue
        print(
            f"  {ent['instance']:<55s} mean={ent['mean_objective']:.2f} "
            f"std={ent['std_objective']:.2f} cv={ent['cv_percent']:.3f}% "
            f"min={ent['min_objective']:.2f} max={ent['max_objective']:.2f} "
            f"({ent['valid_count']}/{ent['total_count']} valid)"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
