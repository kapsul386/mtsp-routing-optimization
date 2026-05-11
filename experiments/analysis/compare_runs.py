"""
Paired comparison of two run-set directories produced by run_audit.py.

Matches per-instance, per-seed pairs and reports:
  - Per-instance: mean baseline / mean candidate / mean delta % / Wilcoxon p
    on signed differences / % seeds improved
  - Aggregate sign across all instances

Usage:
    python experiments/analysis/compare_runs.py \
        --baseline  data/results/audit/<baseline_run_dir> \
        --candidate data/results/audit/<candidate_run_dir> \
        [--instance uniform_n100000_m5_r01.txt]   # filter to one instance

Both directories must follow the run_audit.py layout: <dir>/runs/<stem>__seedNNN.json.
"""
from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path
from typing import Optional

try:
    from scipy import stats  # type: ignore
    HAS_SCIPY = True
except ImportError:
    HAS_SCIPY = False


def load_run(path: Path) -> Optional[dict]:
    try:
        with path.open("r", encoding="utf-8") as fh:
            d = json.load(fh)
        if not d.get("valid"):
            return None
        return d
    except (json.JSONDecodeError, OSError):
        return None


def parse_seed_from_name(stem: str) -> Optional[int]:
    """Extract seed number from `<instance>__seedNNN`."""
    if "__seed" not in stem:
        return None
    try:
        return int(stem.split("__seed", 1)[1])
    except ValueError:
        return None


def collect(dir_path: Path) -> dict[tuple[str, int], dict]:
    """Returns {(instance_filename, seed): run_json} from <dir>/runs/*.json."""
    runs_dir = dir_path / "runs"
    if not runs_dir.is_dir():
        raise SystemExit(f"[error] not a runs directory: {runs_dir}")
    out: dict[tuple[str, int], dict] = {}
    for p in sorted(runs_dir.glob("*.json")):
        stem = p.stem
        seed = parse_seed_from_name(stem)
        if seed is None:
            continue
        instance = stem.split("__seed", 1)[0] + ".txt"
        d = load_run(p)
        if d is None:
            continue
        out[(instance, seed)] = d
    return out


def paired_stats(diffs: list[float]) -> dict:
    """Compute paired statistics for candidate-minus-baseline differences."""
    n = len(diffs)
    out: dict = {"n": n}
    if n == 0:
        return out
    out["mean_diff"] = statistics.fmean(diffs)
    out["std_diff"] = statistics.stdev(diffs) if n > 1 else 0.0
    out["sign_pos"] = sum(1 for x in diffs if x > 0)
    out["sign_neg"] = sum(1 for x in diffs if x < 0)
    out["sign_zero"] = sum(1 for x in diffs if x == 0)
    if HAS_SCIPY and n >= 5 and any(d != 0 for d in diffs):
        try:
            res = stats.wilcoxon(diffs, alternative="less")  # H1: candidate < baseline (improvement)
            out["wilcoxon_p_less"] = float(res.pvalue)
            res2 = stats.wilcoxon(diffs)  # two-sided
            out["wilcoxon_p_two"] = float(res2.pvalue)
        except (ValueError, RuntimeWarning):
            out["wilcoxon_p_less"] = None
            out["wilcoxon_p_two"] = None
    else:
        out["wilcoxon_p_less"] = None
        out["wilcoxon_p_two"] = None
    return out


