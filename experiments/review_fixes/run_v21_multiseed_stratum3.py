"""
Multi-seed v21_minsum on stratum-3 (N=25K, 50K, 100K) — 3 seeds × 18 instances.

Closes reviewer item 1.4: "Только 1 повтор на N≥25K — статистически невалидно".

Wraps run_audit.py with stratum-3 instances and seeds. Per-instance budget:
  N=25K  → 150s (matches existing CSV runtime)
  N=50K  → 180s
  N=100K → 350s

Total estimated wall-clock: ~3.5 hours for 3 seeds × 18 instances at the
above budgets. With resume on, can be split across multiple sessions.

Output: data/results/audit/stratum3_multiseed/
  - runs/<instance>__seed<N>.json
  - summary.json
"""
from __future__ import annotations
import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INSTANCES_DIR = ROOT / "data" / "mtsp" / "generated_multifamily"
OUT_DIR = ROOT / "data" / "results" / "audit" / "stratum3_multiseed"

FAMILIES = ["uniform", "clustered-center", "clustered-offset-depot"]
N_VALUES = [25000, 50000, 100000]
M_VALUES = [5, 7]


def list_instances() -> list[str]:
    """Return list of instance file paths to run."""
    paths = []
    for fam in FAMILIES:
        for n in N_VALUES:
            for m in M_VALUES:
                fn = f"{fam}_n{n}_m{m}_r01.txt"
                fp = INSTANCES_DIR / fn
                if fp.exists():
                    paths.append(str(fp))
                else:
                    print(f"  MISSING: {fp}", file=sys.stderr)
    return paths


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--solver", default="lkh_v21_minsum")
    parser.add_argument("--seeds", type=int, default=3,
                        help="Number of seeds (1..N). Existing per-seed JSONs are skipped.")
    parser.add_argument("--tag", default="stratum3_multiseed")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    instances = list_instances()
    print(f"Stratum-3 multi-seed: {len(instances)} instances × {args.seeds} seeds")
    print(f"Output dir: {OUT_DIR}")
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    cmd = [
        sys.executable,
        str(ROOT / "experiments" / "run_audit.py"),
        "--solver", args.solver,
        "--instances", *instances,
        "--seeds", str(args.seeds),
        "--budget-by-n", "25000:150000,50000:180000,100000:350000",
        "--out-dir", str(OUT_DIR),
        "--tag", args.tag,
    ]

    print(f"\nCommand:\n  {' '.join(cmd)}\n")
    if args.dry_run:
        return

    proc = subprocess.run(cmd, cwd=str(ROOT))
    sys.exit(proc.returncode)


if __name__ == "__main__":
    main()
