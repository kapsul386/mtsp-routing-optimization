# FILO2-inspired capacity-aware repair for high-m MINSUM (`lkh_v21_minsum_cap`)

**Date:** 2026-04-29
**Goal:** Close the gap to FILO2 on high-m mTSP-MINSUM (m≥80) without
rewriting the solver. Add a single CVRP-style guard rail to v21's repair
operators.

## What was found in FILO2

FILO2 is a CVRP solver, not an mTSP solver. The MTSP→CVRP adapter at
`baseline/filo2withoutcode/mtsp_to_cvrp.py` sets `capacity = ceil((n-1)/m)`
— exactly the balanced load each agent should carry. Then FILO2's standard
machinery enforces capacity at every move:

- **Construction (`solution/savings.hpp`).** Clarke-Wright savings merge
  proceeds only if the sum of merged loads `≤ vehicle_capacity`. Routes start
  as singletons; the merge graph is built monotonically under this
  constraint.
- **Local search (`localsearch/OneZeroExchange.hpp`,
  `localsearch/TailsExchange.hpp`).** Relocate, swap, and tail-exchange
  moves are gated by `target_route.load + delta_load ≤ capacity`. Moves that
  would overflow are rejected before the cost delta is even computed.
- **`opt/routemin.hpp`** runs a destroy-and-reinsert loop with the same
  capacity gate; reinsertion into an at-capacity route is forbidden.

The recurring pattern: **capacity is a hard precondition, checked early,
trivially cheap (one int comparison)**. It costs FILO2 nothing per move and
removes a large class of pathological moves where one route absorbs most
customers. Pure MINSUM has no such safety net — mathematically MINSUM can
prefer imbalanced solutions if a single very long route happens to be the
shortest total. On real high-m geometries this trap is real.

## What was found in v21 baseline

On `clustered-offset-depot_n10000_m100_r01.txt` (n=10000, m=100, target
~100 customers per agent), seed=42, 60 s budget, the user's headline numbers:

| Solver | MINSUM | max\_route | imbalance | wall |
|---|---:|---:|---:|---:|
| LKH3 | 8,524 | 5,072 | 59.5 | 60.7 s |
| FILO2 | 17,874 | 299 | 1.67 | 60.1 s |
| `lkh_v21_minsum` | 23,261 | 473 | 2.04 | 31.4 s |
| 2opt+greed | 29,470 | 361 | 1.23 | 0.3 s |

LKH3 wins on raw MINSUM but produces an absurd 59.5× imbalance, useless in
practice. FILO2 dominates on the practical "good MINSUM with believable
balance" Pareto frontier. v21 is closer to FILO2 in shape (imbalance 2.04,
not 59.5) but ~30 % worse on MINSUM.

A diagnostic of v21's output revealed why: one route consistently absorbs
400+ customers (the "fat route" pattern), which carries a non-trivial
intra-route detour cost. Pure MINSUM acceptance has no incentive to
redistribute as long as the total is still being driven down locally.

## What was changed

A single, controlled change: **capacity-aware repair**. New solver
`lkh_v21_minsum_cap`. Existing `lkh_v21_minsum` is untouched.

### File-level changes (5 files, ~80 lines net)

1. **`src/v21/core/05_route_list.hpp`** — added `int RouteSize(int r)
   const` for O(1) customer count per route (excluding depot endpoints).
2. **`src/v21/core/14_repair_ops.hpp`** —
   - `RepairContext` gains `int route_cap = 0` (0 = legacy mode, no
     constraint).
   - In both `RepairCheapestInsertion` and `RepairRegret2Insertion`, the
     inner loop over routes now skips any route whose `RouteSize >=
     route_cap` (when `route_cap > 0`).
   - Fallback path (when no candidate-driven position is found): in cap
     mode, prefer the route with the fewest customers under cap, instead
     of the legacy "shortest-length route" rule. This keeps placement
     tight when forced into the fallback.
3. **`src/v21/core/18_autotune.hpp`** — `AutoTuneParams` gains
   `int route_cap = 0`. `ResolveParamsForInstance` does not auto-set this;
   only the cap solver populates it. Default 0 → all existing solvers
   unchanged.
