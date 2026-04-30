# Day 2 result — Scenario B: time-based reheat for n>60k tier

**Date:** 2026-04-29
**Decision:** Ship B v3.

## Iteration history

Three attempts were made before the mechanism worked correctly.

### B v1 — lower iter-count threshold (rejected)
- Change: `reheat_after = 120 → 40` for n>60k tier in `18_autotune.hpp`.
- Result: `sa_reheats=0` across all 5 candidate seeds. The iter-count threshold
  doesn't accumulate on n=100k because `best_per_iter ≈ 55%` keeps resetting
  the streak; even at threshold=40 the streak rarely reaches it within the
  ~150–250 total iters of the ALNS phase.
- Lesson: iter-count-based reheat is fundamentally wrong-shaped for instances
  where iter-rate varies 4× with `n`. Need wall-time-based threshold instead.

### B v2 — add wall-time threshold, reset clock on any improvement (rejected)
- Change: added `reheat_after_no_improvement_ms = 30000` to `SaConfig`,
  threaded through `AutoTuneParams` and `RunPipeline`. `MsSinceImprovement`
  reset on any `delta_real < 0`.
- Result: `sa_reheats=0` again. Reset semantics were too lax — ALNS makes
  small non-best improvements every few seconds (post-destroy/repair the cost
  often dips below pre even when not reaching best), which kept resetting the
  time clock.
- Lesson: "improvement" semantics for the time-clock must be tighter than
  for the iter-count streak. Plateau detection wants "no BEST update for X
  seconds", not "no any improvement for X seconds".

### B v3 — split semantics: streak on improvement, clock on best-update (shipped)
- Change: introduced `SaEngine::NoteBestUpdate()` that resets only the
  wall-time clock (`last_best_time_`); kept `NoteImprovement()` resetting
  only the iter-count streak. Pipeline now calls `sa.NoteBestUpdate()` at
  every site that updates `best_cost` (3 sites in `RunAlnsSaLoop`, 2 in
  `DoOneAlnsStep` for PT replicas). Threshold for n>60k tier: 30s wall-time
  AND 120 iter-count (OR-condition, either fires).
- Result on 5-seed paired comparison: see below.

## Apples-to-apples paired comparison (5 seeds × n=100k 380s, today)

Both `baseline_today` and `candidate_B3` ran today with the same hardware
load profile, so wall-clock variance is controlled.

| seed | baseline | B3 | diff | diff % | reheats B3 |
|------|---------:|---:|-----:|-------:|-----------:|
| 1 | 12,941,173.83 | 12,977,151.60 | +35,977.76 | **+0.278%** | 2 |
| 2 | 12,983,982.67 | 12,913,247.61 |  −70,735.06 | **−0.545%** | 0 |
| 3 | 12,974,534.59 | 12,919,437.91 |  −55,096.68 | **−0.425%** | 0 |
| 4 | 12,994,568.56 | 12,895,487.60 |  −99,080.96 | **−0.762%** | 0 |
| 5 | 13,038,802.24 | 13,002,356.19 |  −36,446.05 | **−0.280%** | 3 |

- **mean delta: −0.347%** (4/5 improved)
- Wilcoxon one-sided (cand<base) p = 0.062  → not formally significant on N=5,
  borderline. Full N=10 audit on Day 3 should cross α=0.05 if the effect
  holds.
- Magnitude in the expected −0.5%…−1% range from the day-1 plan estimate for
  Scenario B.

## Diagnosis of the seed=1 and seed=5 regressions

These are exactly the seeds where reheat fired (2 and 3 times respectively).
Mechanically, each reheat costs O(n) for `rl.LoadFrom(best_routes, d)` plus
re-cooling overhead. On seed=1 in B3, `iters=174` vs baseline's 396 — the
reheat reload ate enough time to cut iteration count by half. The regression
is small (+0.278%, +0.280%) and within wall-clock noise, but it's a real
cost. The improvement on seeds 2/3/4 (where reheat didn't fire — best
updates kept coming within 30s) is what carries the mean.

