#!/usr/bin/env python3
"""Run FILO2 as an external CVRP baseline for this MTSP repository."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import time
from pathlib import Path

from mtsp_to_cvrp import read_mtsp_instance, write_cvrp
from parse_filo2_solution import (
    compute_metrics,
    parse_routes_from_file,
    parse_routes_from_text,
    postprocess_to_m_routes,
)


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]


def tail(text: str, limit: int = 5000) -> str:
    return text[-limit:] if len(text) > limit else text


def default_filo2_bin() -> Path:
    candidates = [
        REPO_ROOT / "external" / "filo2" / "build" / "filo2",
        REPO_ROOT / "external" / "filo2" / "build" / "filo2.exe",
        REPO_ROOT / "external" / "filo2" / "build" / "Release" / "filo2.exe",
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def find_new_solution_files(raw_dir: Path, started_at: float) -> list[Path]:
    if not raw_dir.exists():
        return []
    candidates: list[Path] = []
    for path in raw_dir.rglob("*"):
        if not path.is_file():
            continue
        if path.stat().st_mtime + 1.0 < started_at:
            continue
        if path.suffix.lower() in {".sol", ".txt", ".out", ".log"}:
            candidates.append(path)
    return sorted(candidates, key=lambda p: p.stat().st_mtime, reverse=True)


def parse_peak_memory_mb(stderr: str) -> float | None:
    match = re.search(r"Maximum resident set size.*?:\s*(\d+)", stderr)
    if not match:
        return None
    return int(match.group(1)) / 1024.0


def run_filo2(
    mtsp_input: Path,
    m: int | None,
    time_limit: int,
    seed: int,
    output_dir: Path,
    filo2_bin: Path,
    postprocess: str,
    extra_args: list[str],
) -> Path:
    n, instance_m, coords = read_mtsp_instance(mtsp_input)
    requested_m = m if m is not None else instance_m

    tmp_dir = REPO_ROOT / "data" / "cvrp_tmp"
    tmp_dir.mkdir(parents=True, exist_ok=True)
    output_dir.mkdir(parents=True, exist_ok=True)

    instance_name = mtsp_input.stem
    cvrp_path = tmp_dir / f"{instance_name}_m{requested_m}.vrp"
    capacity = write_cvrp(coords, requested_m, cvrp_path, name=instance_name)

    if not filo2_bin.exists():
        raise FileNotFoundError(
            f"FILO2 binary not found: {filo2_bin}. "
            "Run baseline/filo2/install_filo2.sh and baseline/filo2/build_filo2.sh first."
        )

    raw_out_dir = output_dir / "raw" / f"{instance_name}_m{requested_m}_seed{seed}"
    raw_out_dir.mkdir(parents=True, exist_ok=True)

    cmd = [
        str(filo2_bin),
        str(cvrp_path),
        "--optimization-seconds",
        str(time_limit),
        "--seed",
        str(seed),
        "--outpath",
        str(raw_out_dir),
        *extra_args,
    ]

    started_wall = time.time()
    completed = subprocess.run(
        cmd,
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    elapsed = time.time() - started_wall

    raw_routes: list[list[int]] = []
    solution_source: str | None = None
    for candidate in find_new_solution_files(raw_out_dir, started_wall):
        parsed = parse_routes_from_file(candidate, n)
        if parsed:
            raw_routes = parsed
            solution_source = str(candidate)
            break

    if not raw_routes:
        raw_routes = parse_routes_from_text(completed.stdout + "\n" + completed.stderr, n)
        if raw_routes:
            solution_source = "stdout/stderr"

    final_routes = raw_routes
    postprocess_actions: list[str] = []
    if raw_routes and postprocess == "exact-m":
        final_routes, postprocess_actions = postprocess_to_m_routes(raw_routes, requested_m, coords)

    raw_metrics = compute_metrics(raw_routes, coords, requested_m) if raw_routes else None
    final_metrics = compute_metrics(final_routes, coords, requested_m) if raw_routes else None

    result_stem = f"{instance_name}_m{requested_m}_seed{seed}"
    routes_path: str | None = None
    if raw_routes:
        routes_file = output_dir / f"{result_stem}_routes.json"
        routes_file.write_text(json.dumps(final_routes, separators=(",", ":")), encoding="utf-8")
        routes_path = str(routes_file)

    result = {
        "algorithm": "FILO2_CVRP_adapter",
        "status": "ok" if completed.returncode == 0 and raw_routes else "no_solution_parsed",
        "source_instance": str(mtsp_input),
        "converted_instance": str(cvrp_path),
        "solution_source": solution_source,
        "routes_path": routes_path,
        "n_vertices": n,
        "n_customers": n - 1,
        "m_requested": requested_m,
        "capacity": capacity,
        "time_limit_seconds": time_limit,
        "seed": seed,
        "elapsed_seconds": elapsed,
        "return_code": completed.returncode,
        "peak_memory_mb": parse_peak_memory_mb(completed.stderr),
        "postprocess": postprocess,
        "postprocess_actions": postprocess_actions,
        "raw_metrics": raw_metrics,
        "metrics": final_metrics,
        "stdout_tail": tail(completed.stdout),
        "stderr_tail": tail(completed.stderr),
        "command": cmd,
    }

    result_path = output_dir / f"{result_stem}.json"
    result_path.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("status", "elapsed_seconds", "return_code", "metrics")}, indent=2))
    print(f"Saved result to {result_path}")
    return result_path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--m", type=int)
    parser.add_argument("--time-limit", type=int, default=300)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--output-dir", default=str(REPO_ROOT / "data" / "results" / "baselines" / "filo2"))
    parser.add_argument("--filo2-bin", default=str(default_filo2_bin()))
    parser.add_argument("--postprocess", choices=("none", "exact-m"), default="exact-m")
    parser.add_argument("--extra-arg", action="append", default=[])
    args = parser.parse_args()

    run_filo2(
        mtsp_input=Path(args.input),
        m=args.m,
        time_limit=args.time_limit,
        seed=args.seed,
        output_dir=Path(args.output_dir),
        filo2_bin=Path(args.filo2_bin),
        postprocess=args.postprocess,
        extra_args=args.extra_arg,
    )


if __name__ == "__main__":
    main()

