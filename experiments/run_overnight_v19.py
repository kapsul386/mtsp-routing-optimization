#!/usr/bin/env python3
"""Overnight benchmark suite: 2opt+greed, lkh-wrapper-v19, FILO2, LKH3.

Usage:
    # Quick smoke (≈30s) — runs only n=100, validates plumbing
    python experiments/run_overnight_v19.py --smoke

    # Full overnight run (~5 hours)
    python experiments/run_overnight_v19.py

    # Resume after interruption (skips already-completed runs)
    python experiments/run_overnight_v19.py --resume

Layout of outputs (a fresh timestamped subdir per run, unless --resume):
    data/results/overnight_<timestamp>/
        runs/<solver>_n<N>_m<M>_seed<S>.json   (raw output JSON)
        filo2/<n>_m<M>_seed<S>/<...>           (FILO2 raw artefacts)
        progress.log                            (one line per run)
        summary.csv                             (final aggregated table)
        summary.json                            (final structured summary)
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Optional

REPO = Path(__file__).resolve().parents[1]
MTSP_EXE = REPO / "build" / "src" / "Release" / "mtsp.exe"
LKH3_BIN_WIN = REPO / "baseline" / "LKH3" / "LKH"
INSTANCES_DIR = REPO / "data" / "mtsp" / "generated_multifamily"

SEEDS = [42, 7, 13]


@dataclass
class Config:
    n: int
    m: int
    run_lkh3: bool
    run_filo2: bool
    v19_budget_ms: int
    lkh3_budget_ms: int
    filo2_time_limit_s: int
    seeds: list[int]


# ---- Test matrix --------------------------------------------------------------
# Hard cap on FILO2 wall time so it cannot run forever.
FILO2_HARD_TIMEOUT_FACTOR = 5  # subprocess timeout = time_limit * factor + 120s


def smoke_configs() -> list[Config]:
    return [
        Config(n=100, m=3, run_lkh3=True, run_filo2=True,
               v19_budget_ms=5000, lkh3_budget_ms=5000, filo2_time_limit_s=5,
               seeds=[42]),
    ]


def full_configs() -> list[Config]:
    return [
        # Small: all four solvers, three seeds
        Config(n=1000, m=3, run_lkh3=True, run_filo2=True,
               v19_budget_ms=30_000, lkh3_budget_ms=30_000, filo2_time_limit_s=30,
               seeds=SEEDS),
        Config(n=1000, m=5, run_lkh3=True, run_filo2=True,
               v19_budget_ms=30_000, lkh3_budget_ms=30_000, filo2_time_limit_s=30,
               seeds=SEEDS),
        Config(n=1000, m=7, run_lkh3=True, run_filo2=True,
               v19_budget_ms=30_000, lkh3_budget_ms=30_000, filo2_time_limit_s=30,
               seeds=SEEDS),
        # Medium-small: keep LKH3, but give it more time on n=5000+
        Config(n=5000, m=5, run_lkh3=True, run_filo2=True,
               v19_budget_ms=30_000, lkh3_budget_ms=60_000, filo2_time_limit_s=60,
               seeds=SEEDS),
        Config(n=5000, m=7, run_lkh3=True, run_filo2=True,
               v19_budget_ms=30_000, lkh3_budget_ms=60_000, filo2_time_limit_s=60,
               seeds=SEEDS),
        Config(n=10000, m=5, run_lkh3=True, run_filo2=True,
               v19_budget_ms=30_000, lkh3_budget_ms=90_000, filo2_time_limit_s=60,
               seeds=SEEDS),
        Config(n=10000, m=7, run_lkh3=True, run_filo2=True,
               v19_budget_ms=30_000, lkh3_budget_ms=90_000, filo2_time_limit_s=60,
               seeds=SEEDS),
        # Medium-large: drop LKH3 (per user spec). FILO2 may fail at 25k.
        Config(n=25000, m=5, run_lkh3=False, run_filo2=True,
               v19_budget_ms=60_000, lkh3_budget_ms=0, filo2_time_limit_s=120,
               seeds=SEEDS),
        Config(n=25000, m=7, run_lkh3=False, run_filo2=True,
               v19_budget_ms=60_000, lkh3_budget_ms=0, filo2_time_limit_s=120,
               seeds=SEEDS),
        # Bridge between medium and large
        Config(n=50000, m=5, run_lkh3=False, run_filo2=True,
               v19_budget_ms=180_000, lkh3_budget_ms=0, filo2_time_limit_s=180,
               seeds=SEEDS),
        Config(n=50000, m=7, run_lkh3=False, run_filo2=True,
               v19_budget_ms=180_000, lkh3_budget_ms=0, filo2_time_limit_s=180,
               seeds=SEEDS),
        # Large: 2opt+greed / v19 / FILO2; main result of the night
        Config(n=100000, m=5, run_lkh3=False, run_filo2=True,
               v19_budget_ms=300_000, lkh3_budget_ms=0, filo2_time_limit_s=300,
               seeds=SEEDS),
        Config(n=100000, m=7, run_lkh3=False, run_filo2=True,
               v19_budget_ms=300_000, lkh3_budget_ms=0, filo2_time_limit_s=300,
               seeds=SEEDS),
    ]


# ---- subprocess wrappers ------------------------------------------------------

def run_solver_via_mtsp_exe(instance: Path,
                            solver: str,
                            seed: int,
                            time_budget_ms: int,
                            output_path: Path,
                            extra_step_args: Optional[list[str]] = None,
                            env: Optional[dict[str, str]] = None,
                            hard_timeout_s: float = 60.0) -> dict:
    """Run a solver registered in the C++ factory and capture JSON stdout.

    `time_budget_ms <= 0` means no budget (the solver runs to convergence).
    `hard_timeout_s` is a *subprocess* timeout — should always be larger than
    any time-budget the solver can request.
    """
    cmd = [str(MTSP_EXE), "--input-file", str(instance), "--step", solver]
    if time_budget_ms > 0:
        cmd += ["--time-budget-ms", str(time_budget_ms)]
    cmd += ["--seed", str(seed)]
    if extra_step_args:
        cmd += extra_step_args

    start = time.time()
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              env=env, timeout=hard_timeout_s)
    except subprocess.TimeoutExpired as exc:
        elapsed = time.time() - start
        return {"status": "timeout_subprocess", "elapsed": elapsed,
                "stderr": (exc.stderr or b"").decode("utf-8", errors="replace")[:500]}

    elapsed = time.time() - start
    if proc.returncode != 0:
        return {"status": "nonzero_exit", "exit_code": proc.returncode,
                "stderr": proc.stderr[:500], "elapsed": elapsed}

    # Try to parse JSON from stdout. The exe prints exactly one JSON document.
    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError:
        return {"status": "parse_error", "stdout_head": proc.stdout[:500],
                "stderr": proc.stderr[:500], "elapsed": elapsed}

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(data, indent=2), encoding="utf-8")

    return {
        "status": data.get("status", "ok"),
        "objective": data.get("objective"),
        "valid": data.get("valid"),
        "elapsed": elapsed,
        "solver_time": data.get("time"),
    }


def run_filo2_via_wsl(instance: Path,
                      m: int,
                      seed: int,
                      time_limit_s: int,
                      out_dir: Path,
                      hard_timeout_s: float) -> dict:
    """Invoke FILO2 wrapper through WSL.

    The FILO2 wrapper writes its own JSON file into `out_dir`; we just locate
    the newest one after the subprocess finishes.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    # Use forward-slash paths for WSL
    instance_wsl = "/mnt/c/" + str(instance).replace("\\", "/").replace("C:/", "")
    out_dir_wsl = "/mnt/c/" + str(out_dir).replace("\\", "/").replace("C:/", "")
    cmd_str = (
        f"cd /mnt/c/Users/ddkup/coursework/mtsp-routing-optimization && "
        f"python3 baseline/filo2withoutcode/run_filo2_baseline.py "
        f"--input {instance_wsl} --m {m} --time-limit {time_limit_s} "
        f"--seed {seed} --output-dir {out_dir_wsl}"
    )
    cmd = ["wsl", "sh", "-lc", cmd_str]

    start = time.time()
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              timeout=hard_timeout_s)
    except subprocess.TimeoutExpired:
        return {"status": "timeout_subprocess", "elapsed": time.time() - start}
    elapsed = time.time() - start

    if proc.returncode != 0:
        return {"status": "nonzero_exit", "exit_code": proc.returncode,
                "stderr": proc.stderr[-500:], "elapsed": elapsed}

    # Find the JSON file the wrapper wrote (it prints "Saved result to ...").
    result_path: Optional[Path] = None
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.startswith("Saved result to "):
            candidate = Path(line[len("Saved result to "):].strip())
            # Convert WSL-ish path back to a Windows path if needed.
            if candidate.is_absolute() and not candidate.exists():
                rel = candidate.as_posix().replace("/mnt/c/", "C:/")
                candidate = Path(rel)
            if candidate.exists():
                result_path = candidate
            break
    if result_path is None:
        # Fallback: newest .json under out_dir
        json_files = list(out_dir.rglob("*.json"))
        if json_files:
            result_path = max(json_files, key=lambda p: p.stat().st_mtime)

    summary = {"status": "ok_no_json", "elapsed": elapsed}
    if result_path and result_path.exists():
        try:
            data = json.loads(result_path.read_text(encoding="utf-8"))
            metrics = data.get("metrics") or data.get("raw_metrics") or {}
            summary = {
                "status": data.get("status", "ok"),
                "objective": metrics.get("total_distance"),
                "valid": metrics.get("valid_cover"),
                "exact_m": metrics.get("exact_m"),
                "elapsed": elapsed,
                "solver_time": data.get("elapsed_seconds"),
                "result_path": str(result_path),
            }
        except (json.JSONDecodeError, OSError) as exc:
            summary = {"status": "parse_error", "error": str(exc), "elapsed": elapsed}
    return summary


