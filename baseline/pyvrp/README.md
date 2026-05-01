# PyVRP baseline

Python HGS (Hybrid Genetic Search) wrapper. Один из наиболее
производительных open-source CVRP-солверов на Python.

## Установка

```bash
pip install pyvrp
```

~30 МБ. Если не установлено — runner вернёт `status: "deps_missing"`.

## CVRP-постановка для mTSP

- `num_vehicles = m` (homogeneous, single vehicle type)
- `start_depot = end_depot = node 0`
- `delivery = 1` для каждого customer
- `capacity = ceil((n-1)/m)`
- Координаты scaled × 1000 (PyVRP требует int coords для distance
  matrix; precision ~3 знака)
- Stop по времени (`MaxRuntime`) или по итерациям (`MaxIterations`)

После решения routes пост-обрабатываются до ровно `m` маршрутов через
общий `postprocess_to_m_routes`.

## Использование

```bash
python baseline/pyvrp/run_pyvrp_baseline.py \
  --input data/mtsp/generated_multifamily/uniform_n1000_m5_r01.txt \
  --m 5 --time-limit 60 --seed 1 \
  --output-dir data/results/baselines/pyvrp/smoke
```

Также можно запускать по итерациям:
```bash
... --iters 10000 ...
```

## Scalability ceiling

PyVRP — academic-quality HGS, рассчитан на medium-scale CVRP.
Прикидки (по опыту авторов и логам):
- n ≤ 1000: very fast, минуты
- n ≤ 5000: feasible, hours
- n ≤ 10000: borderline
- n ≥ 25000: not recommended

Тестировать **up to n=10000**, ceiling подтвердить sweep'ом.

## Output

См. формат в `baseline/ortools/README.md` — идентичный JSON для
агрегатора `compare_baselines.py`.