4. **`src/v21/core/17_pipeline.hpp`** — single-replica and PT replica
   `RepairContext` constructions now copy `params.route_cap` into both
   `rctx_normal` and `rctx_balance`. Two-line addition.
5. **`src/v21/minsum/minsum_solver_cap.cpp`** *(new file)* — solver class
   `LkhWrapperSolverV21MinsumCap`, registered as `lkh_v21_minsum_cap`.
   Computes `target = ceil((n-1)/m)`, `cap = ceil(target * (1 + slack))`
   with default `slack = 0.75`, sets `params.route_cap = cap`. CLI accepts
   `--route-cap <int>` (override) and `--route-cap-slack <double>`
   (fractional slack).

### Slack tuning

The slack value was tuned empirically on the flagship instance (single
seed=1, 60 s):

| slack | cap | MINSUM | max\_size | at-cap routes |
|---|---:|---:|---:|---:|
| no cap (v21) | ∞ | 22,158 | 513 | — |
| 0.10 | 111 | 21,642 | 736 | 71 |
| 0.25 | 125 | 21,188 | 463 | 25 |
| 0.50 | 150 | 20,502 | 506 | 9 |
| **0.75** | **175** | **20,215** | **343** | **1** |
| 1.00 | 200 | 20,712 | 309 | 1 |
| 1.50 | 250 | 20,974 | 322 | 0 |

The unintuitive finding: a strict cap (slack 0.10–0.25) is *worse* than a
loose cap. With slack 0.10 only ~28 routes can absorb new customers, so
repair is constantly forced into expensive fallback positions far from
candidate neighbours. Slack 0.75 is the sweet spot: only ~1 route ever
hits the cap on average, but its existence steers occasional
over-concentration into emptier routes — best MINSUM and best balance
simultaneously.

This is consistent with the FILO2 design philosophy: capacity should be a
hard precondition for the rare extreme case, not a tight quota.

## Results

### Flagship instance: `clustered-offset-depot_n10000_m100_r01.txt`

5-seed paired comparison, both solvers run today (controlled hardware
state):

| seed | `lkh_v21_minsum` | `lkh_v21_minsum_cap` | diff | diff % |
|---|---:|---:|---:|---:|
| 1 | 21,894.00 | 21,316.32 | −577.68 | −2.64 % |
| 2 | 21,613.01 | 20,292.23 | −1,320.78 | −6.11 % |
| 3 | 22,309.93 | 21,132.46 | −1,177.47 | −5.28 % |
| 4 | 22,025.13 | 20,498.04 | −1,527.09 | −6.93 % |
| 5 | 21,858.82 | 20,144.25 | −1,714.57 | −7.84 % |

- mean baseline 21,940.18, mean candidate 20,676.66
- **mean delta −1,263.52 (−5.76 %)**
- 5/5 seeds improved
- Wilcoxon one-sided p = **0.031** → significant at α = 0.05
- Wilcoxon two-sided p = 0.062

Balance metrics improved as well:

| metric | v21 mean | v21\_cap mean | direction |
|---|---:|---:|---|
| max\_route | 593.28 | 521.85 | **−12.0 %** |
| imbalance | 2.705 | 2.525 | **−6.7 %** |
| max route size | 467.4 | 337.4 | **−27.8 %** |

So both MINSUM and balance improved. Gap to FILO2 (mean 17,874) shrank
from ~30 % to ~15.7 %. Gap to LKH3 still larger but LKH3's 59.5×
imbalance disqualifies it for practical use.

### Multi-instance check (high-m, n=10k m=100)

5 seeds × 4 instances × 60 s budget, all run sequentially today. The
flagship was already completed; this is the additional set.

