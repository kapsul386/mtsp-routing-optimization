# mtsp-routing-optimization

Курсовой проект по задаче множественного коммивояжёра (mTSP) с одной депо-вершиной.
Целевые функции — **MINSUM** (сумма длин всех маршрутов) и **MINMAX**
(длина самого длинного маршрута). Целевой масштаб — `N` до 100 000 городов,
`m` от 5 до 100, бюджет времени 350 секунд на CPU без GPU.

**Главный артефакт работы — модульная архитектура ALNS+SA+PT
в [`src/v21/`](src/v21/)**, выросшая из 21-итерационной single-file
линейки в [`src/legacy/`](src/legacy/).

**Итоговый отчёт:** [`docs/report/main.pdf`](docs/report/main.pdf)
([LaTeX-исходник](docs/report/main.tex)).

## Куда смотреть в первую очередь

| Что | Где |
|---|---|
| Итоговый отчёт (PDF, ~4.6 МБ, 106 страниц) | [`docs/report/main.pdf`](docs/report/main.pdf) |
| Флагманский solver `lkh_v21_minsum` | [`src/v21/minsum/minsum_solver.cpp`](src/v21/minsum/minsum_solver.cpp) |
| Ядро архитектуры (23 модуля header-only) | [`src/v21/core/`](src/v21/core/) |
| Эволюция алгоритма v1→v21 | [`src/legacy/`](src/legacy/) (см. раздел 5.4 отчёта) |
| Как воспроизвести бенчмарки | [`experiments/runners/`](experiments/runners/) + [`experiments/configs/`](experiments/configs/) |

## Главный научный результат

Флагманская архитектура — модульный ALNS+SA+PT-каркас в [`src/v21/`](src/v21/),
зарегистрированный в SolverFactory под тремя именами: `lkh_v21_minsum`
(базовый MINSUM), `lkh_v21_minsum_cap` (high-m вариант с capacity-aware
repair), `lkh_v21_minmax` (MINMAX-критерий).

**Главный экспериментальный результат — `lkh_v21_minsum_cap`** (один из
трёх вариантов v21, FILO2-вдохновлённая capacity-aware repair) на high-m
сценариях ($m=100$): парный MINSUM-выигрыш **−3.24 % mean** на 4 инстансах,
**16/20 seeds улучшено**, Wilcoxon $p = 0.002$ (статистически значимо при
$\alpha=0.05$). Разрыв до FILO2 на flagship-инстансе сокращается с ~30 % до
~16 % при сохранении умеренного баланса маршрутов. Подробное описание —
раздел 5.9 отчёта.

## Сборка

```powershell
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

**Зависимости:** C++23, CMake 3.25+, OpenMP (опционально, для параллельной
полишинг-фазы). На Windows MSVC OpenMP ставится по умолчанию. На Linux:
gcc 12+ или clang 16+ с поддержкой C++23.

После сборки появятся два бинарника:

- `build/src/Release/mtsp.exe` — основной mTSP-решатель
- `build/src/Release/tsp.exe` — TSP-решатель (single-route, для
  сравнительных экспериментов)

## Запуск

### Один прогон mTSP (флагман v21)

```powershell
.\build\src\Release\mtsp.exe `
  --input-file data\mtsp\generated_multifamily\uniform_n10000_m5_r01.txt `
  --step lkh_v21_minsum `
  --time-budget-ms 60000 `
  --seed 1
