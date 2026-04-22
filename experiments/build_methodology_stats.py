from __future__ import annotations

import argparse
import importlib.util
import math
from pathlib import Path

import pandas as pd
from scipy import stats


ROOT = Path(__file__).resolve().parents[1]
import sys

if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from python.mtsp_runner import run_solver as run_mtsp_solver


def load_generator_module():
    module_path = ROOT / "experiments" / "generate_mtsp_instances.py"
    spec = importlib.util.spec_from_file_location("generate_mtsp_instances", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Could not load generator module from {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def ensure_pilot_instances(pilot_dir: Path) -> list[dict]:
    generator = load_generator_module()
    generate_instance = generator.generate_instance
    write_instance = generator.write_instance

    specs = [
        {"family": "uniform", "n": 100, "m": 3, "seed": 20260421, "filename": "uniform_n100_m3_pilot.txt"},
        {"family": "clustered-center", "n": 100, "m": 3, "seed": 20260422, "filename": "clustered_center_n100_m3_pilot.txt"},
        {
            "family": "clustered-offset-depot",
            "n": 100,
            "m": 3,
            "seed": 20260423,
            "filename": "clustered_offset_depot_n100_m3_pilot.txt",
        },
        {"family": "mixed-outliers", "n": 100, "m": 3, "seed": 20260424, "filename": "mixed_outliers_n100_m3_pilot.txt"},
        {"family": "uniform", "n": 200, "m": 5, "seed": 20260425, "filename": "uniform_n200_m5_pilot.txt"},
    ]

    generation_kwargs = {
        "width": 100.0,
        "height": 100.0,
        "cluster_counts": [3, 4],
        "cluster_spread_ratio": 0.08,
        "cluster_center_margin_ratio": 0.18,
        "cluster_center_separation_ratio": 0.2,
        "depot_margin_ratio": 0.08,
        "depot_separation_ratio": 0.18,
        "mixed_outlier_ratio_min": 0.1,
        "mixed_outlier_ratio_max": 0.2,
        "mixed_outlier_cluster_distance_ratio": 0.3,
        "mixed_outlier_depot_distance_ratio": 0.22,
        "mixed_outlier_edge_band_ratio": 0.12,
    }

    pilot_dir.mkdir(parents=True, exist_ok=True)
    for spec in specs:
        path = pilot_dir / spec["filename"]
        if path.exists():
            continue
        coords = generate_instance(
            family=spec["family"],
            node_count=spec["n"],
            seed=spec["seed"],
            **generation_kwargs,
        )
        write_instance(path, spec["m"], coords)

    return specs


def run_solver(executable: Path, instance_path: Path, solver_args: list[str]) -> dict:
    return run_mtsp_solver(executable, instance_path, solver_args)


def build_pilot_outputs(seed_runs: int) -> tuple[pd.DataFrame, pd.DataFrame, pd.DataFrame]:
    executable = ROOT / "build" / "src" / "Release" / "mtsp.exe"
    if not executable.exists():
        raise RuntimeError(f"mTSP executable not found: {executable}")

    pilot_dir = ROOT / "data" / "mtsp" / "methodology_pilot"
    specs = ensure_pilot_instances(pilot_dir)
    seeds = list(range(1, seed_runs + 1))

    solver_builders = {
        "rand+nn": lambda seed: ["--step", "rand+nn", "--seed", str(seed)],
        "grasp": lambda seed: ["--step", "grasp", "--iters", "50", "--rcl", "3", "--seed", str(seed)],
        "lkh-wrapper-v2": lambda seed: [
            "--step", "lkh-wrapper-v2",
            "--rounds", "24",
            "--seed", str(seed),
            "--candidate-count", "12",
            "--lookahead-weight", "0.35",
            "--depot-weight", "0.12",
        ],
    }

    raw_rows: list[dict] = []
    for spec in specs:
        instance_path = pilot_dir / spec["filename"]
        family_label = spec["family"].replace("-", "_")
        for solver, build_args in solver_builders.items():
            for seed in seeds:
                output = run_solver(executable, instance_path, build_args(seed))
                raw_rows.append(
                    {
                        "instance": spec["filename"],
                        "family": family_label,
                        "node_count": spec["n"],
                        "salesman_count": spec["m"],
                        "solver": solver,
                        "seed": seed,
                        "objective": float(output["objective"]),
                        "time_seconds": float(output["time"]),
                        "valid": bool(output["valid"]),
                    }
                )

    raw_df = pd.DataFrame(raw_rows)

    summary_rows: list[dict] = []
    ci_projection_rows: list[dict] = []
    for (instance, solver), group in raw_df.groupby(["instance", "solver"], sort=True):
        values = group["objective"].to_numpy()
        runs = len(values)
        mean = float(values.mean())
        sd = float(values.std(ddof=1))
        cv_percent = sd / mean * 100.0
        tcrit = float(stats.t.ppf(0.975, df=runs - 1))
        ci95_halfwidth = tcrit * sd / math.sqrt(runs)

        summary_rows.append(
            {
                "instance": instance,
                "family": group["family"].iloc[0],
                "node_count": int(group["node_count"].iloc[0]),
                "salesman_count": int(group["salesman_count"].iloc[0]),
                "solver": solver,
                "runs": runs,
                "mean_objective": mean,
                "sd_objective": sd,
                "cv_percent": cv_percent,
                "ci95_halfwidth": ci95_halfwidth,
            }
        )

        for projected_runs in [10, 20, 30, 50]:
            tcrit_projected = float(stats.t.ppf(0.975, df=projected_runs - 1))
            projected_halfwidth = tcrit_projected * sd / math.sqrt(projected_runs)
            ci_projection_rows.append(
                {
                    "instance": instance,
                    "solver": solver,
                    "R": projected_runs,
                    "predicted_ci_halfwidth": projected_halfwidth,
                    "predicted_rel_halfwidth_percent": projected_halfwidth / mean * 100.0,
                }
            )

    return raw_df, pd.DataFrame(summary_rows), pd.DataFrame(ci_projection_rows)


def build_pairwise_current() -> pd.DataFrame:
    current = pd.read_csv(ROOT / "data" / "results" / "mtsp_results.csv")
    if "instance_family" not in current.columns:
        current["instance_family"] = "uniform"
    current["instance_family"] = current["instance_family"].fillna("uniform")

    pair_rows: list[dict] = []
    grouped = current.groupby(["instance_family", "node_count", "salesman_count"], sort=True)
    for (family, node_count, salesman_count), group in grouped:
        pivot = group.pivot(index="instance", columns="solver", values="objective")
        if not {"grasp", "lkh-wrapper-v2"}.issubset(pivot.columns):
            continue

        diffs = (pivot["grasp"] - pivot["lkh-wrapper-v2"]).dropna()
        if len(diffs) <= 1:
            continue

        runs = len(diffs)
        mean = float(diffs.mean())
        sd = float(diffs.std(ddof=1))
        tcrit = float(stats.t.ppf(0.975, df=runs - 1))
        ci_halfwidth = tcrit * sd / math.sqrt(runs)
        paired_t = stats.ttest_rel(pivot.loc[diffs.index, "grasp"], pivot.loc[diffs.index, "lkh-wrapper-v2"])
        try:
            wilcoxon = stats.wilcoxon(diffs)
            wilcoxon_pvalue = float(wilcoxon.pvalue)
        except ValueError:
            wilcoxon_pvalue = float("nan")

        pair_rows.append(
            {
                "family": family,
                "node_count": int(node_count),
                "salesman_count": int(salesman_count),
                "n_instances": runs,
                "mean_diff_grasp_minus_lkh": mean,
                "sd_diff": sd,
                "ci95_low": mean - ci_halfwidth,
                "ci95_high": mean + ci_halfwidth,
                "paired_t_pvalue": float(paired_t.pvalue),
                "wilcoxon_pvalue": wilcoxon_pvalue,
                "lkh_wins": int((diffs > 0).sum()),
                "grasp_wins": int((diffs < 0).sum()),
            }
        )

    return pd.DataFrame(pair_rows)


def main() -> None:
    parser = argparse.ArgumentParser(description="Build statistical artifacts for the mTSP experiment methodology appendix.")
    parser.add_argument("--seed-runs", type=int, default=20, help="Number of seeds per stochastic solver in the pilot.")
    args = parser.parse_args()

    results_dir = ROOT / "data" / "results"
    results_dir.mkdir(parents=True, exist_ok=True)

    pilot_raw, pilot_summary, pilot_ci_projection = build_pilot_outputs(seed_runs=args.seed_runs)
    pairwise_current = build_pairwise_current()

    pilot_raw.to_csv(results_dir / "methodology_pilot_runs.csv", index=False)
    pilot_summary.to_csv(results_dir / "methodology_pilot_summary.csv", index=False)
    pilot_ci_projection.to_csv(results_dir / "methodology_pilot_ci_projection.csv", index=False)
    pairwise_current.to_csv(results_dir / "methodology_pairwise_current.csv", index=False)

    print("Saved methodology statistics:")
    print("- methodology_pilot_runs.csv")
    print("- methodology_pilot_summary.csv")
    print("- methodology_pilot_ci_projection.csv")
    print("- methodology_pairwise_current.csv")


if __name__ == "__main__":
    main()
