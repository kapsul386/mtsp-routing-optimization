"""
Per-run diagnostic analyzer for v21 day-1 scenario selection.

Reads one or more solver-output JSON files (from run_audit.py / runs/<*>.json)
and prints a side-by-side comparison of metadata + anytime-trace shape.

Usage:
    python experiments/analyze_run.py \
        data/results/audit/_smoke_n10k/runs/uniform_n10000_m5_r01__seed001.json \
        data/results/audit/profile_n100k/runs/uniform_n100000_m5_r01__seed001.json

Output focuses on the three Day-2 scenario hypotheses:
  - A: compute-bound -> low iters/sec on big n (re-eval bottleneck)
  - B: algorithm-bound -> early plateau, few/no reheats relative to no_improve span
  - C: PT-mixing bad -> low swap acceptance, replica bests clustered
"""
from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path


def load_meta(path: Path) -> dict:
    """Loads a run JSON and returns flattened metadata + parsed anytime trace."""
    with path.open("r", encoding="utf-8") as fh:
        d = json.load(fh)
    meta = dict(d.get("metadata") or {})
    out: dict = {
        "_path": str(path),
        "_objective": float(d.get("objective", 0.0)) if d.get("valid") else None,
        "_time_seconds": float(d.get("time", 0.0)),
        "_valid": bool(d.get("valid")),
    }
    out.update(meta)

    # Parse anytime trace if present.
    trace_str = meta.get("anytime_trace")
    if trace_str:
        try:
            out["_trace"] = json.loads(trace_str)
        except json.JSONDecodeError:
            out["_trace"] = []
    else:
        out["_trace"] = []
    return out


def fnum(s, fallback="-") -> str:
    """Pretty-print a numeric metadata value."""
    if s is None:
        return fallback
    try:
        f = float(s)
        if abs(f) >= 1000:
            return f"{f:,.0f}"
        if abs(f) >= 10:
            return f"{f:.2f}"
        return f"{f:.4f}"
    except (TypeError, ValueError):
        return str(s)


def get_int(meta: dict, key: str, default: int = 0) -> int:
    try:
        return int(meta.get(key, default))
    except (TypeError, ValueError):
        return default


def get_float(meta: dict, key: str, default: float = 0.0) -> float:
    try:
        return float(meta.get(key, default))
    except (TypeError, ValueError):
        return default


def derive_stats(m: dict) -> dict:
    """Compute scenario-selection-relevant derived stats."""
    phase5_ms = get_int(m, "phase5_ms")
    alns_iters = get_int(m, "alns_iters")
    alns_accepts = get_int(m, "alns_accepts")
    alns_best_updates = get_int(m, "alns_best_updates")
    sa_reheats = get_int(m, "sa_reheats")
    pt_replicas = get_int(m, "pt_replicas", 1)
    pt_swap_attempts = get_int(m, "pt_swaps_attempted")
    pt_swap_accepts = get_int(m, "pt_swaps_accepted")

    derived: dict = {
        "iters_per_sec": (alns_iters / phase5_ms * 1000.0) if phase5_ms > 0 else 0.0,
        "accept_rate": (alns_accepts / alns_iters) if alns_iters > 0 else 0.0,
        "best_update_rate": (alns_best_updates / alns_iters) if alns_iters > 0 else 0.0,
        "reheats_per_min": (sa_reheats / phase5_ms * 60_000.0) if phase5_ms > 0 else 0.0,
        "pt_swap_accept": (pt_swap_accepts / pt_swap_attempts) if pt_swap_attempts > 0 else None,
        "pt_replicas": pt_replicas,
    }

    # Anytime-trace plateau detection.
    trace = m.get("_trace") or []
    if trace:
        derived["trace_points"] = len(trace)
        derived["trace_first_ms"] = trace[0][0]
        derived["trace_last_ms"] = trace[-1][0]
        derived["trace_first_cost"] = trace[0][1]
        derived["trace_last_cost"] = trace[-1][1]
        # Cost reduction broken into 5 equal-time buckets — shows where progress lives.
        end_ms = trace[-1][0]
        buckets = [None, None, None, None, None]
        for ms, cost in trace:
            idx = min(4, int(ms / max(1, end_ms / 5)))
            if buckets[idx] is None or cost < buckets[idx]:
                buckets[idx] = cost
        # First-bucket cost as baseline; per-bucket relative reduction.
        b0 = buckets[0] if buckets[0] is not None else trace[0][1]
        derived["trace_buckets"] = [
            (b0 - bc) / b0 * 100.0 if bc is not None else 0.0 for bc in buckets
        ]
        # Plateau: time from last best-update to end of ALNS.
        # We can only measure within the trace, not what happened after.
        # Use last-trace-point vs phase5_ms (ALNS phase end ≈ trace_last_ms+epsilon).
        derived["plateau_tail_ms"] = max(0, phase5_ms - (trace[-1][0] - trace[0][0]))

        # No-improve gap: longest delta between consecutive trace timestamps,
        # as a fraction of total ALNS phase.
        if len(trace) >= 2:
            gaps_ms = [trace[i + 1][0] - trace[i][0] for i in range(len(trace) - 1)]
            derived["max_gap_ms"] = max(gaps_ms)
            derived["max_gap_frac"] = derived["max_gap_ms"] / max(1, phase5_ms)
        else:
            derived["max_gap_ms"] = 0
            derived["max_gap_frac"] = 0.0
    else:
        derived["trace_points"] = 0

    # Operator stats: top-3 destroy/repair by acceptance.
    destroy_ops = []
    repair_ops = []
    for k, v in m.items():
        if isinstance(k, str):
            if k.startswith("destroy_") and k.endswith("_calls"):
                op = k[len("destroy_"):-len("_calls")]
                calls = get_int(m, k)
                accepts = get_int(m, f"destroy_{op}_accepts")
                if calls > 0:
                    destroy_ops.append((op, calls, accepts, accepts / calls))
            elif k.startswith("repair_") and k.endswith("_calls"):
                op = k[len("repair_"):-len("_calls")]
                calls = get_int(m, k)
                accepts = get_int(m, f"repair_{op}_accepts")
                if calls > 0:
                    repair_ops.append((op, calls, accepts, accepts / calls))
    destroy_ops.sort(key=lambda x: -x[1])
    repair_ops.sort(key=lambda x: -x[1])
    derived["destroy_ops"] = destroy_ops
    derived["repair_ops"] = repair_ops

    return derived