# ---- Main orchestration ------------------------------------------------------

def append_log(log_path: Path, row: dict) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(row, ensure_ascii=False) + "\n")


def is_already_done(out_path: Path) -> bool:
    """A run is considered done if the output JSON exists and has objective."""
    if not out_path.exists():
        return False
    try:
        data = json.loads(out_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return False
    return data.get("objective") is not None


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--smoke", action="store_true", help="Run a tiny test set (~30s)")
    parser.add_argument("--resume", action="store_true",
                        help="Append to data/results/overnight_LATEST/ instead of new dir")
    parser.add_argument("--out-dir", default=None,
                        help="Override output directory (default: timestamped under data/results)")
    args = parser.parse_args()

    if not MTSP_EXE.exists():
        sys.exit(f"mtsp.exe not found at {MTSP_EXE} — build first")

    # Set up output directory
    if args.out_dir:
        out_dir = Path(args.out_dir)
    elif args.resume:
        # Find newest overnight_* dir
        results_root = REPO / "data" / "results"
        candidates = sorted(results_root.glob("overnight_*"), key=lambda p: p.stat().st_mtime)
        if not candidates:
            sys.exit("--resume specified but no overnight_* directory exists")
        out_dir = candidates[-1]
        print(f"[resume] using {out_dir}")
    else:
        ts = datetime.now().strftime("%Y%m%d_%H%M")
        out_dir = REPO / "data" / "results" / f"overnight_{ts}"
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"Output directory: {out_dir}")

    runs_dir = out_dir / "runs"
    filo2_dir = out_dir / "filo2"
    log_path = out_dir / "progress.log"

    configs = smoke_configs() if args.smoke else full_configs()

    # Environment for LKH3
    lkh3_env = os.environ.copy()
    lkh3_env["LKH3_WSL_BIN"] = str(LKH3_BIN_WIN)

    # Aggregate over all runs
    summary_rows: list[dict] = []

    total_runs = sum(
        (1 + 1 + (1 if c.run_lkh3 else 0) + (1 if c.run_filo2 else 0)) * len(c.seeds)
        for c in configs
    )
    done_count = 0
    skipped_count = 0
    overall_start = time.time()

    for cfg in configs:
        instance = INSTANCES_DIR / f"uniform_n{cfg.n}_m{cfg.m}_r01.txt"
        if not instance.exists():
            print(f"[SKIP] missing instance: {instance.name}")
            done_count += (1 + 1 + (1 if cfg.run_lkh3 else 0) + (1 if cfg.run_filo2 else 0)) * len(cfg.seeds)
            continue

        for seed in cfg.seeds:
            tag = f"n{cfg.n}_m{cfg.m}_seed{seed}"

            # ----- 2opt+greed -----
            done_count += 1
            out_path = runs_dir / f"2optgreed_{tag}.json"
            if is_already_done(out_path):
                print(f"[{done_count}/{total_runs}] [SKIP-done] 2opt+greed {tag}")
                skipped_count += 1
            else:
                print(f"[{done_count}/{total_runs}] 2opt+greed {tag} ...", flush=True)
                hard_timeout = max(60.0, cfg.n * 0.005 + 120.0)
                r = run_solver_via_mtsp_exe(
                    instance, "2opt+greed", seed, 0, out_path,
                    hard_timeout_s=hard_timeout)
                print(f"   -> status={r.get('status')} obj={r.get('objective')} t={r.get('elapsed'):.1f}s")
                append_log(log_path, {"solver": "2opt+greed", "tag": tag, **r})
                summary_rows.append({"solver": "2opt+greed", **cfg.__dict__,
                                     "seed": seed, **r})

            # ----- v19 -----
            done_count += 1
            out_path = runs_dir / f"v19_{tag}_t{cfg.v19_budget_ms}.json"
            if is_already_done(out_path):
                print(f"[{done_count}/{total_runs}] [SKIP-done] v19 {tag}")
                skipped_count += 1
            else:
                print(f"[{done_count}/{total_runs}] v19 {tag} budget={cfg.v19_budget_ms}ms ...", flush=True)
                hard_timeout = cfg.v19_budget_ms / 1000.0 * 2 + 60.0
                r = run_solver_via_mtsp_exe(
                    instance, "lkh-wrapper-v19", seed, cfg.v19_budget_ms, out_path,
                    hard_timeout_s=hard_timeout)
                print(f"   -> status={r.get('status')} obj={r.get('objective')} t={r.get('elapsed'):.1f}s")
                append_log(log_path, {"solver": "v19", "tag": tag, **r})
                summary_rows.append({"solver": "v19", **cfg.__dict__,
                                     "seed": seed, **r})

            # ----- LKH3 -----
            if cfg.run_lkh3:
                done_count += 1
                out_path = runs_dir / f"lkh3_{tag}.json"
                if is_already_done(out_path):
                    print(f"[{done_count}/{total_runs}] [SKIP-done] LKH3 {tag}")
                    skipped_count += 1
                else:
                    print(f"[{done_count}/{total_runs}] LKH3 {tag} budget={cfg.lkh3_budget_ms}ms ...", flush=True)
                    hard_timeout = cfg.lkh3_budget_ms / 1000.0 * 2 + 120.0
                    r = run_solver_via_mtsp_exe(
                        instance, "lkh3-baseline", seed, cfg.lkh3_budget_ms, out_path,
                        extra_step_args=["--objective", "minsum"],
                        env=lkh3_env, hard_timeout_s=hard_timeout)
                    print(f"   -> status={r.get('status')} obj={r.get('objective')} t={r.get('elapsed'):.1f}s")
                    append_log(log_path, {"solver": "lkh3-baseline", "tag": tag, **r})
                    summary_rows.append({"solver": "lkh3-baseline", **cfg.__dict__,
                                         "seed": seed, **r})

            # ----- FILO2 (CVRP adapter) -----
            if cfg.run_filo2:
                done_count += 1
                filo2_run_dir = filo2_dir / tag
                # Use a marker JSON in runs/ to track completion
                marker = runs_dir / f"filo2_{tag}.json"
                if is_already_done(marker):
                    print(f"[{done_count}/{total_runs}] [SKIP-done] FILO2 {tag}")
                    skipped_count += 1
                else:
                    print(f"[{done_count}/{total_runs}] FILO2 {tag} time-limit={cfg.filo2_time_limit_s}s ...", flush=True)
                    hard_timeout = cfg.filo2_time_limit_s * FILO2_HARD_TIMEOUT_FACTOR + 120.0
                    r = run_filo2_via_wsl(
                        instance, cfg.m, seed, cfg.filo2_time_limit_s,
                        filo2_run_dir, hard_timeout_s=hard_timeout)
                    print(f"   -> status={r.get('status')} obj={r.get('objective')} t={r.get('elapsed'):.1f}s")
                    # Save marker JSON for the resume mechanism
                    if r.get("objective") is not None:
                        marker.parent.mkdir(parents=True, exist_ok=True)
                        marker.write_text(json.dumps(r, indent=2), encoding="utf-8")
                    append_log(log_path, {"solver": "FILO2", "tag": tag, **r})
                    summary_rows.append({"solver": "FILO2", **cfg.__dict__,
                                         "seed": seed, **r})

    # Final summary
    overall_elapsed = time.time() - overall_start
    print(f"\nAll runs done in {overall_elapsed:.0f}s ({overall_elapsed/60:.1f}min). "
          f"Skipped (already-done): {skipped_count}/{total_runs}")

    # Always rebuild summary CSV from the on-disk JSONs (even when --resume).
    rebuild_summary(out_dir)


