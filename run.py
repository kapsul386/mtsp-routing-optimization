from __future__ import annotations

import argparse
import json
import logging
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np


ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from python.cpp_updater import get_executable_path, recompiles_if_necessary
from python.validate import validate_tour


def parse_args() -> tuple[argparse.Namespace, list[str]]:
    parser = argparse.ArgumentParser(description="Run baseline TSP solvers from the migrated framework.")
    parser.add_argument("--task", type=str, required=True, help="Path to task txt (line1: n_nodes, line2: ids).")
    parser.add_argument("--coords", type=str, default="World_TSP.npz", help="Path to NPZ with [idx, lat, lon].")
    return parser.parse_known_args()


def resolve_user_path(path_str: str) -> Path:
    path = Path(path_str).expanduser()
    return path if path.is_absolute() else (Path.cwd() / path).resolve()


def read_task(task_path: Path) -> Tuple[int, List[int]]:
    lines = task_path.read_text(encoding="utf-8").strip().splitlines()
    n_nodes = int(lines[0].strip())
    ids = [int(x) for x in lines[1].strip().split()]
    if len(ids) != n_nodes:
        raise ValueError(f"ids length {len(ids)} does not match n_nodes {n_nodes}")
    return n_nodes, ids


def load_coords(coords_npz: Path) -> Dict[int, Tuple[float, float]]:
    data = np.load(coords_npz)["data"]
    return {int(row[0]): (float(row[1]), float(row[2])) for row in data}


def latlon_for_selected(ids: List[int], id_to_coord: Dict[int, Tuple[float, float]]) -> np.ndarray:
    return np.asarray([id_to_coord[sid] for sid in ids], dtype=np.float64)


def save_solution(task_path: Path, route_ids: List[int]) -> Path:
    out_path = task_path.with_name(task_path.stem + "_solution.txt")
    out_path.write_text(" ".join(str(x) for x in route_ids), encoding="utf-8")
    return out_path


def main() -> None:
    args, cpp_args = parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")

    task_path = resolve_user_path(args.task)
    coords_npz = resolve_user_path(args.coords)

    n_nodes, ids = read_task(task_path)
    id_to_coord = load_coords(coords_npz)
    latlon = latlon_for_selected(ids, id_to_coord)
    payload = json.dumps({"n": n_nodes, "latlon": latlon.T.tolist()})

    executable = get_executable_path("tsp")
    recompiles_if_necessary(exe_path=executable)

    process = subprocess.run(
        [str(executable)] + cpp_args,
        input=payload,
        text=True,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    if process.returncode != 0:
        raise RuntimeError(process.stderr.strip() or "TSP executable failed")

    output = json.loads(process.stdout)
    route_pos = output["route"]
    real_time = output["time"]
    length_km = output["len"]

    ok, msg = validate_tour(route_pos, n_nodes)
    route_ids = [ids[i] for i in route_pos]
    out_path = save_solution(task_path, route_ids)

    logging.info(f"Valid: {ok} ({msg}) | Length: {length_km:.6f} km | Time: {real_time:.4f} s")
    for step in output.get("steps", []):
        logging.info(f"Step {step['name']}: len={step['len']:.6f} | time={step['time']:.6f} s")
    logging.info(f"Solution saved: {out_path}")
    logging.info(f"Route (first 25 ids): {route_ids[:25]} ...")


if __name__ == "__main__":
    main()