def fmt_pvalue(p: Optional[float]) -> str:
    if p is None:
        return "  n/a"
    if p < 0.001:
        return "<0.001"
    return f"{p:.3f}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--instance", default=None, help="Filter to a single instance filename (e.g. uniform_n100000_m5_r01.txt).")
    args = parser.parse_args()

    baseline_dir = Path(args.baseline)
    candidate_dir = Path(args.candidate)
    if not baseline_dir.is_absolute():
        baseline_dir = (Path(__file__).resolve().parents[1] / args.baseline).resolve()
    if not candidate_dir.is_absolute():
        candidate_dir = (Path(__file__).resolve().parents[1] / args.candidate).resolve()

    base = collect(baseline_dir)
    cand = collect(candidate_dir)

    by_instance: dict[str, list[tuple[int, float, float]]] = defaultdict(list)
    for (inst, seed), d_cand in cand.items():
        d_base = base.get((inst, seed))
        if d_base is None:
            continue
        if args.instance and inst != args.instance:
            continue
        by_instance[inst].append((seed, float(d_base["objective"]), float(d_cand["objective"])))

    if not by_instance:
        print("[error] no matched (instance, seed) pairs.", file=sys.stderr)
        return 2

    print("=" * 100)
    print(f"PAIRED COMPARISON  baseline={baseline_dir.name}  vs  candidate={candidate_dir.name}")
    if not HAS_SCIPY:
        print("[note] scipy not available — Wilcoxon p-values will be omitted.")
    print("=" * 100)

    overall_diffs_pct: list[float] = []
    for inst in sorted(by_instance):
        rows = sorted(by_instance[inst])
        diffs = [c - b for _, b, c in rows]
        diffs_pct = [(c - b) / b * 100.0 for _, b, c in rows]
        base_objs = [b for _, b, _ in rows]
        cand_objs = [c for _, _, c in rows]
        overall_diffs_pct.extend(diffs_pct)

        ps = paired_stats(diffs)

        print(f"\n--- {inst} ({len(rows)} paired seeds) ---")
        print(f"  {'seed':>4s} {'baseline':>14s} {'candidate':>14s} {'diff':>12s} {'diff %':>8s}")
        for (seed, b, c), dpct in zip(rows, diffs_pct):
            marker = "  IMPROVED" if c < b else ("  worse" if c > b else "")
            print(f"  {seed:>4d} {b:>14,.2f} {c:>14,.2f} {c-b:>+12,.2f} {dpct:>+7.3f}%{marker}")
        print()
        mean_b = statistics.fmean(base_objs)
        mean_c = statistics.fmean(cand_objs)
        mean_dpct = (mean_c - mean_b) / mean_b * 100.0
        print(f"  mean baseline  = {mean_b:>14,.2f}")
        print(f"  mean candidate = {mean_c:>14,.2f}")
        print(f"  mean delta     = {mean_c - mean_b:>+14,.2f}  ({mean_dpct:+.3f}%)")
        print(f"  improved seeds = {ps['sign_neg']}/{ps['n']}   worse: {ps['sign_pos']}   tied: {ps['sign_zero']}")
        if ps["wilcoxon_p_less"] is not None:
            improved_str = "**IMPROVED**" if ps["wilcoxon_p_less"] < 0.05 else "not significant"
            print(f"  Wilcoxon (cand<base, one-sided)  p = {fmt_pvalue(ps['wilcoxon_p_less'])}  -> {improved_str}")
            print(f"  Wilcoxon two-sided                p = {fmt_pvalue(ps['wilcoxon_p_two'])}")

    if len(by_instance) > 1:
        print("\n" + "=" * 100)
        print("OVERALL (across all instances, all seeds — paired %-diffs)")
        ps_all = paired_stats(overall_diffs_pct)
        print(f"  n pairs = {ps_all['n']}")
        print(f"  mean %-diff  = {ps_all['mean_diff']:+.3f}%")
        print(f"  improved/worse/tied = {ps_all['sign_neg']}/{ps_all['sign_pos']}/{ps_all['sign_zero']}")
        if ps_all["wilcoxon_p_less"] is not None:
            improved_str = "**IMPROVED**" if ps_all["wilcoxon_p_less"] < 0.05 else "not significant"
            print(f"  Wilcoxon (cand<base, one-sided) p = {fmt_pvalue(ps_all['wilcoxon_p_less'])}  -> {improved_str}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