def rebuild_summary(out_dir: Path) -> None:
    """Walk runs/ and filo2/ JSONs, write summary.csv + summary.json."""
    runs_dir = out_dir / "runs"
    rows: list[dict] = []
    for p in sorted(runs_dir.glob("*.json")):
        try:
            data = json.loads(p.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError):
            continue
        # Try to infer solver / n / m / seed from the file name.
        name = p.stem  # e.g. "v19_n5000_m5_seed42_t30000"
        parts = name.split("_")
        solver = parts[0]
        n = m = seed = budget_ms = None
        for piece in parts[1:]:
            if piece.startswith("n") and piece[1:].isdigit():
                n = int(piece[1:])
            elif piece.startswith("m") and piece[1:].isdigit():
                m = int(piece[1:])
            elif piece.startswith("seed") and piece[4:].isdigit():
                seed = int(piece[4:])
            elif piece.startswith("t") and piece[1:].isdigit():
                budget_ms = int(piece[1:])
        objective = data.get("objective")
        valid = data.get("valid")
        # Walltime: prefer "time" (mtsp.exe top-level), else "elapsed".
        wall = data.get("time", data.get("elapsed", data.get("solver_time")))
        # Solver-internal time
        solver_time = data.get("time", data.get("solver_time"))
        # Some FILO2 markers have nested objective in "metrics"
        if objective is None and isinstance(data.get("metrics"), dict):
            objective = data["metrics"].get("total_distance")
        rows.append({
            "solver": solver,
            "n": n,
            "m": m,
            "seed": seed,
            "budget_ms": budget_ms,
            "objective": objective,
            "valid": valid,
            "wall_seconds": wall,
            "solver_seconds": solver_time,
            "file": p.name,
        })
    rows.sort(key=lambda r: (r["n"] or 0, r["m"] or 0, r["solver"] or "", r["seed"] or 0))

    summary_csv = out_dir / "summary.csv"
    with summary_csv.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["solver", "n", "m", "seed", "budget_ms", "objective",
                        "valid", "wall_seconds", "solver_seconds", "file"])
        writer.writeheader()
        writer.writerows(rows)
    (out_dir / "summary.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    print(f"Summary: {summary_csv} ({len(rows)} rows)")


if __name__ == "__main__":
    main()
