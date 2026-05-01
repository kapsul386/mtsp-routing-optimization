# HGS-CVRP baseline

Vidal's Hybrid Genetic Search для CVRP — современный academic-quality
solver. Repo: https://github.com/vidalt/HGS-CVRP

## Установка и сборка

```bash
bash baseline/hgs_cvrp/install_hgs.sh   # git clone в external/HGS-CVRP
bash baseline/hgs_cvrp/build_hgs.sh     # cmake build в external/HGS-CVRP/build/hgs
```

Требования: cmake ≥ 3.10, g++ с C++17. На Linux (или WSL Ubuntu) работает
из коробки. На Windows можно собрать через MSVC.

Если binary не собран, runner вернёт `status: "binary_not_built"` с
hint'ом про install/build скрипты.

## CVRP-постановка для mTSP

Конверсия mTSP → CVRP файл (`.vrp` TSPLIB-формат) через переиспользуемый
`baseline/filo2withoutcode/mtsp_to_cvrp.py`:
- demand=1 для всех customers
- capacity = ceil((n-1)/m)
- vehicles = m
- DEPOT_SECTION = node 1

HGS-CVRP CLI:
```
hgs <instance.vrp> <output.sol> -seed N -t S -veh m
```

Парсинг .sol: формат HGS — `Route #k: c1 c2 c3 ...` для каждого маршрута,
плюс строка `Cost <total>` в конце. Customers индексы 1-based в файле,
конвертируются в 0-based depot-wrapped в нашем JSON.

После решения routes пост-обрабатываются до ровно m через
`postprocess_to_m_routes`.

## Использование

```bash
python baseline/hgs_cvrp/run_hgs_baseline.py \
  --input data/mtsp/generated_multifamily/uniform_n1000_m5_r01.txt \
  --m 5 --time-limit 60 --seed 1 \
  --output-dir data/results/baselines/hgs_cvrp/smoke
```

Дополнительные параметры HGS-CVRP можно прокинуть через `--extra-arg`:
```bash
python ... --extra-arg=-it --extra-arg=20000
```

## Scalability ceiling

Authors HGS-CVRP заявляют рассчёт на medium-scale CVRP до ~1000 клиентов.
На n=2000 ещё работает, на n=5000+ становится непригодным. Тестировать
**up to n=2000**, ceiling подтвердить sweep'ом.

Это сильнейший academic baseline для small-medium VRP — нужен чтобы
показать как v21 ведёт себя в этом диапазоне.

## Output

См. формат в `baseline/ortools/README.md`.