def print_run(label: str, m: dict, der: dict) -> None:
    print(f"\n=== {label} ===")
    print(f"  path: {m['_path']}")
    print(f"  n={m.get('node_count')} m={m.get('salesman_count')} seed={m.get('seed')} "
          f"budget_ms={m.get('budget_ms')}")
    print(f"  objective={fnum(m.get('_objective'))} valid={m['_valid']} "
          f"wall={m['_time_seconds']:.2f}s")
    print()
    print(f"  --- phase budget (ms) ---")
    print(f"  cand={fnum(m.get('phase1_ms'))}  seed={fnum(m.get('phase2_ms'))}  "
          f"polish={fnum(m.get('phase3_ms'))}  alns={fnum(m.get('phase5_ms'))}  "
          f"final={fnum(m.get('phase6_ms'))}  total_elapsed={fnum(m.get('total_elapsed_ms'))}")
    print()
    print(f"  --- ALNS-SA loop ---")
    print(f"  iters={fnum(m.get('alns_iters'))}  accepts={fnum(m.get('alns_accepts'))}  "
          f"best_updates={fnum(m.get('alns_best_updates'))}")
    print(f"  iters/sec={der['iters_per_sec']:.1f}  accept_rate={der['accept_rate']:.3f}  "
          f"best/iter={der['best_update_rate']*100:.3f}%")
    print(f"  reheats={fnum(m.get('sa_reheats'))}  cooldowns={fnum(m.get('sa_cooldowns'))}  "
          f"reheats/min={der['reheats_per_min']:.2f}")
    print(f"  T_init={fnum(m.get('sa_T_init'))}")
    print()
    if der["pt_replicas"] > 1:
        print(f"  --- Parallel Tempering ---")
        print(f"  replicas={der['pt_replicas']}  epochs={fnum(m.get('pt_epochs'))}")
        print(f"  swap_attempts={fnum(m.get('pt_swaps_attempted'))}  "
              f"swap_accepts={fnum(m.get('pt_swaps_accepted'))}  "
              f"acc_rate={(der['pt_swap_accept'] or 0)*100:.1f}%")
        bests = []
        for r in range(der["pt_replicas"]):
            v = m.get(f"pt_rep{r}_best")
            if v is not None:
                bests.append(float(v))
        if bests:
            spread = (max(bests) - min(bests)) / min(bests) * 100.0 if min(bests) > 0 else 0.0
            print(f"  per-replica best: min={min(bests):,.0f} max={max(bests):,.0f} "
                  f"spread={spread:.2f}%")
        print()
    if der["trace_points"]:
        print(f"  --- Anytime trace ({der['trace_points']} points) ---")
        print(f"  cost: {der['trace_first_cost']:,.2f} -> {der['trace_last_cost']:,.2f} "
              f"({(der['trace_first_cost']-der['trace_last_cost'])/der['trace_first_cost']*100:.2f}% reduction)")
        print(f"  time: {der['trace_first_ms']}ms .. {der['trace_last_ms']}ms")
        bs = der["trace_buckets"]
        print(f"  cost reduction by quintile (relative to bucket 0):")
        for i, b in enumerate(bs):
            print(f"    bucket {i+1} ({i*20:2d}%-{(i+1)*20:3d}% of trace time): {b:.3f}%")
        print(f"  longest gap between best updates: {der['max_gap_ms']}ms "
              f"({der['max_gap_frac']*100:.1f}% of ALNS phase)")
        print(f"  plateau tail (phase5_end - last_best): {der['plateau_tail_ms']}ms")
    print()
    print(f"  --- Top destroy ops by call count ---")
    for op, calls, accepts, ar in der["destroy_ops"][:5]:
        print(f"    {op:<32s} calls={calls:6d}  accepts={accepts:6d}  rate={ar*100:5.2f}%")
    print(f"  --- Top repair ops by call count ---")
    for op, calls, accepts, ar in der["repair_ops"][:5]:
        print(f"    {op:<32s} calls={calls:6d}  accepts={accepts:6d}  rate={ar*100:5.2f}%")


