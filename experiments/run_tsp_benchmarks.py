from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import numpy as np

from python.cpp_updater import get_executable_path, recompiles_if_necessary
from python.tsp_runner import run_latlon_solver


def resolve_path(path_str: str) -> Path:
    path = Path(path_str)
    return path if path.is_absolute() else ROOT / path


def normalize_display_path(path: Path) -> str:
    resolved = path.resolve()
    try:
        return str(resolved.relative_to(ROOT.resolve()))
    except ValueError:
        return str(resolved)


def extract_solver_name(solver_cfg: dict) -> str:
    if solver_cfg.get("name"):
        return str(solver_cfg["name"])

    args = solver_cfg.get("args", [])
    for idx, token in enumerate(args):
        if token == "--step" and idx + 1 < len(args):
            return str(args[idx + 1])
    raise ValueError(f"Could not determine solver name from config: {solver_cfg}")


def read_task(task_path: Path) -> tuple[int, list[int]]:
    lines = [line.strip() for line in task_path.read_text(encoding="utf-8").splitlines() if line.strip()]
    node_count = int(lines[0])
    ids = [int(x) for x in lines[1].split()]
    if len(ids) != node_count:
        raise ValueError(f"{task_path}: expected {node_count} ids, got {len(ids)}")
    return node_count, ids


def load_coords(coords_npz: Path) -> dict[int, tuple[float, float]]:
    data = np.load(coords_npz)["data"]
    return {int(row[0]): (float(row[1]), float(row[2])) for row in data}


def run_solver(executable: Path, coords_map: dict[int, tuple[float, float]], task_path: Path, solver_name: str,
               solver_args: list[str]) -> dict:
    node_count, ids = read_task(task_path)
    latlon = np.asarray([coords_map[node_id] for node_id in ids], dtype=np.float64)
    output = run_latlon_solver(executable, latlon, solver_args)
    return {
        "task": task_path.name,
        "path": normalize_display_path(task_path),
        "node_count": node_count,
        "solver": solver_name,
        "length": float(output["len"]),
        "time_seconds": float(output["time"]),
        "step_time_seconds": float(output["steps"][-1]["time"]) if output.get("steps") else float(output["time"]),
        "steps": json.dumps(output.get("steps", []), ensure_ascii=False),
        "route": json.dumps(output["route"], ensure_ascii=False),
    }


def write_csv(path: Path, rows: list[dict], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def aggregate(rows: list[dict]) -> list[dict]:
    grouped: dict[tuple[int, str], list[dict]] = defaultdict(list)
    for row in rows:
        grouped[(row["node_count"], row["solver"])].append(row)

    summary = []
    for (node_count, solver), items in sorted(grouped.items()):
        summary.append(
            {
                "node_count": node_count,
                "solver": solver,
                "runs": len(items),
                "avg_length": round(sum(item["length"] for item in items) / len(items), 6),
                "avg_time_seconds": round(sum(item["time_seconds"] for item in items) / len(items), 6),
                "avg_step_time_seconds": round(sum(item["step_time_seconds"] for item in items) / len(items), 6),
            }
        )
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description="Run reproducible TSP benchmark batch.")
    parser.add_argument("--config", default="experiments/tsp_config.json", help="Path to TSP benchmark config.")
    args = parser.parse_args()

    config_path = resolve_path(args.config)
    config = json.loads(config_path.read_text(encoding="utf-8"))
    task_dir = resolve_path(config["task_dir"])
    tasks = sorted(task_dir.glob("*.txt"))
    if not tasks:
        raise RuntimeError(f"No tasks found in {task_dir}")

    coords_npz = resolve_path(config["coords_npz"])
    coords_map = load_coords(coords_npz)

    executable = get_executable_path("tsp")
    recompiles_if_necessary(exe_path=executable)

    rows = []
    for task_path in tasks:
        for solver in config["solvers"]:
            rows.append(run_solver(executable, coords_map, task_path, extract_solver_name(solver), solver["args"]))

    write_csv(
        resolve_path(config["results_csv"]),
        rows,
        ["task", "path", "node_count", "solver", "length", "time_seconds", "step_time_seconds", "steps", "route"],
    )
    write_csv(
        resolve_path(config["summary_csv"]),
        aggregate(rows),
        ["node_count", "solver", "runs", "avg_length", "avg_time_seconds", "avg_step_time_seconds"],
    )


if __name__ == "__main__":
    main()
