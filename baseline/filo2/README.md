# FILO2 CVRP Adapter Baseline

This baseline uses FILO2 as an external scalable CVRP solver and adapts MTSP
instances to CVRP instances.

FILO2 is not a native MTSP solver. It is used as a scalable VRP-family
baseline for large instances.

Repository: https://github.com/acco93/filo2

## Method

The adapter runs the following pipeline:

```text
MTSP instance
  -> convert to CVRP
  -> run external FILO2
  -> parse FILO2 solution
  -> convert routes back to MTSP indexing
  -> evaluate with MTSP metrics
```

For an MTSP instance with `n` vertices and `m` salesmen:

- the depot remains node 1 in the generated CVRP file;
- depot demand is 0;
- every customer demand is 1;
- CVRP capacity is `ceil((n - 1) / m)`.

## Limitation

FILO2 optimizes CVRP total route cost under capacity constraints. It does not
directly optimize the MTSP objective and does not strictly enforce exactly `m`
routes.

The runner therefore stores both raw route metrics and postprocessed MTSP
metrics. By default, postprocessing tries to return exactly `m` routes:

- if FILO2 returns fewer than `m` routes, the longest routes are split;
- if FILO2 returns more than `m` routes, the cheapest route pairs are merged.

This makes comparisons easier, but the result should still be reported as
`FILO2_CVRP_adapter`, not as a native MTSP solver.

## Install

From the repository root:

```bash
bash baseline/filo2/install_filo2.sh
bash baseline/filo2/build_filo2.sh
```

The installer uses sparse checkout and places FILO2 under `external/filo2`.
The external solver source is intentionally not vendored into this repository.

## Run

Example:

```bash
python3 baseline/filo2/run_filo2_baseline.py \
  --input data/mtsp/generated_multifamily/uniform_n100000_m5_r01.txt \
  --time-limit 300 \
  --seed 42
```

The salesman count is read from the MTSP instance by default. You can override
it with `--m`.

Results are written to `data/results/baselines/filo2` by default.

## Output Metrics

The result JSON contains:

- `algorithm`;
- `source_instance`;
- `converted_instance`;
- `m_requested`;
- `routes_returned_raw`;
- `routes_returned`;
- `total_distance`;
- `max_route_distance`;
- `avg_route_distance`;
- `imbalance`;
- `valid_cover`;
- `exact_m`;
- `elapsed_seconds`;
- `return_code`;
- tail snippets of FILO2 stdout and stderr.

