"""Run small-instance benchmark of v17 against baselines."""
from __future__ import annotations

import json
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build" / "src" / "Release" / "mtsp.exe"
INST_DIR = ROOT / "data" / "mtsp" / "generated_multifamily"
OUT_DIR = ROOT / "data" / "results" / "manual_runs" / "v17_small"
OUT_DIR.mkdir(parents=True, exist_ok=True)

FAMILIES = [
    "uniform",
    "clustered-center",
    "mixed",
    "outliers",
]
SIZES = [50, 100, 500, 1000]
M = 5

# (solver, time_budget_ms_for_size_<=200, time_budget_ms_for_size_>200)
SOLVERS = [
    ("rand+nn",          1000,  1000),
    ("2opt+greed",       1000,  1000),
    ("grasp",            2000,  5000),
    ("lkh-wrapper-v2",   2000,  5000),
    ("lkh-wrapper-v12",  3000, 10000),
    ("lkh-wrapper-v15",  3000, 10000),
    ("lkh-wrapper-v16",  3000, 10000),
    ("lkh-wrapper-v17",  3000, 10000),
]


def run_solver(instance_path: Path, solver: str, time_ms: int, seed: int = 42) -> dict:
    cmd = [
        str(EXE),
        "--input-file",
        str(instance_path),
        "--step",
        solver,
        "--time-budget-ms",
        str(time_ms),
        "--seed",
        str(seed),
    ]
    t0 = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    elapsed = time.perf_counter() - t0
    if proc.returncode != 0:
        return {"solver": solver, "valid": False, "error": proc.stderr[:200]}
    try:
        d = json.loads(proc.stdout)
        return {
            "solver": solver,
            "objective": d.get("objective"),
            "valid": d.get("valid", False),
            "time": d.get("time", elapsed),
        }
    except Exception as e:
        return {"solver": solver, "valid": False, "error": str(e)[:200]}


def main() -> None:
    rows = []
    total = len(FAMILIES) * len(SIZES) * len(SOLVERS)
    done = 0
    for family in FAMILIES:
        for n in SIZES:
            inst_name = f"{family}_n{n}_m{M}_r01.txt"
            inst_path = INST_DIR / inst_name
            if not inst_path.exists():
                print(f"  SKIP missing: {inst_name}")
                continue
            for solver, t_small, t_big in SOLVERS:
                t_ms = t_small if n <= 200 else t_big
                res = run_solver(inst_path, solver, t_ms)
                res["family"] = family
                res["n"] = n
                res["m"] = M
                rows.append(res)
                done += 1
                obj = res.get("objective")
                obj_str = f"{obj:.2f}" if isinstance(obj, (int, float)) else "N/A"
                print(f"[{done:>3}/{total}] {family:24} n={n:>4} {solver:20} -> obj={obj_str:>10} valid={res.get('valid')}")

    out_file = OUT_DIR / "v17_small_results.json"
    out_file.write_text(json.dumps(rows, indent=2), encoding="utf-8")
    print(f"\nSaved {len(rows)} results to {out_file}")


if __name__ == "__main__":
    main()
