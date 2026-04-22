from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import Sequence

import numpy as np


def run_process(
    executable: Path,
    solver_args: Sequence[str],
    *,
    payload: str | None = None,
    input_file: Path | None = None,
) -> dict:
    command = [str(executable)]
    working_directory: Path | None = None
    if input_file is not None:
        command.extend(["--input-file", input_file.name])
        working_directory = input_file.parent
    command.extend(solver_args)

    process = subprocess.run(
        command,
        cwd=str(working_directory) if working_directory is not None else None,
        input=payload,
        text=True,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    if process.returncode != 0:
        raise RuntimeError(f"{' '.join(command)}\n{process.stderr}")
    return json.loads(process.stdout)


def run_latlon_solver(executable: Path, latlon: np.ndarray, solver_args: Sequence[str]) -> dict:
    payload = json.dumps({"n": int(latlon.shape[0]), "latlon": latlon.T.tolist()})
    return run_process(executable, solver_args, payload=payload)


def run_euclidean_solver(executable: Path, coords: Sequence[Sequence[float]], solver_args: Sequence[str]) -> dict:
    payload = json.dumps({"n": len(coords), "metric": "euclidean", "coords": [list(point) for point in coords]})
    return run_process(executable, solver_args, payload=payload)


def write_euclidean_instance(path: Path, coords: Sequence[Sequence[float]]) -> Path:
    lines = [f"{len(coords)} euclidean"]
    lines.extend(f"{float(point[0])} {float(point[1])}" for point in coords)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path
