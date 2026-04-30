# Day 1 morning — scenario decision

**Date:** 2026-04-29
**Decision:** **Scenario A** (compute-bound: delta-eval fix in `10_inter_route_moves.hpp`).

## Evidence

Two seed=1 runs through `experiments/run_audit.py` (single-seed, scenario-selection mode):

| Metric                   | n=10k 60s        | n=100k 380s       |
|--------------------------|-----------------:|------------------:|
| ALNS phase ms            |           29,402 |           242,068 |
| iters                    |              761 |               293 |
| iters/sec                |             25.9 |               1.2 |
| accept rate              |            0.834 |             0.768 |
| best/iter                |           58.74% |            55.29% |
| sa_reheats               |                0 |                 0 |
| pt_replicas (active)     |                1 |                 1 |
| last quintile cost-Δ     |          +0.422% |          +0.493% |
| longest no-improve gap   |   838ms (2.9% φ) |   23,726ms (9.8% φ) |

Run JSONs:
- `data/results/audit/_smoke_n10k/runs/uniform_n10000_m5_r01__seed001.json`
- `data/results/audit/profile_n100k/runs/uniform_n100000_m5_r01__seed001.json`

## Why Scenario A (and not B/C)

**Scenario A — compute-bound, super-linear scaling:** confirmed.
- Throughput drop: n×10 → iters/sec ×0.047 = **21.6× drop**.
- Expected if per-iter cost is O(n): ×10 drop.
- Super-linear slowdown factor: **2.14×**.
- Direct match with audit finding: `RecomputeLength()` calls in
  `src/v21/core/10_inter_route_moves.hpp:136-137, 224, 227` walk the entire
  route after the delta has already been computed. Each route is O(L) ≈ O(n/m),
  so per-iter overhead is ~O(n/m) above the O(1) delta. Exactly the shape
  observed: ~×2 over linear in n.

**Scenario C — PT mixing:** ruled out for n=100k.
- AutoTune disables PT for the n>60k tier (`pt_replicas=1` in metadata).
- The audit's earlier claim ("4–8 replicas for larger n") was based on the
  generic ladder, but AutoTune's instance-size dispatcher overrides that for
  the largest tier — instrumenting PT on n=100k would be wasted effort.

**Scenario B — algorithm-bound plateau:** secondary at best.
- Both runs show `sa_reheats=0`. On n=10k that's correct (best/iter=58.7%, no
  stagnation). On n=100k it could matter, BUT the trace shows progress in the
  final quintile (+0.493% delta in last 20% of trace time) — n=100k is **time-
  bound, not plateau-bound**. The longest no-improve gap is 23.7s = 9.8% of
  ALNS phase, which is just slow iters, not stagnation. After Scenario A
  raises throughput, B may start mattering — but as a Day-2 target it's
  premature.

## Implication for Day 2

Stick with the plan: **Scenario A only**. Concrete file/lines:

- `src/v21/core/10_inter_route_moves.hpp:136-137` (relocate, swap)
- `src/v21/core/10_inter_route_moves.hpp:224, 227` (or-opt)

Replace `rl.RecomputeLength(r, d)` with `route_length_[r] += dL` using the
delta already computed during evaluation. Keep a debug invariant under
`#ifdef DEBUG_INVARIANTS` to occasionally verify that incremental tracking
matches a recomputed length.

**Expected gain (from refined plan):** −1.5%…−2.5% on n=100k, +0.3–0.5% on
n=50k. Mechanism: doubling throughput from 1.2 iters/sec to ~2.5 → ~600 iters
in same wall-time → ~330 best-updates instead of 162 → 1.5–2× more cost
reduction.

## What was NOT done (and why)

- **WPR/ETW sampling profile.** Not needed: metadata + super-linear factor
  + audit-pinpointed bug location together give a unique scenario without
  function-level sampling. If Scenario A delta-fix doesn't yield the expected
  improvement on Day 3 re-bench, *then* run WPR to find the next bottleneck.
- **Variance audit (10 seeds).** This is the day-1-afternoon block, not the
  morning. Will run via the same harness with `--seeds 10`. Wall ≈ 1.7h
  sequential.
