"""
Runs FILO2 (Accorsi-Vigo 2024) on mTSP instances by adapting them to CVRP:
  - CAPACITY = ceil((n-1)/m)   (induces approximately m routes)
  - demand_i  = 1  for each customer

Limitations of this adaptation (documented per reviewer's caveat):
  - FILO2 may return m+/-1 or even fewer routes depending on geometry.
  - The number of routes is NOT a hard constraint in CVRP; it emerges from
    capacity. We document any deviation.

Targets the same 18 stratum-3 ячеек as v21 multi-seed, plus mTSPLib for full
coverage. Output goes to two CSVs:
  - filo2_stratum3_results.csv
  - filo2_mtsplib_results.csv
"""
from __future__ import annotations
import argparse
import csv
import json
import math
import os
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]
# Path to the FILO2 binary inside WSL. Override via the FILO2_WSL_BIN
# environment variable when the binary lives outside the default location.
FILO2_BIN_WSL = os.environ.get(
    "FILO2_WSL_BIN",
    "/mnt/c/Users/ddkup/coursework/external/filo2/build/filo2",
)


# ----------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------

def euclidean(p, q):
    return math.hypot(p[0] - q[0], p[1] - q[1])


def gini(values):
    n = len(values)
    if n == 0:
        return 0.0
    mean = sum(values) / n
    if mean <= 0:
        return 0.0
    s = 0.0
    for x in values:
        for y in values:
            s += abs(x - y)
    return s / (2 * n * n * mean)


def load_mtsp(path):
    with open(path) as f:
        first = f.readline().split()
        n, m = int(first[0]), int(first[1])
        coords = []
        for _ in range(n):
            xy = f.readline().split()
            coords.append((float(xy[0]), float(xy[1])))
    return coords, n, m


def to_wsl_path(p):
    p = os.path.abspath(p).replace("\\", "/")
    if p[1:3] == ":/":
        p = "/mnt/" + p[0].lower() + p[2:]
    return p


def write_cvrp(coords, n, m, out_path, scale=1000, demand_scale=10):
    """Write CVRP file with capacity = demand_scale * ceil((n-1)/m), demand=demand_scale, depot=1.

    Using demand_scale > 1 prevents some FILO2 quirks at small demand values
    (where it occasionally treats depot as a 0-demand customer).
    """
    base_cap = math.ceil((n - 1) / m)
    capacity = demand_scale * base_cap
    with open(out_path, "w") as f:
        f.write(f"NAME : tmp_mtsp\n")
        f.write(f"COMMENT : mTSP-as-CVRP (capacity={capacity}, m_target={m})\n")
        f.write(f"TYPE : CVRP\n")
        f.write(f"DIMENSION : {n}\n")
        f.write(f"EDGE_WEIGHT_TYPE : EUC_2D\n")
        f.write(f"CAPACITY : {capacity}\n")
        f.write(f"NODE_COORD_SECTION\n")
        for i, (x, y) in enumerate(coords):
            f.write(f"{i + 1} {round(x * scale)} {round(y * scale)}\n")
        f.write("DEMAND_SECTION\n")
        f.write("1 0\n")
        for i in range(1, n):
            f.write(f"{i + 1} {demand_scale}\n")
        f.write("DEPOT_SECTION\n1\n-1\n")
        f.write("EOF\n")
    return capacity


