"""
Runs LKH-3 with FAIR mTSP parameters on stratum-1 instances:
  (a) MTSP_MIN_SIZE = -1, MTSP_OBJECTIVE = MINSUM
      (the documented auto-balance: floor(N / (ceil(N/MAX_SIZE)+1)))
  (b) MTSP_MIN_SIZE = round(0.8 * (n-1)/m), MTSP_OBJECTIVE = MINMAX

Both runs are direct invocation of the same LKH binary used by the project,
through WSL.

Output: experiments/review_fixes/fair_lkh3_results.csv with columns
[instance, n, m, mode, time_seconds, sum_length, makespan, gini, balance_ratio,
 wilcoxon_paired_p_vs_v21, ...]
"""

import csv
import json
import math
import os
import statistics
import subprocess
import sys
import tempfile
import time
from collections import defaultdict

csv.field_size_limit(2**31 - 1)

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RES = os.path.join(ROOT, "data", "results")
INSTANCES_DIR = os.path.join(ROOT, "data", "mtsp", "stratum1_small")
# Path to the LKH-3 binary inside WSL. Override via the LKH3_WSL_BIN
# environment variable when the binary lives outside the default location.
LKH_WSL = os.environ.get(
    "LKH3_WSL_BIN",
    "/mnt/c/Users/ddkup/coursework/external/LKH-3.0.7/LKH",
)


# ----------------------------------------------------------------------
# Geometry & metrics
# ----------------------------------------------------------------------

def euclidean(p, q):
    return math.hypot(p[0] - q[0], p[1] - q[1])


def load_instance(instance_path):
    with open(instance_path) as f:
        first = f.readline().split()
        n, m = int(first[0]), int(first[1])
        coords = []
        for _ in range(n):
            xy = f.readline().split()
            coords.append((float(xy[0]), float(xy[1])))
    return coords, n, m


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


def balance_metrics(coords, routes):
    lens = []
    for route in routes:
        if not route:
            lens.append(0.0)
            continue
        L = 0.0
        for i in range(len(route) - 1):
            L += euclidean(coords[route[i]], coords[route[i + 1]])
        lens.append(L)
    m = len(lens)
    total = sum(lens)
    mean = total / m if m > 0 else 0.0
    return {
        "sum": total,
        "makespan": max(lens),
        "min_route": min(lens),
        "range": max(lens) - min(lens),
        "std": statistics.pstdev(lens) if m >= 2 else 0.0,
        "gini": gini(lens),
        "balance_ratio": (max(lens) / mean) if mean > 0 else float("inf"),
        "n_nonempty": sum(1 for L in lens if L > 0),
        "route_lengths": lens,
    }


# ----------------------------------------------------------------------
# LKH-3 invocation
# ----------------------------------------------------------------------

def write_tsp_file(coords, n, m, path, scale=1000):
    with open(path, "w") as f:
        f.write(f"NAME : tmp\nTYPE : TSP\nDIMENSION : {n}\n")
        f.write("EDGE_WEIGHT_TYPE : EUC_2D\n")
        f.write(f"SALESMEN : {m}\n")
        f.write("DEPOT_SECTION\n1\n-1\n")
        f.write("NODE_COORD_SECTION\n")
        for i, (x, y) in enumerate(coords):
            f.write(f"{i + 1} {x * scale:.0f} {y * scale:.0f}\n")
        f.write("EOF\n")


