# VeRyPy classical CVRP heuristics

5 классических CVRP-эвристик через единый Python adapter. Это «слабые»
baseline'ы из 1960-90х — нужны как нижняя планка narrative (показать
что v21 побеждает классику).

| Heuristic | VeRyPy module | Описание |
|---|---|---|
| `Sweep` | `classic_heuristics.sweep` | Polar sweep вокруг депо |
| `Savings` (default) | `classic_heuristics.parallel_savings` | Clarke-Wright parallel savings |
| `RFCS` | `classic_heuristics.gillet_miller_sweep` | Route-First-Cluster-Second (Gillett-Miller) |
| `NN` | `classic_heuristics.nearest_neighbor` | Nearest neighbor insertion |
| `SCI` | `classic_heuristics.cheapest_insertion` | Sequential cheapest insertion |

## Установка

VeRyPy не на PyPI как стабильный пакет. Установка из git:
```bash
pip install git+https://github.com/yorak/VeRyPy.git
```

Альтернативно — clone и `pip install -e .`. Если не установлено,
runner вернёт `status: "deps_missing"`.

## CVRP-постановка для mTSP

- `D` = full Euclidean distance matrix (float, не масштабируется)
- `d_i = 1` для customers, `d_0 = 0` для депо
- `C = ceil((n-1)/m)` — capacity per route

Эвристики **детерминированы** (no seed). Time-limit неприменим — все
запускаются за один проход в полиномиальное время.

После решения — postprocess до ровно m маршрутов.

## Использование

```bash
python baseline/verypy/run_verypy_baseline.py \
  --input data/mtsp/generated_multifamily/uniform_n1000_m5_r01.txt \
  --m 5 --heuristic Savings \
  --output-dir data/results/baselines/verypy/smoke
```

Все 5 эвристик:
```bash
for H in Sweep Savings RFCS NN SCI; do
  python baseline/verypy/run_verypy_baseline.py \
    --input data/mtsp/generated_multifamily/uniform_n1000_m5_r01.txt \
    --m 5 --heuristic $H \
    --output-dir data/results/baselines/verypy/all
done
```

## Scalability ceiling

Большинство эвристик O(n²) или O(n² log n) на построении distance
matrix. Тестировать `up to n=10000`. На n=25k+ matrix-construction
становится bottleneck (~5 ГБ RAM на n=25k full matrix).

## Output

См. формат в `baseline/ortools/README.md`.