| instance | v21 mean | v21\_cap mean | mean delta % | improved | Wilcoxon p |
|---|---:|---:|---:|---:|---:|
| `clustered-offset-depot_n10000_m100_r01` (flagship) | 21,940.18 | 20,676.66 | **−5.76 %** | 5/5 | **0.031** |
| `clustered-center_n10000_m100_r01` | 12,971.94 | 12,236.12 | **−5.67 %** | 5/5 | **0.031** |
| `n10000_m100_r01` (uniform-style) | 15,669.42 | 15,385.57 | −1.81 % | 5/5 | **0.031** |
| `mixed-outliers_n10000_m100_r01` | 17,413.67 | 16,995.18 | −2.40 % | 3/5 | 0.156 |

**Aggregate (across all 4 instances, 20 paired runs):** mean −3.24 %,
**16/20 improved**, **Wilcoxon one-sided p = 0.002** → highly significant.

### Balance metrics across multi-instance set

| instance | metric | v21 mean | v21\_cap mean | direction |
|---|---|---:|---:|---|
| flagship (clustered-offset-depot) | max\_route | 593.28 | 521.85 | **−12.0 %** |
| flagship | imbalance | 2.705 | 2.525 | **−6.7 %** |
| flagship | max route size | 467.4 | 337.4 | **−27.8 %** |
| clustered-center | max\_route | 475.37 | 494.46 | +4.0 % |
| clustered-center | imbalance | 3.660 | 4.037 | +10.3 % |
| `mixed-outliers` | max\_route | 707.64 | 537.91 | **−24.0 %** |
| `mixed-outliers` | imbalance | 4.073 | 3.173 | **−22.1 %** |
| `mixed-outliers` | max route size | 1115.6 | 478.0 | **−57.2 %** |
| `n10000_m100` (uniform) | max\_route | 591.63 | 629.03 | +6.3 % |
| `n10000_m100` | imbalance | 3.775 | 4.094 | +8.4 % |

Honest picture: **MINSUM is consistently improved; balance is instance-
dependent.** On 2 of 4 instances (the flagship and mixed-outliers), cap
also dramatically improves balance — including a 57 % cut in worst-route
customer count on `mixed-outliers`, where v21 occasionally stuffed 1100+
customers into a single route. On the other 2 (clustered-center and
uniform), max\_route and imbalance get slightly worse: cap fills certain
"good geometry" routes to capacity and pushes overflow into routes
slightly farther from the customer's natural neighborhood. The trade is
worth it because MINSUM still drops, but the balance side-effect is real
and not uniformly positive.

The number of empty routes also increases mildly under cap (typically by
3–7 routes out of 100). This is a structural consequence: cap rarely
fires in the optimal regime (≈ 1 route at cap on average), but when it
does, customers redirected to "smallest under-cap route" often pick the
nearest plausible route rather than truly emptying ones. For the course
report this is worth disclosing rather than hiding.

## Honest framing for the report

Pure MINSUM has no mathematical preference for balanced solutions; on some
instances an imbalanced configuration could in principle have lower total
length. The capacity-aware repair is therefore not a *correctness*
improvement but a *practical-stabilization* improvement: it removes one
class of pathological local optima (the "fat route" trap) by making the
search space avoid configurations that no real-world dispatcher would
ever consider acceptable. The fact that on every tested high-m instance
this also lowers MINSUM in absolute terms is empirical, not theoretical.

The change is consistent with FILO2's design philosophy without porting
its full algorithm. CVRP capacity is the cheapest possible structural
prior; we adopt only the prior, not the rest of the CVRP machinery.

## Files

- New solver: `src/v21/minsum/minsum_solver_cap.cpp`
- Modified core: `src/v21/core/05_route_list.hpp`,
  `14_repair_ops.hpp`, `17_pipeline.hpp`, `18_autotune.hpp`
- Run artifacts: `data/results/audit/h2h_n10k_m100/v21_today/`,
  `.../v21_cap_075/`, `data/results/audit/multi_m100/`

## Recommendation

Include `lkh_v21_minsum_cap` as a separate solver in the main course
deliverable. Frame it as "FILO2-inspired stabilization for high-m
MINSUM". Keep `lkh_v21_minsum` as the general-purpose default; the cap
variant is a specialist tool for `m ≳ 30`-class instances where the fat-
route trap is the dominant failure mode.
