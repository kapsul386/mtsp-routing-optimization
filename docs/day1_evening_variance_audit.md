# Day 1 evening — variance audit results

**Date:** 2026-04-29
**Source:** `data/results/audit/baseline/` (30 runs, 10 seeds × 3 instances).
**Wall-time:** 90.5 min sequential.

## Variance summary

| Instance              |       mean |      std |    cv |       min..max | mean wall |
|-----------------------|-----------:|---------:|------:|---------------:|----------:|
| uniform_n10000_m5     |    410,131 |    2,439 | 0.59% |  407,058 .. 414,023 |     32.6s |
| uniform_n50000_m5     |  4,557,678 |    7,728 | **0.17%** |  4,546,867 .. 4,567,060 |    139.1s |
| uniform_n100000_m5    | 13,009,235 |   35,956 | **0.28%** | 12,967,086 .. 13,062,845 |    370.9s |

## Verdicts

- **n=50k, n=100k: tight variance.** cv 0.17% / 0.28% — Scenario A's expected
  −1.5%…−2.5% on n=100k is 5–9× std → highly detectable via Wilcoxon.
- **n=10k: marginal but workable.** cv 0.59% slightly above the 0.5% plan
  threshold. With paired Wilcoxon on N=10 seeds, even a 0.3% uniform improvement
  is detectable at p<0.05 (because all-10-paired-diffs same sign → p≈0.002).
- **n=10k mean_t = 32.6s out of 60s budget.** Algorithm consistently terminates
  ~halfway through wall-clock budget on this size due to AutoTune's phase split.
  **Implication:** Scenario A's gain on n=10k will be smaller than on n=100k —
  not because the fix doesn't help, but because the budget-cap masks part of the
  speedup.

## Anytime-curve diagnosis (per-seed)

Aggregated from `metadata.anytime_trace` across all 30 runs.

### n=10k 60s — 0/10 seeds plateau-suspect
- iters/sec: 28.2 ± 3.3
- best/iter: 54–60% (consistent)
- last-quintile cost reduction: +0.49% to +0.77% — every seed still improving
  through the final 20% of trace time
- worst longest-gap-frac: 1.9% of phase
- **Verdict:** time-bound, no plateau. Pure throughput regime.

### n=50k 180s — 0/10 seeds plateau-suspect, PT active
- iters/sec: 11.9 ± 2.6 (bimodal: PT replicas finish epochs at slightly
  different speeds depending on system load)
- pt_replicas = 4 (PT is enabled for this tier)
- last-quintile reduction: +0.19% to +0.40%, all positive
- 0/10 plateau-suspect
- **Verdict:** time-bound, healthy.

### n=100k 380s — 7/10 seeds plateau-suspect, PT NOT active
This is where the picture changed compared to the morning seed=1 analysis:
- pt_replicas = 1 (AutoTune disables PT for n>60k tier)
- iters/sec: 0.76 ± 0.20 (large coefficient of variation 26%)
- best/iter: ranges 5.3% to 56.7% across seeds — **massive heterogeneity**
- last-quintile cost reduction:
  - mean +0.070%, threshold for "plateau-suspect" is < 0.1%
  - **7/10 seeds below the threshold**, 3/10 above
  - 4 seeds have exactly +0.000% (last best update was at the quintile boundary)
- **worst longest-gap-frac: 84.5%** (seed 6: 204s of 242s ALNS phase with no
  improvement)
- **sa_reheats = 0 across all 10 seeds** — reheat threshold for n>60k is too
  conservative; the mechanism never fires even when stagnation is severe

| n=100k seed | iters | i/s | best% | red% | q5Δ% | longest-gap | gap% |
|---|---|---|---|---|---|---|---|
| 1  | 196 | 0.81 | 51.0% | 1.79% | +0.238% |  37s | 15.4% |
| 2  | 180 | 0.82 | 30.0% | 1.70% | +0.031% |  99s | 45.5% |
| 3  | 185 | 0.84 | 12.4% | 1.09% | +0.080% | 113s | 51.2% |
| 4  | 181 | 0.81 |  9.9% | 1.31% | +0.000% | 143s | 63.9% |
| 5  | 210 | 0.93 | 34.8% | 1.77% | +0.085% |  64s | 28.5% |
| 6  | 113 | 0.47 |  5.3% | 1.30% | +0.029% | **204s** | **84.5%** |
| 7  | 212 | 0.93 | 34.0% | 1.50% | +0.102% |  47s | 20.5% |
| 8  | 102 | 0.48 | 29.4% | 1.44% | +0.000% |  74s | 34.8% |
| 9  | 105 | 0.53 | 12.4% | 1.06% | +0.000% | 139s | 69.4% |
| 10 | 233 | 0.99 | 56.7% | 1.71% | +0.135% |  14s |  6.0% |

## Updated scenario decision

Morning conclusion was "Scenario A only". Evening data supports a **two-step**
plan instead, since A and B are non-conflicting and target distinct seed
populations:

1. **Scenario A first** (`10_inter_route_moves.hpp:136-137,224,227`).
   Replace `RecomputeLength` with incremental delta updates. Big lever on
   throughput (×2 expected); helps the entire seed distribution.
2. **Mid-day-2 micro-bench** of A: 5 seeds × n=100k 380s. If clearly improving
   → proceed.
3. **Scenario B follow-up** (`18_autotune.hpp` for n>60k tier).
   Halve `reheat_after_no_improvement` so reheat fires 1–3× per ALNS phase
   on the slow-iter regime. Targeted at the 7/10 plateau-suspect seeds.
4. **Final variance audit** (Day 3): full 10-seed bench, paired Wilcoxon vs
   `data/results/audit/baseline/` separately for A+B vs baseline.

This deviates from the original plan's "pick one scenario" rule, but the
evidence justifies it: both A and B are confirmed open by the data, both
fixes are local and independent (different files, different mechanics), and
sequencing them with intermediate benches preserves attribution.

## Files referenced

- Aggregator (one-off, this file's tables): inline Python, see git log.
- Per-run analyzer: `experiments/analyze_run.py`.
- Run JSONs: `data/results/audit/baseline/runs/*.json` (30 files).
- Summary: `data/results/audit/baseline/summary.json`.
