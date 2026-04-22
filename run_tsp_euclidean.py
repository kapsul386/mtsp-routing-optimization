from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from python.cpp_updater import get_executable_path, recompiles_if_necessary
from python.tsp_runner import run_process
from python.validate import validate_tour


def parse_args() -> tuple[argparse.Namespace, list[str]]:
    parser = argparse.ArgumentParser(description="Run TSP solvers on a Euclidean coordinate file.")
    parser.add_argument("--input", type=str, required=True, help="Path to a Euclidean TSP instance file.")
    return parser.parse_known_args()


def resolve_user_path(path_str: str) -> Path:
    path = Path(path_str).expanduser()
    return path if path.is_absolute() else (Path.cwd() / path).resolve()


def read_node_count(path: Path) -> int:
    with path.open("r", encoding="utf-8-sig") as fh:
        header = fh.readline().strip().split()
    if not header:
        raise ValueError(f"{path} is empty.")
    return int(header[0])


def main() -> None:
    args, solver_args = parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")

    input_path = resolve_user_path(args.input)
    node_count = read_node_count(input_path)

    executable = get_executable_path("tsp")
    recompiles_if_necessary(exe_path=executable)

    output = run_process(executable, solver_args, input_file=input_path)
    ok, msg = validate_tour(output["route"], node_count)
    logging.info(f"Valid: {ok} ({msg}) | Length: {output['len']:.6f} | Time: {output['time']:.4f} s")
    for step in output.get("steps", []):
        logging.info(f"Step {step['name']}: len={step['len']:.6f} | time={step['time']:.6f} s")
    logging.info(f"Route (first 25 nodes): {output['route'][:25]} ...")


if __name__ == "__main__":
    main()
