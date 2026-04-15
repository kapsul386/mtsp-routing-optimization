from __future__ import annotations

import argparse
import json
import logging
import subprocess
from pathlib import Path
from typing import List, Tuple

from python.cpp_updater import get_executable_path, recompiles_if_necessary


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run unified mTSP solvers.")
    parser.add_argument("--input", type=str, required=True, help="Path to mTSP instance file.")
    return parser.parse_known_args()


def read_instance(path: Path) -> Tuple[int, int, List[Tuple[float, float]]]:
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    n, m = map(int, lines[0].split())
    coords = [tuple(map(float, line.split())) for line in lines[1:]]
    if len(coords) != n:
        raise ValueError(f"Expected {n} coordinates, got {len(coords)}")
    return n, m, coords


def main() -> None:
    args, solver_args = parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")

    input_path = Path(args.input)
    n, m, coords = read_instance(input_path)
    payload = json.dumps({"n": n, "m": m, "coords": coords})

    executable = get_executable_path("mtsp")
    recompiles_if_necessary(exe_path=executable)

    process = subprocess.run([str(executable)] + solver_args, input=payload, text=True, capture_output=True)
    if process.returncode != 0:
        raise RuntimeError(process.stderr)

    output = json.loads(process.stdout)
    logging.info(f"Valid: {output['valid']} | Objective: {output['objective']:.6f} | Time: {output['time']:.4f} s")
    for step in output.get("steps", []):
        logging.info(
            f"Step {step['name']}: objective={step['objective']:.6f} | "
            f"valid={step['valid']} | time={step['time']:.6f} s"
        )
    for idx, route in enumerate(output["routes"], start=1):
        logging.info(f"Route {idx}: {' -> '.join(map(str, route))}")


if __name__ == "__main__":
    main()