def write_par_file(par_path, problem_path, result_path, n, m, mode,
                    time_limit, runs=1):
    """Write LKH .par file. mode in {minsum_default, minsum_balanced, minmax_balanced}."""
    if mode == "minsum_default":
        mtsp_min_size = 1
        mtsp_max_size = 999_999_999
        objective = "MINSUM"
    elif mode == "minsum_balanced":
        # Documented auto value: -1
        mtsp_min_size = -1
        mtsp_max_size = round(1.2 * (n - 1) / m)
        objective = "MINSUM"
    elif mode == "minmax_balanced":
        mtsp_min_size = round(0.8 * (n - 1) / m)
        mtsp_max_size = round(1.2 * (n - 1) / m)
        objective = "MINMAX"
    else:
        raise ValueError(f"Unknown mode {mode}")

    with open(par_path, "w") as f:
        f.write(f"PROBLEM_FILE = {problem_path}\n")
        f.write(f"MTSP_SOLUTION_FILE = {result_path}\n")
        f.write(f"SALESMEN = {m}\n")
        f.write(f"MTSP_OBJECTIVE = {objective}\n")
        f.write(f"MTSP_MIN_SIZE = {mtsp_min_size}\n")
        if mtsp_max_size > 0:
            f.write(f"MTSP_MAX_SIZE = {mtsp_max_size}\n")
        f.write("INITIAL_TOUR_ALGORITHM = MTSP\n")
        f.write("CANDIDATE_SET_TYPE = POPMUSIC\n")
        f.write(f"RUNS = {runs}\n")
        f.write(f"TIME_LIMIT = {time_limit}\n")
        f.write("TRACE_LEVEL = 1\n")
        f.write("SEED = 1\n")


def run_lkh(par_path_wsl, timeout=600):
    """Run LKH via wsl. Return (stdout, stderr, retcode, wall_time, timed_out)."""
    t0 = time.time()
    timed_out = False
    try:
        proc = subprocess.run(
            ["wsl", "-e", "bash", "-c", f"{LKH_WSL} {par_path_wsl}"],
            capture_output=True,
            text=False,
            timeout=timeout,
        )
        stdout = proc.stdout.decode("utf-8", errors="replace")
        stderr = proc.stderr.decode("utf-8", errors="replace")
        rc = proc.returncode
    except subprocess.TimeoutExpired as ex:
        timed_out = True
        # Kill orphan LKH inside WSL
        try:
            subprocess.run(["wsl", "-e", "bash", "-c", "pkill -9 LKH"],
                            capture_output=True, timeout=10)
        except Exception:
            pass
        stdout = (ex.stdout or b"").decode("utf-8", errors="replace") if ex.stdout else ""
        stderr = (ex.stderr or b"").decode("utf-8", errors="replace") if ex.stderr else ""
        rc = -1
    wall = time.time() - t0
    return stdout, stderr, rc, wall, timed_out


