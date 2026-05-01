# mtsp-routing-optimization

Курсовой проект по задаче множественного коммивояжёра (mTSP) с одной депо-вершиной
и целевой функцией MINSUM. Содержит набор эвристических solver-ов разных
поколений (от простого `2opt+greed` до модульного ALNS+SA+PT каркаса в `src/v21/`),
единый экспериментальный harness на Python и формальный отчёт в `docs/KT/`.

Полный отчёт: [`docs/KT/generated_final_report/itogovy_otchet_mtsp.tex`](docs/KT/generated_final_report/itogovy_otchet_mtsp.tex)
(с компилированной PDF-версией рядом).

## Сборка

```powershell
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Зависимости: C++17, CMake 3.20+, OpenMP (опционально, для параллельной фазы
polish). На Windows MSVC OpenMP ставится по умолчанию.

## Запуск одного solver-а

```powershell
.\build\src\Release\mtsp.exe `
  --input-file data\mtsp\generated_multifamily\uniform_n10000_m5_r01.txt `
  --step lkh_v21_minsum `
  --time-budget-ms 60000 `
  --seed 1
```

Вывод — JSON на stdout с полями `routes`, `objective`, `valid`, `time` и
расширенным `metadata`.

## Solver reference (что зарегистрировано в SolverFactory)

### Текущая флагманская архитектура (`src/v21/`)

| Имя в --step          | Файл                                     | Когда использовать |
|-----------------------|------------------------------------------|--------------------|
| `lkh_v21_minsum`      | `src/v21/minsum/minsum_solver.cpp`       | По умолчанию для MINSUM mTSP |
| `lkh_v21_minsum_cap`  | `src/v21/minsum/minsum_solver_cap.cpp`   | High-m сценарии (m≥30); FILO2-вдохновлённый capacity-prior |
| `lkh_v21_minmax`      | `src/v21/minmax/minmax_solver.cpp`       | Когда целевая функция MINMAX |

Опции: `--seed`, `--time-budget-ms`, `--threads`, плюс для `cap` —
`--route-cap` или `--route-cap-slack` (default 0.75).

### Базовые эвристики (для baseline-сравнений)

| Имя в --step    | Описание |
|-----------------|----------|
| `rand+nn`       | Случайное разбиение + ближайший сосед внутри группы |
| `2opt+greed`    | Жадное построение + 2-opt внутри маршрутов |
| `grasp`         | GRASP с RCL и межмаршрутными обменами |
| `lkh3-baseline` | Внешний LKH-3 через wrapper |
| `lkh-wrapper`   | Single-route LKH-стиль для TSP |

### Историческая линейка (v1..v21 single-file wrappers)

`lkh-wrapper-v1` … `lkh-wrapper-v21` (имена с дефисами). Каждая версия — это
итерация эволюции собственного LKH-стилевого подхода до текущей модульной
архитектуры в `src/v21/`. Используются как baseline-сравнения и для
демонстрации инженерной динамики проекта (раздел 5.4 отчёта).

**Внимание**: `lkh-wrapper-v21` (с дефисом) — это старая single-file ветвь.
Новая модульная архитектура — это `lkh_v21_minsum` / `lkh_v21_minsum_cap` /
`lkh_v21_minmax` (с подчёркиваниями).

## Экспериментальный harness

Воспроизводимые бенчмарки находятся в [`experiments/`](experiments/).
Главные точки входа:

| Скрипт                              | Что делает |
|-------------------------------------|------------|
| `experiments/run_audit.py`          | Запуск N seeds × M instances × budget-by-n; per-run JSON + summary.json |
| `experiments/compare_runs.py`       | Парный Wilcoxon signed-rank на двух наборах run-результатов |
| `experiments/analyze_run.py`        | Per-run диагностика (anytime trace, ALNS counters, balance metrics) |
| `experiments/run_benchmarks.py`     | Старый full-matrix бенч-раннер (CSV-выход) |
| `experiments/build_methodology_stats.py` | Pilot-runs + cv/CI95 для главы методологии отчёта |
| `python/mtsp_runner.py`             | Низкоуровневый solver-launcher (используется harness'ом) |

Пример воспроизводимого audit (10 seeds × 3 instance × budget-by-n):

```powershell
python experiments/run_audit.py `
  --solver lkh_v21_minsum `
  --instances data\mtsp\generated_multifamily\uniform_n10000_m5_r01.txt `
              data\mtsp\generated_multifamily\uniform_n50000_m5_r01.txt `
              data\mtsp\generated_multifamily\uniform_n100000_m5_r01.txt `
  --seeds 10 `
  --budget-by-n "10000:60000,50000:180000,100000:380000" `
  --out-dir data\results\audit\baseline `
  --tag baseline
```

Результат: `data/results/audit/baseline/runs/*.json` + `summary.json`.

## Структура проекта

```
mtsp-routing-optimization/
├─ src/                       — C++ исходники
│  ├─ v21/                    — текущая модульная архитектура
│  │  ├─ core/00..21_*.hpp    — 22 нумерованных модуля (header-only)
│  │  ├─ minsum/              — MINSUM solvers (стандартный + cap-вариант)
│  │  └─ minmax/              — MINMAX solver
│  ├─ algorithms/             — TSP-эвристики (genetic, memetic, vns, NN)
│  ├─ baselines/              — Внешний LKH-3 wrapper (Linux-only)
│  ├─ lkh_wrapper_v8/         — Историческая модульная ветвь v8 (00..60_*.cpp)
│  ├─ lkh_wrapper_v9/         — Историческая модульная ветвь v9
│  ├─ mtsp_lkh_wrapper_v1.cpp — v1..v21 single-file wrappers (исторические)
│  └─ mtsp_main.cpp           — точка входа CLI
├─ experiments/               — Python harness + benchmark configs
├─ python/                    — runners и baselines (OR-Tools, FILO2 adapter)
├─ data/
│  ├─ mtsp/generated_multifamily/  — синтетические инстансы (7 семейств × n × m)
│  └─ results/
│     ├─ audit/               — Wilcoxon-сравнения (новые artefacts)
│     │  └─ _archive/         — Устаревшие smoke/ candidate-rejected
│     └─ manual_runs/         — Ручные head-to-head сравнения
├─ docs/                      — Документация
│  ├─ KT/                     — Формальный отчёт (.tex + PDF)
│  ├─ lkh_wrapper_evolution.md — Заметки об эволюции LKH-стиля
│  ├─ reference_baseline.md    — Описание baseline-решателей
│  └─ research_hypothesis.md   — Формулировка исходных гипотез
├─ baseline/                  — Внешние solvers для сравнения
│  ├─ filo2/                  — FILO2 (CVRP-solver)
│  └─ LKH3/                   — LKH-3 source
└─ build/                     — CMake build directory (gitignored)
```

## Главный научный результат

`lkh_v21_minsum_cap` (FILO2-вдохновлённая capacity-aware repair) на high-m
сценариях ($m=100$) даёт парный MINSUM-выигрыш **−3.24% mean** на 4
инстансах, **16/20 seeds улучшено**, Wilcoxon $p = 0.002$ (статистически
значимо при $\alpha=0.05$). Разрыв до FILO2 на flagship-инстансе сокращается
с ~30% до ~16% при сохранении умеренного баланса маршрутов. Подробное
описание — раздел 5.9 формального отчёта в
[docs/KT/](docs/KT/generated_final_report/itogovy_otchet_mtsp.tex).

## Лицензия

См. [LICENSE](LICENSE).