```

Вывод — JSON на stdout с полями `routes`, `objective`, `valid`, `time`,
`metadata` (содержит anytime-trace, ALNS-счётчики, balance-метрики).

### Воспроизведение полного бенчмарка

Например, прогон large-scale (N=25K..100K, ~6 часов):

```powershell
.\experiments\runners\run_large_scale.ps1
```

Скрипт последовательно прогоняет 4 стадии из конфигов
`experiments/configs/large_scale_*.json` и пишет логи в `data/results/`.
Аналогично — `run_extended.ps1` (расширенный second-pass) и
`run_verify.ps1` (верификация с честным LKH-3 binary).

### Кастомный audit-прогон

```powershell
python experiments/runners/run_audit.py `
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

## Solver reference (зарегистрировано в SolverFactory)

### Флагман — `src/v21/`

| Имя в `--step`        | Файл                                     | Когда использовать |
|-----------------------|------------------------------------------|--------------------|
| `lkh_v21_minsum`      | `src/v21/minsum/minsum_solver.cpp`       | По умолчанию для MINSUM mTSP |
| `lkh_v21_minsum_cap`  | `src/v21/minsum/minsum_solver_cap.cpp`   | High-m (m≥30) — FILO2-вдохновлённый capacity-prior |
| `lkh_v21_minmax`      | `src/v21/minmax/minmax_solver.cpp`       | Когда целевая функция MINMAX |

Опции: `--seed`, `--time-budget-ms`, `--threads`. Для `cap`:
`--route-cap` или `--route-cap-slack` (default 0.75).

### Базовые эвристики — `src/solvers/`

| Имя в `--step`  | Описание |
|-----------------|----------|
| `rand+nn`       | Случайное разбиение + ближайший сосед внутри группы |
| `2opt+greed`    | Жадное построение + 2-opt внутри маршрутов |
| `grasp`         | GRASP с RCL и межмаршрутными обменами |
| `lkh3-baseline` | Внешний LKH-3 через C++-обёртку (нужен LKH binary под WSL) |
| `lkh-wrapper`   | Single-route LKH-стиль для TSP |

### Историческая линейка — `src/legacy/`

`lkh-wrapper-v1` … `lkh-wrapper-v21` (имена через дефис) — single-file
итерации эволюции собственного LKH-стилевого подхода. Используются как
сравнительные baseline и для демонстрации инженерной динамики проекта
(раздел 5.4 отчёта).

> **Важно:** `lkh-wrapper-v21` (с **дефисом**) — это старая single-file
> ветвь. Новая модульная архитектура — это `lkh_v21_minsum` /
> `lkh_v21_minsum_cap` / `lkh_v21_minmax` (с **подчёркиваниями**).

## Структура проекта

```text
mtsp-routing-optimization/
│
├─ src/                        — C++ исходники (всё что компилируется в mtsp.exe / tsp.exe)
│  ├─ v21/                     ФЛАГМАН — модульная ALNS+SA+PT-архитектура
│  │  ├─ core/                 — 23 header-only модуля ядра
│  │  │  ├─ 00_types.hpp           — базовые типы (Route, RouteSet, …)
│  │  │  ├─ 01_budget.hpp          — управление time-budget по фазам
│  │  │  ├─ 02_distance.hpp        — lazy distance oracle + KD-tree-based кэш
│  │  │  ├─ 03_kdtree.hpp          — KD-tree для NN-запросов
│  │  │  ├─ 04..05_*               — route-index, route-list (структуры данных)
│  │  │  ├─ 06_candidate_set.hpp   — K-NN candidate set
│  │  │  ├─ 07_seed_routes.hpp     — генерация начального решения
│  │  │  ├─ 08..10_*               — local search (2-opt, 3-opt-light, inter-route)
│  │  │  ├─ 11_validation.hpp      — проверка корректности маршрутов
│  │  │  ├─ 12_alns_framework.hpp  — ALNS-фреймворк (destroy + repair)
│  │  │  ├─ 13_destroy_ops.hpp     — destroy-операторы (random, worst, related, …)
│  │  │  ├─ 14_repair_ops.hpp      — repair-операторы (greedy, regret-2, …)
│  │  │  ├─ 15_sa_engine.hpp       — Simulated Annealing engine
│  │  │  ├─ 16_parallel_pool.hpp   — multi-start ensemble (упомянут как PT в ранних версиях)
│  │  │  ├─ 17_pipeline.hpp        — главный pipeline: seed → polish → ALNS → final-2opt
│  │  │  ├─ 18_autotune.hpp        — автопараметризация по (n, m)
│  │  │  ├─ 19_gls.hpp             — Guided Local Search
│  │  │  ├─ 20_route_pair_reopt.hpp — перестройка пар маршрутов
│  │  │  ├─ 21_popmusic.hpp        — POPMUSIC-стиль восстановление
│  │  │  └─ 22_region_granular_ops.hpp — region-reopt (DualOpt-вдохновлённое)
│  │  ├─ minsum/               — MINSUM-решатели
│  │  │  ├─ minsum_solver.cpp           — стандартный MINSUM
│  │  │  ├─ minsum_solver_cap.cpp       — capacity-aware вариант (high-m)
│  │  │  ├─ minsum_solver_depot2m_plus.cpp — экспериментальный пресет
│  │  │  └─ minsum_accept.hpp           — критерий принятия для ALNS
│  │  └─ minmax/               — MINMAX-решатели
│  │     ├─ minmax_solver.cpp
│  │     └─ minmax_accept.hpp           — soft-alpha критерий приёма
│  │
│  ├─ solvers/                 — standalone mTSP-решатели (baseline-сравнение)
│  │  ├─ mtsp_grasp_solver.cpp         → --step grasp
│  │  ├─ mtsp_greedy_2opt_solver.cpp   → --step 2opt+greed
│  │  ├─ mtsp_rand_nn_solver.cpp       → --step rand+nn
│  │  └─ lkh_wrapper.cpp               → --step lkh-wrapper (single-route TSP)
│  │
│  ├─ algorithms/              — общие TSP/mTSP-эвристики
│  │  ├─ tsp/                  — генетические, мемические, VNS, NN (для tsp.exe)
│  │  └─ mtsp/                 — standalone CLI-варианты (не подключены к build, для reference)
│  │
│  ├─ baselines/               — C++-обёртка над LKH-3 (запуск через WSL)
│  │  ├─ lkh3_baseline.cpp     → --step lkh3-baseline
│  │  └─ lkh3_baseline.h
│  │
│  ├─ legacy/                  — историческая эволюция (см. раздел 5.4 отчёта)
│  │  ├─ mtsp_lkh_wrapper_v{1..21}.cpp  — single-file линейка v1…v21
│  │  ├─ lkh_wrapper_v8/                — модульная ветвь v8 (00..60_*.cpp)
│  │  └─ lkh_wrapper_v9/                — модульная ветвь v9
│  │
│  ├─ instance.cpp             — парсер TSP-инстансов
│  ├─ mtsp_instance.cpp        — парсер mTSP-инстансов (формат "n m\n n×(x y)")
│  ├─ mtsp_utils.cpp           — общие утилиты (геометрия, JSON-сериализация)
│  ├─ main.cpp                 — точка входа tsp.exe
│  ├─ mtsp_main.cpp            — точка входа mtsp.exe (главный CLI)
│  └─ CMakeLists.txt           — определяет mtsp_core / mtsp / tsp targets
│
├─ include/                    — публичные C++-заголовки (factory, solver-interface, json.hpp)
│
├─ experiments/                — Python-харнес для бенчмарков
│  ├─ configs/                 — конфиги прогонов (что, чем, на каких инстансах)
│  │  ├─ stratum1_*.json       — стратум 1: малые N=100..1000 (корректность)
│  │  ├─ stratum2_config.json  — стратум 2: верификация N=10K..25K
│  │  ├─ large_scale_*.json    — основные большие прогоны N=25K, 50K, 100K
│  │  ├─ extended_*.json       — расширенный second-pass на больших N
│  │  ├─ verify_*.json         — верификационные прогоны с canonical LKH-3
│  │  ├─ high_m_*.json         — high-m сценарии (m≥30)
│  │  └─ config.{json,yaml}    — generic-конфиги по умолчанию
│  ├─ runners/                 — скрипты запуска (ставят прогон в очередь и пишут результаты)
│  │  ├─ run_audit.py          — запуск N seeds × M instances × budget-by-n
│  │  ├─ run_benchmarks.py     — full-matrix бенч-раннер (CSV-выход)
│  │  ├─ run_ortools_stratum1.py — OR-Tools на stratum-1 (отдельный путь)
│  │  ├─ run_large_scale.ps1   — PowerShell-обёртка для всей серии large-scale
│  │  ├─ run_extended.ps1      — то же для extended (second-pass)
│  │  ├─ run_verify.ps1        — для verification-прогона с правильным LKH-3
│  │  └─ run_v4_v7_baseline_long_run.ps1, repair_*.ps1 — служебные long-run скрипты
│  ├─ analysis/                — анализ собранных результатов (CSV/JSON → таблицы/фигуры)
│  │  ├─ analyze_run.py        — per-run диагностика (anytime trace, ALNS-counters)
│  │  ├─ compare_runs.py       — парный Wilcoxon на двух наборах run-результатов
│  │  ├─ compare_baselines.py  — таблица сравнения всех baseline + v21
│  │  ├─ build_figures_v3.py   — генерация всех фигур для отчёта (требует matplotlib)
│  │  ├─ build_stratum1_final_table.py — итоговая таблица stratum-1
│  │  ├─ enrich_results_with_metrics.py — добавляет sum_length, max_length, balance в CSV
│  │  └─ generate_*infographic.py — иллюстрации семейств инстансов / depot-2m бюджета
│  ├─ review_fixes/            — пост-ревью расширенный анализ
│  │  └─ (~80 файлов: ablation, BCa-bootstrap, FILO2 vs ALNS, mTSPLib и т.д.)
│  ├─ generate_mtsp_instances.py  — генератор синтетических mTSP-инстансов
│  └─ mtsp_experiment_utils.py    — общие функции (загрузка инстансов, валидация семейств)
│
├─ python/                     — Python-обёртки над C++-binary + ML-baselines
│  ├─ cpp_updater.py           — авто-rebuild C++-binary при изменениях
│  ├─ mtsp_runner.py           — низкоуровневый launcher mtsp.exe (вызывается harness'ом)
│  ├─ tsp_runner.py            — то же для tsp.exe
│  ├─ validate.py              — валидация решения mTSP (closed-tour, m routes, no dups)
│  └─ mtsp_baselines/          — Python-baselines: OR-Tools, FILO2-CVRP-адаптер, exact-MIP
│     ├─ ortools_routing.py
│     ├─ tsp_transform_lkh.py
│     ├─ exact_mip.py
│     └─ common.py
│
├─ data/
│  ├─ mtsp/                    — синтетические mTSP-инстансы (формат "n m\n n×(x y)")
│  │  ├─ generated_multifamily/      — 215 инстансов, 7 семейств × {n} × {m}
│  │  ├─ generated_high_m_fixedgeo/  — 141 high-m fixed-geometry инстанс
│  │  ├─ stratum1_small/             — 60 малых инстансов (N=100..1000)
│  │  └─ mtsplib/                    — 16 mTSPLib-производных инстансов (TSPLIB → mTSP)
│  ├─ tsp/                     — TSP-инстансы для tsp.exe
│  └─ results/                 — CSV/JSON-выходы прогонов
│     ├─ audit/stratum3_multiseed/   — 5-seed multi-seed audit (главные таблицы отчёта)
│     ├─ extended_*.csv,            — большие прогоны (extended-second-pass)
│     ├─ large_scale_*.csv,         — основные large-scale прогоны
│     ├─ stratum1_*.csv,            — stratum-1 (малые инстансы)
│     └─ verify_*.csv               — верификационные прогоны
│
├─ docs/
│  ├─ report/                  — итоговый отчёт ВШЭ
│  │  ├─ main.tex              — исходник (XeLaTeX, ~2150 строк)
│  │  ├─ main.pdf              — собранный PDF (~4.6 МБ, 106 страниц)
│  │  ├─ fig*.png              — фигуры отчёта (40+ изображений)
│  │  └─ _new_figs/            — рабочая директория генерации новых фигур
│  ├─ KT/                      — обязательные документы курсовой ВШЭ
│  │  ├─ Описание_проекта.docx
│  │  ├─ Промежуточный_отчет_КТ1.docx
│  │  └─ Ссылка_на_исходный_код_проекта.docx
│  └─ materials/               — указатель на референсные PDF (статьи хранятся локально)
│     └─ README.md             — список ссылок на arXiv / DOI цитируемых работ
│
├─ build/                      — CMake build directory (gitignored)
├─ external/                   — клоны внешних решателей: FILO2, HGS-CVRP, vroom (gitignored)
│
├─ CMakeLists.txt              — корневой CMake, делегирует в src/
├─ run_mtsp.py                 — Python-обёртка для запуска mtsp.exe
├─ README.md                   — этот файл
└─ LICENSE                     — MIT
```

## Платформа

- **Windows + MSVC + WSL** — основная платформа разработки. LKH-3 / FILO2
  binary запускаются под WSL (путь задаётся переменной `LKH3_WSL_BIN` /
  `FILO2_WSL_BIN`).
- **Linux** — должно собираться нативно (gcc/clang + cmake), но
  C++-обёртка LKH-3 в `src/baselines/` исключается из build на не-Windows
  (см. CMakeLists.txt) — на Linux запускайте LKH-3 напрямую.

## Лицензия

См. [LICENSE](LICENSE).
