from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import List, Tuple


def read_instance(path: Path) -> Tuple[int, int, List[Tuple[float, float]]]:
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    node_count, salesman_count = map(int, lines[0].split())
    coords = [tuple(map(float, line.split())) for line in lines[1:]]
    if len(coords) != node_count:
        raise ValueError(f"{path}: expected {node_count} coordinates, got {len(coords)}")
    return node_count, salesman_count, coords


def run_solver(executable: Path, instance_path: Path, solver_args: list[str]) -> dict:
    process = subprocess.run(
        [str(executable), "--input-file", instance_path.name] + solver_args,
        cwd=str(instance_path.parent),
        text=True,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    if process.returncode != 0:
        raise RuntimeError(f"{instance_path} | {' '.join(solver_args)}\n{process.stderr}")
    return json.loads(process.stdout)
