from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from python.cpp_updater import get_executable_path, recompiles_if_necessary
from python.mtsp_runner import run_solver


def parse_args() -> tuple[argparse.Namespace, list[str]]:
    parser = argparse.ArgumentParser(description="Run unified mTSP solvers.")
    parser.add_argument("--input", type=str, required=True, help="Path to mTSP instance file.")
    return parser.parse_known_args()


def resolve_user_path(path_str: str) -> Path:
    path = Path(path_str).expanduser()
    return path if path.is_absolute() else (Path.cwd() / path).resolve()


def main() -> None:
    args, solver_args = parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")

    input_path = resolve_user_path(args.input)

    executable = get_executable_path("mtsp")
    recompiles_if_necessary(exe_path=executable)

    output = run_solver(executable, input_path, solver_args)
    status = output.get("status", "ok")
    objective_text = f"{float(output['objective']):.6f}" if output.get("valid", False) else "n/a"
    logging.info(f"Status: {status} | Valid: {output['valid']} | Objective: {objective_text} | Time: {output['time']:.4f} s")
    for step in output.get("steps", []):
        step_status = step.get("status", "ok")
        step_objective = f"{float(step['objective']):.6f}" if step.get("valid", False) else "n/a"
        logging.info(
            f"Step {step['name']}: status={step_status} | objective={step_objective} | "
            f"valid={step['valid']} | time={step['time']:.6f} s"
        )
    for idx, route in enumerate(output["routes"], start=1):
        logging.info(f"Route {idx}: {' -> '.join(map(str, route))}")


if __name__ == "__main__":
    main()
