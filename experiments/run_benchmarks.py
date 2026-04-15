from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from python.cpp_updater import get_executable_path, recompiles_if_necessary


def read_instance(path: Path) -> tuple[int, int, list[tuple[float, float]]]:
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    node_count, salesman_count = map(int, lines[0].split())
    coords = [tuple(map(float, line.split())) for line in lines[1:]]
    if len(coords) != node_count:
        raise ValueError(f"{path}: expected {node_count} coordinates, got {len(coords)}")
    return node_count, salesman_count, coords


def run_solver(executable: Path, instance_path: Path, solver_args: list[str]) -> dict:
    node_count, salesman_count, coords = read_instance(instance_path)
    payload = json.dumps({"n": node_count, "m": salesman_count, "coords": coords})
    process = subprocess.run([str(executable)] + solver_args, input=payload, text=True, capture_output=True)
    if process.returncode != 0:
        raise RuntimeError(f"{instance_path} | {' '.join(solver_args)}\n{process.stderr}")

    output = json.loads(process.stdout)
    return {
        "instance": instance_path.name,
        "path": str(instance_path),
        "node_count": node_count,
        "salesman_count": salesman_count,
        "solver": solver_args[1] if len(solver_args) >= 2 else "unknown",
        "objective": float(output["objective"]),
        "time_seconds": float(output["time"]),
        "step_time_seconds": float(output["steps"][-1]["time"]) if output.get("steps") else float(output["time"]),
        "valid": bool(output["valid"]),
        "steps": json.dumps(output.get("steps", []), ensure_ascii=False),
        "routes": json.dumps(output["routes"], ensure_ascii=False)
    }


def write_csv(path: Path, rows: list[dict], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def aggregate(rows: list[dict]) -> list[dict]:
    grouped: dict[tuple[int, int, str], list[dict]] = defaultdict(list)
    for row in rows:
        grouped[(row["node_count"], row["salesman_count"], row["solver"])].append(row)

    summary = []
    for (node_count, salesman_count, solver), items in sorted(grouped.items()):
        avg_objective = sum(item["objective"] for item in items) / len(items)
        avg_time = sum(item["time_seconds"] for item in items) / len(items)
        valid_runs = sum(1 for item in items if item["valid"])
        summary.append(
            {
                "node_count": node_count,
                "salesman_count": salesman_count,
                "solver": solver,
                "runs": len(items),
                "avg_objective": round(avg_objective, 6),
                "avg_time_seconds": round(avg_time, 6),
                "avg_step_time_seconds": round(sum(item["step_time_seconds"] for item in items) / len(items), 6),
                "valid_runs": valid_runs
            }
        )
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description="Run reproducible baseline mTSP experiments.")
    parser.add_argument("--config", default="experiments/config.json", help="Path to experiment config JSON.")
    args = parser.parse_args()

    config_path = Path(args.config)
    config = json.loads(config_path.read_text(encoding="utf-8"))

    instance_dir = Path(config["instance_dir"])
    instances = sorted(instance_dir.glob(config["instance_glob"]))
    if not instances:
        raise RuntimeError(f"No instances matched in {instance_dir}")

    executable = get_executable_path("mtsp")
    recompiles_if_necessary(exe_path=executable)

    rows = []
    for instance_path in instances:
        for solver in config["solvers"]:
            rows.append(run_solver(executable, instance_path, solver["args"]))

    results_csv = Path(config["results_csv"])
    summary_csv = Path(config["summary_csv"])

    write_csv(
        results_csv,
        rows,
        [
            "instance", "path", "node_count", "salesman_count", "solver", "objective",
            "time_seconds", "step_time_seconds", "valid", "steps", "routes"
        ]
    )
    write_csv(
        summary_csv,
        aggregate(rows),
        [
            "node_count", "salesman_count", "solver", "runs", "avg_objective",
            "avg_time_seconds", "avg_step_time_seconds", "valid_runs"
        ]
    )


if __name__ == "__main__":
    main()