def parse_filo2_solution(sol_path):
    """Parse FILO2 .vrp.sol file → list of routes (0-indexed, with depot=0 at start/end).

    FILO2 internally indexes depot=0 and customers=1..n-1. The .vrp.sol output
    uses these internal 0-indexed IDs directly (per Solution.hpp store_to_file:
    `customer != instance.get_depot()` filters out the depot=0). So a route
    line like "Route #N: 1 5 12" means customers 1, 5, 12 in our 0-indexed scheme.
    NO conversion needed.
    """
    routes = []
    if not os.path.exists(sol_path):
        return None
    with open(sol_path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("Route #"):
                parts = line.split(":", 1)[1].strip().split()
                ints = []
                for p in parts:
                    try:
                        v = int(p)
                    except ValueError:
                        continue
                    if v > 0:  # FILO2 should never emit depot=0 inside a route
                        ints.append(v)
                if ints:
                    routes.append([0] + ints + [0])
    return routes if routes else None


def compute_metrics(coords, routes, scale=1000):
    """Compute MINSUM and equity metrics. Coords in original units, routes 0-indexed."""
    if not routes:
        return None
    lens = []
    for r in routes:
        if not r:
            lens.append(0.0)
            continue
        L = 0.0
        for i in range(len(r) - 1):
            L += euclidean(coords[r[i]], coords[r[i + 1]])
        lens.append(L)
    m = len(lens)
    total = sum(lens)
    mean = total / m if m else 0.0
    return {
        "n_routes": m,
        "sum": total,
        "makespan": max(lens),
        "min_route": min(lens),
        "range": max(lens) - min(lens),
        "std": statistics.pstdev(lens) if m >= 2 else 0.0,
        "gini": gini(lens),
        "balance_ratio": max(lens) / mean if mean > 0 else float("inf"),
        "n_nonempty": sum(1 for L in lens if L > 0),
    }


def run_filo2(inst_filename, instance_dir, mtsp_path, seed=1, time_s=60):
    coords, n, m = load_mtsp(mtsp_path)

    # Use a temp dir on local Windows that maps cleanly to WSL via /mnt/c
    tmp = tempfile.mkdtemp(prefix="filo2_run_", dir=str(ROOT / "build"))
    try:
        cvrp_path = os.path.join(tmp, "instance.vrp")
        capacity = write_cvrp(coords, n, m, cvrp_path)

        cvrp_path_wsl = to_wsl_path(cvrp_path)
        outpath_wsl = to_wsl_path(tmp) + "/"

        cmd = [
            "wsl", "-e", "bash", "-c",
            f"{FILO2_BIN_WSL} {cvrp_path_wsl} --outpath {outpath_wsl} "
            f"--optimization-seconds {time_s} --seed {seed} 2>&1"
        ]
        t0 = time.time()
        try:
            # Allow generous overhead — FILO2 setup phase (parsing + KD-tree +
            # candidate set) is not subject to --optimization-seconds and grows
            # with n. Empirically: 25K → ~30s setup; 100K → ~3min setup.
            py_timeout = max(time_s * 3, time_s + 600)
            proc = subprocess.run(cmd, capture_output=True, text=False,
                                   timeout=py_timeout)
            stdout = proc.stdout.decode("utf-8", errors="replace")
            stderr = proc.stderr.decode("utf-8", errors="replace")
            timed_out = False
        except subprocess.TimeoutExpired:
            stdout = ""
            stderr = ""
            timed_out = True

        wall = time.time() - t0

        if timed_out:
            return {
                "instance": inst_filename, "n": n, "m_target": m,
                "n_routes": 0, "valid": False, "timed_out": True,
                "time_seconds": wall, "capacity": capacity,
            }

        # Find solution file
        sol_files = [f for f in os.listdir(tmp) if f.endswith(".vrp.sol")]
        if not sol_files:
            return {
                "instance": inst_filename, "n": n, "m_target": m,
                "n_routes": 0, "valid": False, "timed_out": False,
                "time_seconds": wall, "capacity": capacity,
                "stdout_tail": stdout[-200:],
            }
        sol_path = os.path.join(tmp, sol_files[0])
        routes = parse_filo2_solution(sol_path)
        if routes is None:
            return {
                "instance": inst_filename, "n": n, "m_target": m,
                "n_routes": 0, "valid": False, "timed_out": False,
                "time_seconds": wall, "capacity": capacity,
            }

        # Validate coverage
        covered = set()
        for r in routes:
            for v in r:
                if v != 0:
                    covered.add(v)
        valid = (len(covered) == n - 1) and all(c in covered for c in range(1, n))

        bm = compute_metrics(coords, routes)
        return {
            "instance": inst_filename, "n": n, "m_target": m,
            "valid": valid, "timed_out": False,
            "time_seconds": wall, "capacity": capacity,
            **bm,
        }
    finally:
        try:
            shutil.rmtree(tmp, ignore_errors=True)
        except Exception:
            pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scope", choices=["stratum3", "mtsplib"], default="stratum3")
    parser.add_argument("--seeds", type=int, default=1)
    parser.add_argument("--time-s", type=int, default=None,
                        help="FILO2 optimization seconds (auto if not given)")
    args = parser.parse_args()

    if args.scope == "stratum3":
        instance_dir = ROOT / "data" / "mtsp" / "generated_multifamily"
        out_path = ROOT / "experiments" / "review_fixes" / "filo2_stratum3_results.csv"
        instances = []
        for fam in ["uniform", "clustered-center", "clustered-offset-depot"]:
            for n in [25000, 50000, 100000]:
                for m in [5, 7]:
                    fn = f"{fam}_n{n}_m{m}_r01.txt"
                    if (instance_dir / fn).exists():
                        instances.append((fn, n))
    else:
        instance_dir = ROOT / "data" / "mtsp" / "mtsplib"
        out_path = ROOT / "experiments" / "review_fixes" / "filo2_mtsplib_results.csv"
        instances = []
        for fn in sorted(os.listdir(instance_dir)):
            if fn.endswith(".txt"):
                instances.append((fn, None))

    fields = [
        "instance", "n", "m_target", "n_routes", "valid", "timed_out",
        "time_seconds", "capacity", "sum", "makespan", "min_route",
        "range", "std", "gini", "balance_ratio", "n_nonempty",
    ]

    done = set()
    if out_path.exists():
        with open(out_path) as f:
            for r in csv.DictReader(f):
                done.add(r["instance"])

    write_header = not out_path.exists()
    out_path.parent.mkdir(parents=True, exist_ok=True)

    with open(out_path, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        if write_header:
            w.writeheader()

        for inst_filename, n_hint in instances:
            if inst_filename in done:
                print(f"SKIP {inst_filename} (already done)")
                continue
            mtsp_path = instance_dir / inst_filename
            if args.time_s is not None:
                time_s = args.time_s
            elif n_hint is None:
                time_s = 30
            elif n_hint <= 25000:
                time_s = 150
            elif n_hint <= 50000:
                time_s = 250
            else:
                time_s = 350

            print(f"FILO2 on {inst_filename} (time={time_s}s)...", flush=True)
            r = run_filo2(inst_filename, instance_dir, mtsp_path, seed=1, time_s=time_s)
            row = {k: r.get(k) for k in fields}
            w.writerow(row)
            f.flush()
            if r.get("timed_out"):
                print(f"   TIMED OUT after {r['time_seconds']:.1f}s")
            elif r.get("valid"):
                print(f"   sum={r['sum']:.0f} routes={r['n_routes']} "
                      f"gini={r['gini']:.3f} bal={r['balance_ratio']:.2f}")
            else:
                print(f"   FAILED")


if __name__ == "__main__":
    main()
