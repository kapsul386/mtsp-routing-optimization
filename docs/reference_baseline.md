# Внешний Reference Baseline Для mTSP

## Зачем он нужен

Внутреннее сравнение между `rand+nn`, `2opt+greed`, `grasp` и `lkh-wrapper`
показывает, какой из наших методов лучше других наших методов.

Но этого недостаточно, если мы хотим понимать, насколько мы далеки от
сильного внешнего решения. Для этого в проект добавлен отдельный
reference baseline на базе OR-Tools.

## Канонический reference CSV

В проекте используется один канонический reference CSV:

- `data/results/mtsp_reference_results.csv`

Все остальные сравнения и отчеты должны строиться только относительно него.

## Что именно сравнивается

- наши solvers продолжают запускаться через основной pipeline;
- внешний baseline считается отдельно и не подменяет наши результаты;
- после этого строится таблица сравнений с метриками:
  - `reference_objective`
  - `objective_gap = our_objective - reference_objective`
  - `relative_gap_percent`

Если `objective_gap` уменьшается от версии к версии, значит мы реально
приближаемся к сильному внешнему ориентиру.

## Какие файлы используются

- основной конфиг: `experiments/reference_config.json`
- конфиг для versioned-сравнений с тем же reference CSV:
  `experiments/lkh_versions_reference_config.json`
- запуск reference baseline: `experiments/run_reference_benchmarks.py`
- построение отчета: `experiments/build_reference_report.py`

## Какие артефакты получаются

- `data/results/mtsp_reference_results.csv` <- канонический reference CSV
- `data/results/mtsp_reference_comparison.csv`
- `data/results/mtsp_reference_summary.csv`
- `data/results/mtsp_reference_report.md`

## Как запускать

```bash
python experiments/run_reference_benchmarks.py
python experiments/build_reference_report.py
```

## Формат сравнения в отчетах

В отчетах основной формат должен быть таким:

- `our | OR-Tools | gap | time`

Это позволяет сразу видеть:

- качество нашего решения;
- уровень внешнего ориентира;
- расстояние до него;
- цену по времени.

## Важное замечание

OR-Tools в этом проекте используется как сильный внешний baseline, а не как
доказанный optimum. Поэтому в тексте курсовой корректнее писать
`external reference solution` или `reference baseline`, а не `точный оптимум`.
