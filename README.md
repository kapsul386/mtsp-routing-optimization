# mtsp-routing-optimization

Учебно-исследовательский проект по задаче множественного коммивояжера (`mTSP`). Репозиторий собирает в одном месте кодовую базу, материалы по контрольным точкам и экспериментальный контур для сравнения эвристических алгоритмов маршрутизации.

## Цель

Исследовать, какие эвристические подходы дают лучшее качество решения `mTSP` по критерию `MINSUM` при фиксированном ограничении на время вычислений.

## Рабочая гипотеза

При ограничении времени до 60 секунд более сильные эвристики, сочетающие построение начального решения и локальное улучшение, будут давать лучшее значение `MINSUM`, чем простые базовые стратегии вроде `random + nearest neighbor` и `greedy + 2-opt`. Ожидается, что это преимущество сохранится при росте размерности задачи и числа коммивояжеров.

## Что уже есть

- `src/algorithms/tsp/` содержит базовые TSP-эвристики и перенесенный общий каркас солверов.
- `src/algorithms/mtsp/` содержит ранние прототипы `mTSP`-алгоритмов; актуальные рабочие реализации лежат в корне `src/` и запускаются через общий runner.
- `docs/KT/` хранит описание проекта и материалы контрольной точки.
- `docs/appendices/` содержит оформленные приложения к курсовой работе.
- `experiments/` предназначен для конфигураций запусков и журнала результатов.
- `data/` зарезервирован под TSPLIB-инстансы, синтетику и итоговые результаты.

## Ближайший план

1. Унифицировать формат входа и выхода для `mTSP`-алгоритмов.
2. Подготовить набор инстансов для сравнения baseline-методов.
3. Добавить воспроизводимый сценарий экспериментов.
4. Довести исследование до связки `synthetic benchmark -> reference baseline -> математическое приложение к курсовой`.

## Сборка

```bash
cmake -S . -B build
cmake --build build
```

## Запуск TSP-каркаса

Сейчас в репозиторий перенесен базовый runner из старого `tsp-cpp`, который полезен как архитектурная основа и для сравнения простых TSP-эвристик:

```bash
python run.py --task path/to/task.txt --coords path/to/coords.npz --step nearest --start 0
```

Следующий этап развития репозитория: адаптировать этот каркас под единый запуск `mTSP`-эвристик и сравнительные эксперименты по курсовому проекту.

## Запуск mTSP-каркаса

Для `mTSP` теперь есть отдельный runner с единым JSON-входом и общими baseline-солверами:

```bash
python run_mtsp.py --input path/to/instance.txt --step rand+nn
python run_mtsp.py --input path/to/instance.txt --step 2opt+greed
python run_mtsp.py --input path/to/instance.txt --step grasp --iters 50 --rcl 3
python run_mtsp.py --input path/to/instance.txt --step lkh-wrapper-v2 --rounds 24 --seed 42 --candidate-count 12 --lookahead-weight 0.35 --depot-weight 0.12
```

Формат входного файла:

```text
n m
x0 y0
x1 y1
...
```

Где вершина `0` считается депо.

## Экспериментальный пайплайн

Для первого воспроизводимого контура добавлены:

- [experiments/generate_mtsp_instances.py](C:/Users/ddkup/курчас/mtsp-routing-optimization/experiments/generate_mtsp_instances.py) для генерации синтетических инстансов;
- [experiments/run_benchmarks.py](C:/Users/ddkup/курчас/mtsp-routing-optimization/experiments/run_benchmarks.py) для пакетного прогона солверов;
- [experiments/config.json](C:/Users/ddkup/курчас/mtsp-routing-optimization/experiments/config.json) с машинно-читаемой конфигурацией эксперимента.

Текущий основной `mTSP`-benchmark сравнивает `rand+nn`, `2opt+greed`, `grasp` и `lkh-wrapper-v2`.

Базовый запуск:

```bash
python experiments/generate_mtsp_instances.py
python experiments/run_benchmarks.py
```

После запуска появляются:

- [data/results/mtsp_results.csv](C:/Users/ddkup/курчас/mtsp-routing-optimization/data/results/mtsp_results.csv) со всеми сырыми прогонами;
- [data/results/mtsp_summary.csv](C:/Users/ddkup/курчас/mtsp-routing-optimization/data/results/mtsp_summary.csv) со средними значениями по каждому набору параметров.

Отдельное математическое приложение под реализованные алгоритмы лежит в файле
[docs/appendices/mtsp_math_appendix.tex](C:/Users/ddkup/курчас/mtsp-routing-optimization/docs/appendices/mtsp_math_appendix.tex).

## Экспериментальный пайплайн TSP

Для TSP добавлен отдельный контур:

- [experiments/tsp_config.json](C:/Users/ddkup/курчас/mtsp-routing-optimization/experiments/tsp_config.json)
- [experiments/generate_tsp_benchmarks.py](C:/Users/ddkup/курчас/mtsp-routing-optimization/experiments/generate_tsp_benchmarks.py)
- [experiments/run_tsp_benchmarks.py](C:/Users/ddkup/курчас/mtsp-routing-optimization/experiments/run_tsp_benchmarks.py)
- [experiments/build_tsp_report.py](C:/Users/ddkup/курчас/mtsp-routing-optimization/experiments/build_tsp_report.py)

Он сравнивает `nearest`, `vns`, `genetic`, `memetic` и `lkh` на синтетических TSP-инстансах и сохраняет:

- [data/results/tsp_results.csv](C:/Users/ddkup/курчас/mtsp-routing-optimization/data/results/tsp_results.csv)
- [data/results/tsp_summary.csv](C:/Users/ddkup/курчас/mtsp-routing-optimization/data/results/tsp_summary.csv)
- [data/results/tsp_report.md](C:/Users/ddkup/курчас/mtsp-routing-optimization/data/results/tsp_report.md)
