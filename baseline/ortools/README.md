# OR-Tools Routing Solver baseline

Google OR-Tools VRP-based baseline для mTSP. 5 поисковых стратегий
выбираются через `--strategy`:

| Strategy | First-solution | Local-search metaheuristic |
|---|---|---|
| `GLS` (default) | PATH_CHEAPEST_ARC | GUIDED_LOCAL_SEARCH |
| `PARALLEL_GLS` | PARALLEL_CHEAPEST_INSERTION | GUIDED_LOCAL_SEARCH |
| `SAVINGS_GLS` | SAVINGS | GUIDED_LOCAL_SEARCH |
| `TABU` | PATH_CHEAPEST_ARC | TABU_SEARCH |
| `SA` | PATH_CHEAPEST_ARC | SIMULATED_ANNEALING |

## Установка

```bash
pip install ortools
```

~50 МБ. Лучше в отдельный venv. Если пакет не установлен, runner
вернёт `status: "deps_missing"` с инструкцией.

## CVRP-постановка для mTSP

OR-Tools решает VRP — нам нужно зафиксировать `m` маршрутов и
заставить минимизировать total distance:
- `num_vehicles = m`
- `depot = node 0`
- `demand_i = 1` для всех customers
- `capacity = ceil((n-1)/m)` единая для всех агентов
- `cost = floor(euclidean × 1000)` (OR-Tools требует int costs;
  scaling × 1000 даёт ~3 знака после запятой)

После решения routes пост-обрабатываются до ровно `m` маршрутов
через общий `parse_filo2_solution.postprocess_to_m_routes`.

## Использование

Smoke на small инстансе:
```bash
python baseline/ortools/run_ortools_baseline.py \
  --input data/mtsp/generated_multifamily/uniform_n1000_m5_r01.txt \
  --m 5 --time-limit 30 --seed 1 \
  --strategy GLS \
  --output-dir data/results/baselines/ortools/smoke
```

Для всех 5 стратегий:
```bash
for STRAT in GLS PARALLEL_GLS SAVINGS_GLS TABU SA; do
  python baseline/ortools/run_ortools_baseline.py \
    --input data/mtsp/generated_multifamily/uniform_n1000_m5_r01.txt \
    --m 5 --time-limit 60 --seed 1 \
    --strategy $STRAT \
    --output-dir data/results/baselines/ortools/all
done
```

## Scalability ceiling (ожидаемая)

OR-Tools хранит full distance matrix O(n²). Прикидки:
- n=1000: ~8 МБ матрица, OK
- n=5000: ~200 МБ, OK
- n=10000: ~800 МБ, borderline
- n=25000+: ~5 ГБ, скорее всего OOM или слишком медленно

Тестировать `up to n=10000`. Ceiling уточнить scalability sweep'ом.

## Output

JSON в `<output-dir>/<instance>_m<m>_seed<N>_<strategy>.json`:
```json
{
  "algorithm": "OR-Tools_GLS",
  "status": "ok",
  "elapsed_seconds": 28.4,
  "metrics": {
    "routes_count": 5,
    "exact_m": true,
    "valid_cover": true,
    "total_distance": 14123.5,
    "max_route_distance": 3210.1,
    "imbalance": 1.14,
    ...
  },
  "raw_metrics": { /* до postprocess */ },
  "postprocess_actions": ["..."]
}
```
Совместим с FILO2 / другими baseline JSON'ами для агрегатора
`compare_baselines.py`.
