"""
Extra seeds for stratum-3 multi-seed audit: extend from 3 to 5 seeds.

Closes the bootstrap-CI tightening pending from level-1.
Uses run_audit.py with seeds 4 and 5 (resume mode skips already-done seeds 1-3).
"""
from __future__ import annotations
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
    paths = []
    for fam in FAMILIES:
        for n in N_VALUES:
            for m in M_VALUES:
                fn = f"{fam}_n{n}_m{m}_r01.txt"
                fp = INSTANCES_DIR / fn
                if fp.exists():
                    paths.append(str(fp))
    return paths


def main() -> None:
    instances = list_instances()
    print(f"Extending stratum-3 multiseed: {len(instances)} instances × 5 seeds (3 already done, 2 new)")
    cmd = [
        sys.executable,
        str(ROOT / "experiments" / "run_audit.py"),
        "--solver", "lkh_v21_minsum",
        "--instances", *instances,
        "--seeds", "5",
        "--budget-by-n", "25000:150000,50000:180000,100000:350000",
        "--out-dir", str(OUT_DIR),
        "--tag", "stratum3_multiseed_5seeds",
    ]
    proc = subprocess.run(cmd, cwd=str(ROOT))
    sys.exit(proc.returncode)


if __name__ == "__main__":
    main()