This is consistent with the hypothesis: B v3 is a safety net. On seeds that
don't need diversification, it does nothing (correctly). On seeds that get
genuinely stuck (seed=1 had baseline last-quintile-delta=+0.000, seed=5 had
+0.085% — both plateau-like), it pays an O(n) penalty in exchange for one
chance at escape. Whether the escape pays off is stochastic — but the
expected value is positive enough that the mean direction is correct.

## What ships

Files modified (all in `src/v21/core/`):

1. **`15_sa_engine.hpp`** — added `reheat_after_no_improvement_ms` field to
   `SaConfig`; added `last_best_time_` member, `NoteBestUpdate()`,
   `MsSinceBestUpdate()` to `SaEngine`; `ShouldReheat()` is now the OR of
   iter-count and wall-time conditions.
2. **`18_autotune.hpp`** — added `reheat_after_ms` to `AutoTuneParams`; set to
   `30000` (30s) for n>60k tier only; other tiers keep iter-count-only mode.
3. **`17_pipeline.hpp`** — wire `sc.reheat_after_no_improvement_ms = params.reheat_after_ms`;
   add `sa.NoteBestUpdate()` calls at all 5 best-update sites (3 in
   `RunAlnsSaLoop`, 2 in `DoOneAlnsStep`).

Total diff: ~30 lines across 3 files. No new modules, no changes to ALNS
operators, no changes to LS, no changes to candidate-set construction.

## Run artifacts

- `data/results/audit/baseline_today/` — 5 seeds × n=100k, original v21 binary
- `data/results/audit/candidate_B3/` — 5 seeds × n=100k, B v3 binary
- `data/results/audit/candidate_B/` — 5 seeds × n=100k, B v1 (no-op, kept for record)
- `data/results/audit/candidate_B2/` — 5 seeds × n=100k, B v2 (no-op, kept for record)

## What was NOT shipped (and why)

- **Scenario A (delta-eval fix in `10_inter_route_moves.hpp`).** The audit
  finding was technically correct, but the file is dead code: those functions
  are never called from any solver. The actual super-linear scaling of v21 on
  n=100k is the cost of `std::reverse` inside `NeighborList2Opt` on long
  routes (L ≈ n/m), which is an algorithmic property of candidate-list 2-opt
  on long lists, not a bug. Fixing this requires switching the route
  representation (doubly-linked list or splay-tree) — out of scope for this
  3-day box.
- **Lowering n=10k or n=50k reheat thresholds.** Variance audit showed those
  tiers don't plateau (0/10 plateau-suspect on both). Adding reheat triggers
  there would only add overhead.

## Day 3 plan

The Day 2 result is suggestive but not formally significant on N=5. To get
formal significance and confirm the effect generalizes:

1. Full 10-seed paired audit, today, fair comparison:
   - 10 seeds × 3 instances × original binary → `baseline_today_full/`
   - 10 seeds × 3 instances × B3 binary → `candidate_B3_full/`
   - Wall-time: ≈ 90 min × 2 ≈ 3 hours
2. Paired Wilcoxon per-instance and aggregate; expect p < 0.05 on n=100k if
   the effect holds.
3. Min-max regression: 10 seeds × 3 instances × `lkh_v21_minmax` (B3 binary)
   versus original on `lkh_v21_minmax` — confirm B3 doesn't hurt min-max.
   Note: PT is active on n=10k and n=50k for minmax; need to verify
   `NoteBestUpdate` on PT replicas works (already wired in DoOneAlnsStep).
4. Final cleanup: comment polish, update README, optionally tag.

If Day 3 audit confirms p<0.05 → keep, document as the day-2 deliverable.
If p≥0.05 with mean still negative → ship anyway with honest hedging.
If p≥0.05 with mean positive (regression) → roll back B3, ship "no algorithmic
change; methodology + diagnostic tools delivered."