def diff_summary(metas: list[dict], deriveds: list[dict]) -> None:
    """If exactly 2 runs, print a scenario-selection-oriented diff."""
    if len(metas) != 2:
        return
    a, b = metas
    da, db = deriveds
    print("\n" + "=" * 80)
    print("SCENARIO SELECTION DIFF (run A vs run B)")
    print("=" * 80)
    print(f"  A: n={a.get('node_count')}  iters/sec={da['iters_per_sec']:>10,.0f}  "
          f"reheats={a.get('sa_reheats', 0)}  pt_replicas={da['pt_replicas']}")
    print(f"  B: n={b.get('node_count')}  iters/sec={db['iters_per_sec']:>10,.0f}  "
          f"reheats={b.get('sa_reheats', 0)}  pt_replicas={db['pt_replicas']}")

    # n-normalized iters/sec: how does throughput scale per node?
    # Compute-bound prediction: iters/sec * n^k roughly constant
    # (k=1.0 if O(n) per iter, k=0 if O(1) per iter).
    na = get_int(a, "node_count", 1)
    nb = get_int(b, "node_count", 1)
    iA = da["iters_per_sec"]
    iB = db["iters_per_sec"]
    if iA > 0 and iB > 0 and na > 0 and nb > 0:
        # Throughput ratio vs naive O(n) expectation.
        expected_ratio = na / nb  # iters_B / iters_A under O(n) per iter
        actual_ratio = iB / iA
        slowdown_factor = expected_ratio / actual_ratio if actual_ratio > 0 else float("inf")
        print(f"\n  Throughput ratio analysis (B/A):")
        print(f"    n ratio (na/nb) = {na/nb:.3f}  (so expected iters/sec ratio if O(n) per iter)")
        print(f"    actual iters/sec ratio (B/A) = {actual_ratio:.3f}")
        print(f"    super-linear slowdown factor = {slowdown_factor:.2f}x "
              f"({'SUPER-linear' if slowdown_factor > 1.3 else 'roughly linear'})")
        if slowdown_factor > 1.5:
            print(f"    >>> Suggests Scenario A: compute scaling worse than O(n) per iter.")

    # Plateau check (for the larger-n run).
    larger = b if nb >= na else a
    larger_d = db if nb >= na else da
    if larger_d.get("trace_points", 0) > 0:
        late_progress = larger_d["trace_buckets"][-1] - larger_d["trace_buckets"][-2]
        print(f"\n  Late-phase progress on n={get_int(larger, 'node_count')} run:")
        print(f"    last quintile cost reduction (relative to first): {larger_d['trace_buckets'][-1]:.3f}%")
        print(f"    last quintile vs prev quintile delta: {late_progress:+.3f}%")
        if larger_d["max_gap_frac"] > 0.4:
            print(f"    >>> Suggests Scenario B: longest no-improve gap is {larger_d['max_gap_frac']*100:.0f}% of ALNS phase (plateau).")

    # PT mixing check.
    pt_runs = [m for m, d in zip(metas, deriveds) if d["pt_replicas"] > 1]
    if pt_runs:
        for m, d in zip(metas, deriveds):
            if d["pt_replicas"] > 1 and d["pt_swap_accept"] is not None:
                acc = d["pt_swap_accept"] * 100
                print(f"\n  PT mixing on n={m.get('node_count')}: swap acceptance = {acc:.1f}%")
                if acc < 18:
                    print(f"    >>> Suggests Scenario C: PT swap acceptance below 25-35% target.")
                elif acc > 50:
                    print(f"    >>> PT may be over-mixing (replicas too close in T).")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", help="Run JSON files (from run_audit.py).")
    args = parser.parse_args()

    metas = []
    deriveds = []
    for p_str in args.paths:
        p = Path(p_str)
        if not p.exists():
            print(f"[error] missing: {p}", file=sys.stderr)
            return 2
        m = load_meta(p)
        d = derive_stats(m)
        metas.append(m)
        deriveds.append(d)

    for i, (m, d) in enumerate(zip(metas, deriveds)):
        label = f"Run {i+1}: n={m.get('node_count')} m={m.get('salesman_count')}"
        print_run(label, m, d)

    if len(metas) == 2:
        diff_summary(metas, deriveds)
    return 0


if __name__ == "__main__":
    sys.exit(main())