def parse_lkh_solution(result_path):
    """Parse MTSP_SOLUTION_FILE in LKH-3 format.

    Each route line looks like:
      1 39 10 77 ... 86 1 (#36)  Cost: 324092
    Where 1 (1-indexed) is the depot, then customers, then 1 again, then meta-info.

    Returns list of routes as list[list[int]] with 0-indexed vertices and
    depot=0 explicitly at start and end.
    """
    if not os.path.exists(result_path):
        return None
    routes = []
    with open(result_path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            # Skip header/footer lines
            if line.startswith("#") or line.startswith("tmp,") or "tours traveled" in line:
                continue
            # A route line starts with the depot index 1; cut off everything after the
            # parenthesized count.
            # split on '(#'
            head = line.split("(#", 1)[0]
            tokens = head.split()
            ints = []
            for tok in tokens:
                try:
                    ints.append(int(tok))
                except ValueError:
                    break
            if len(ints) < 2:
                continue
            # LKH outputs 1-indexed; first token must be depot=1 and last must also be 1
            if ints[0] != 1 or ints[-1] != 1:
                continue
            # Convert: 1 -> 0 (depot), keep 0-indexed customer ids
            converted = [v - 1 for v in ints]
            routes.append(converted)
    return routes if routes else None


def to_wsl_path(p):
    p = os.path.abspath(p).replace("\\", "/")
    if p[1:3] == ":/":
        p = "/mnt/" + p[0].lower() + p[2:]
    return p


# ----------------------------------------------------------------------
# Per-instance runner
# ----------------------------------------------------------------------

def run_one(inst_file, mode, time_limit_seconds=15, runs=1, scale=1000):
    inst_path = os.path.join(INSTANCES_DIR, inst_file)
    if not os.path.exists(inst_path):
        return {"error": f"missing {inst_path}"}

    coords, n, m = load_instance(inst_path)

    # tmp dir
    with tempfile.TemporaryDirectory(prefix="fair_lkh3_") as tmp:
        prob_path = os.path.join(tmp, "problem.tsp")
        result_path = os.path.join(tmp, "result.txt")
        par_path = os.path.join(tmp, "params.par")
        write_tsp_file(coords, n, m, prob_path, scale=scale)
        write_par_file(par_path,
                        problem_path=to_wsl_path(prob_path),
                        result_path=to_wsl_path(result_path),
                        n=n, m=m, mode=mode,
                        time_limit=time_limit_seconds, runs=runs)

        stdout, stderr, rc, wall, timed_out = run_lkh(to_wsl_path(par_path))

        if timed_out:
            return {
                "instance": inst_file, "n": n, "m": m, "mode": mode,
                "time_seconds": wall, "n_routes": 0, "valid": False,
                "sum": 0, "makespan": 0, "min_route": 0,
                "range": 0, "std": 0, "gini": 0, "balance_ratio": 0,
                "n_nonempty": 0, "timed_out": True,
            }

        routes = parse_lkh_solution(result_path)
        if not routes:
            return {"error": "no solution parsed", "rc": rc,
                    "stdout_tail": stdout[-500:] if stdout else "",
                    "stderr_tail": stderr[-500:] if stderr else ""}

        # Validate
        all_clients = set()
        for r in routes:
            for v in r:
                if v != 0:
                    all_clients.add(v)
        valid = (len(all_clients) == n - 1) and all(c in all_clients for c in range(1, n))

        bm = balance_metrics(coords, routes)

    return {
        "instance": inst_file,
        "n": n, "m": m,
        "mode": mode,
        "time_seconds": wall,
        "n_routes": len(routes),
        "valid": valid,
        "sum": bm["sum"] / scale,
        "makespan": bm["makespan"] / scale,
        "min_route": bm["min_route"] / scale,
        "range": bm["range"] / scale,
        "std": bm["std"] / scale,
        "gini": bm["gini"],
        "balance_ratio": bm["balance_ratio"],
        "n_nonempty": bm["n_nonempty"],
    }


# ----------------------------------------------------------------------
# Driver
# ----------------------------------------------------------------------

def select_instances():
    """Select a small but representative subset of stratum-1 instances.
    Use all 60 for clustered-center + uniform N=100, 200, 500 (no n=1000 to keep runtime tractable).
    """
    all_files = sorted(os.listdir(INSTANCES_DIR))
    selected = []
    for fn in all_files:
        if not fn.endswith(".txt"):
            continue
        # Skip n=1000 to keep runtime ~30 min
        if "n1000_" in fn:
            continue
        selected.append(fn)
    return selected


def main():
    instances = select_instances()
    modes = ["minsum_default", "minsum_balanced", "minmax_balanced"]

    out_path = os.path.join(ROOT, "experiments", "review_fixes", "fair_lkh3_results.csv")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    fieldnames = [
        "instance", "n", "m", "mode", "time_seconds", "n_routes", "valid",
        "sum", "makespan", "min_route", "range", "std", "gini",
        "balance_ratio", "n_nonempty",
    ]

    print(f"Will run {len(instances)} instances × {len(modes)} modes = {len(instances) * len(modes)} runs")
    sys.stdout.flush()

    written = 0
    skipped = 0
    with open(out_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for i, inst in enumerate(instances):
            # Per-instance budget scales with n
            try:
                n_in = int(inst.split("_n", 1)[1].split("_", 1)[0])
            except Exception:
                n_in = 100
            time_limit = 5 if n_in <= 100 else (10 if n_in <= 200 else 20)
            for mode in modes:
                res = run_one(inst, mode, time_limit_seconds=time_limit)
                if "error" in res:
                    print(f"  [{i + 1}/{len(instances)}] {inst} {mode}: ERROR {res['error']}")
                    skipped += 1
                    continue
                # Write only the columns we need
                row = {k: res[k] for k in fieldnames if k in res}
                w.writerow(row)
                f.flush()
                written += 1
                if written % 10 == 0:
                    print(f"  [{written}] {inst} {mode}: sum={res['sum']:.1f} "
                          f"gini={res['gini']:.3f} bal={res['balance_ratio']:.2f} "
                          f"t={res['time_seconds']:.1f}s")
                    sys.stdout.flush()
        print(f"Wrote {written} runs, skipped {skipped}")


if __name__ == "__main__":
    main()
