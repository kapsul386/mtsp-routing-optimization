from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXPERIMENTS = ROOT / "experiments"
for p in (ROOT, EXPERIMENTS):
    if str(p) not in sys.path:
        sys.path.insert(0, str(p))

from python.cpp_updater import get_executable_path, recompiles_if_necessary
from python.mtsp_runner import read_instance, run_solver as run_mtsp_solver
from mtsp_experiment_utils import ensure_instance_family, instance_family_sort_key


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


def is_true(value: object) -> bool:
    return str(value).lower() == "true"


def read_instance_header(path: Path) -> tuple[int, int]:
    with path.open("r", encoding="utf-8") as fh:
        first_line = fh.readline().strip()
    if not first_line:
        raise ValueError(f"{path} does not contain an mTSP header line.")
    node_count, salesman_count = map(int, first_line.split())
    return node_count, salesman_count


def instance_matches_filters(instance_path: Path, config: dict) -> bool:
    normalized_row = ensure_instance_family({"instance": instance_path.name, "path": str(instance_path)})
    allowed_families = {str(value) for value in config.get("instance_families", [])}
    if allowed_families and str(normalized_row["instance_family"]) not in allowed_families:
        return False

    node_count, salesman_count = read_instance_header(instance_path)
    min_node_count = int(config.get("min_node_count", 0))
    max_node_count = int(config.get("max_node_count", 0))
    allowed_node_counts = {int(value) for value in config.get("allowed_node_counts", [])}
    allowed_salesman_counts = {int(value) for value in config.get("allowed_salesman_counts", [])}

    if node_count < min_node_count:
        return False
    if max_node_count > 0 and node_count > max_node_count:
        return False
    if allowed_node_counts and node_count not in allowed_node_counts:
        return False
    if allowed_salesman_counts and salesman_count not in allowed_salesman_counts:
        return False
    return True


def run_solver(executable: Path, instance_path: Path, solver_name: str, solver_args: list[str]) -> dict:
    node_count, salesman_count, _coords = read_instance(instance_path)
    try:
        output = run_mtsp_solver(executable, instance_path, solver_args)
    except RuntimeError as exc:
        # Failsafe: a single solver crash should not abort the entire batch.
        # Record an invalid row and continue.
        message = str(exc).splitlines()[-1] if str(exc) else "unknown"
        return ensure_instance_family(
            {
                "instance": instance_path.name,
                "path": normalize_display_path(instance_path),
                "node_count": node_count,
                "salesman_count": salesman_count,
                "solver": solver_name,
                "objective": "",
                "time_seconds": 0.0,
                "step_time_seconds": 0.0,
                "valid": False,
                "status": f"crashed:{message[:80]}",
                "steps": "[]",
                "routes": "[]",
            }
        )
    valid = bool(output["valid"])
    return ensure_instance_family(
        {
        "instance": instance_path.name,
        "path": normalize_display_path(instance_path),
        "node_count": node_count,
        "salesman_count": salesman_count,
        "solver": solver_name,
        "objective": float(output["objective"]) if valid else "",
        "time_seconds": float(output["time"]),
        "step_time_seconds": float(output["steps"][-1]["time"]) if output.get("steps") else float(output["time"]),
        "valid": valid,
        "status": str(output.get("status", "ok")),
        "steps": json.dumps(output.get("steps", []), ensure_ascii=False),
        "routes": json.dumps(output["routes"], ensure_ascii=False),
        }
    )


def write_csv(path: Path, rows: list[dict], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def aggregate(rows: list[dict]) -> list[dict]:
    grouped: dict[tuple[str, int, int, str], list[dict]] = defaultdict(list)
    for row in rows:
        normalized_row = ensure_instance_family(row)
        grouped[
            (
                str(normalized_row["instance_family"]),
                int(normalized_row["node_count"]),
                int(normalized_row["salesman_count"]),
                str(normalized_row["solver"]),
            )
        ].append(normalized_row)

    summary = []
    for (instance_family, node_count, salesman_count, solver), items in sorted(
        grouped.items(),
        key=lambda item: (
            instance_family_sort_key(item[0][0]),
            item[0][1],
            item[0][2],
            item[0][3],
        ),
    ):
        valid_items = [item for item in items if item["valid"]]
        avg_objective = round(sum(item["objective"] for item in valid_items) / len(valid_items), 6) if valid_items else ""
        avg_time = round(sum(item["time_seconds"] for item in items) / len(items), 6)
        avg_step_time = round(sum(item["step_time_seconds"] for item in items) / len(items), 6)
        summary.append(
            {
                "instance_family": instance_family,
                "node_count": node_count,
                "salesman_count": salesman_count,
                "solver": solver,
                "runs": len(items),
                "avg_objective": avg_objective,
                "avg_time_seconds": avg_time,
                "avg_step_time_seconds": avg_step_time,
                "valid_runs": len(valid_items),
            }
        )
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description="Run reproducible baseline mTSP experiments.")
    parser.add_argument("--config", default="experiments/configs/stratum1_small_config.json", help="Path to experiment config JSON.")
    args = parser.parse_args()

    config_path = resolve_path(args.config)
    config = json.loads(config_path.read_text(encoding="utf-8"))

    instance_dir = resolve_path(config["instance_dir"])
    instances = sorted(instance_dir.glob(config["instance_glob"]))
    instances = [instance_path for instance_path in instances if instance_matches_filters(instance_path, config)]
    if not instances:
        raise RuntimeError(f"No instances matched in {instance_dir}")

    executable = get_executable_path("mtsp")
    recompiles_if_necessary(exe_path=executable)

    rows = []
    for instance_path in instances:
        for solver in config["solvers"]:
            rows.append(run_solver(executable, instance_path, extract_solver_name(solver), solver["args"]))

    results_csv = resolve_path(config["results_csv"])
    summary_csv = resolve_path(config["summary_csv"])

    write_csv(
        results_csv,
        rows,
        [
            "instance_family", "instance", "path", "node_count", "salesman_count", "solver", "objective",
            "time_seconds", "step_time_seconds", "valid", "status", "steps", "routes",
        ],
    )
    write_csv(
        summary_csv,
        aggregate(rows),
        [
            "instance_family", "node_count", "salesman_count", "solver", "runs", "avg_objective",
            "avg_time_seconds", "avg_step_time_seconds", "valid_runs",
        ],
    )


if __name__ == "__main__":
    main()
