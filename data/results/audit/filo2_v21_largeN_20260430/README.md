# FILO2 vs lkh_v21_minsum_cap on large instances (2026-04-30)

Парный head-to-head на n>25000 uniform m=5. Создан в день когда выяснилось,
что `baseline/filo2withoutcode/run_filo2_baseline.py` в дефолтной конфигурации
не уважает `--optimization-seconds` на n>25000: фаза `routemin` (heuristic
reduction of route count) пробегает 1000 итераций без time-checks и съедает
весь бюджет до того, как стартанёт COREOPT.

## Решение для запуска FILO2 на n>25k

`--extra-arg=--routemin-iterations --extra-arg=30` (вместо дефолтных 1000).
С этим параметром на n=50k routemin занимает ~30-60s, оставляя время на
COREOPT-loop. На n=100k preprocessing+routemin всё ещё съедает ~800-1000s
wall, и `--optimization-seconds` < 1000 эффективно игнорируется (COREOPT
запускается с 0 итерациями, результат = post-routemin solution).

## Структура

```
filo2_v21_largeN_20260430/
├─ filo2_n50k_uniform_m5/         FILO2 budget 300s, 3 seeds
├─ filo2_n100k_uniform_m5/        FILO2 budget 600s, 3 seeds
├─ filo2_n50k_uniform_m5_90s/     FILO2 budget  90s, 3 seeds
├─ filo2_n100k_uniform_m5_300s/   FILO2 budget 300s, 3 seeds
├─ v21_n50k_uniform_m5/           v21_minsum_cap budget 300s, 3 seeds
├─ v21_n100k_uniform_m5/          v21_minsum_cap budget 600s, 3 seeds
├─ v21_n50k_uniform_m5_90s/       v21_minsum_cap budget  90s, 3 seeds
├─ v21_n100k_uniform_m5_300s/     v21_minsum_cap budget 300s, 3 seeds
├─ summary.txt                    Сводка long-batch (300s/600s)
├─ summary_short.txt              Сводка short-batch (90s/300s)
├─ summary_proper.txt             Детальная per-seed таблица long-batch
├─ run.log                        Логи long-batch
└─ run_short.log                  Логи short-batch
```

FILO2 JSONs: top-level `<instance>_m<m>_seed<N>.json`, raw FILO2 stdout/sol
в `raw/<instance>_m<m>_seed<N>/`, постпроцессенные маршруты в `..._routes.json`.

v21 JSONs: `runs/<instance>__seed<NNN>.json` (формат `run_audit.py`) +
`summary.json` (агрегаты).

## Главные числа

### n=50000 uniform m=5

| Budget | FILO2 obj  | v21 obj  | Δ (v21−FILO2) | FILO2 wall | v21 wall | FILO2 imbal | v21 imbal |
|--------|------------|----------|---------------|------------|----------|-------------|-----------|
|  90s   | 4,558,259  | 4,569,371| +0.24%        | 182s       |  80s     | 1.47        | 1.04      |
| 300s   | 4,181,251  | 4,505,940| +7.77%        | 300s       | 188s     | 1.44        | 1.03      |

### n=100000 uniform m=5

| Budget | FILO2 obj   | v21 obj    | Δ      | FILO2 wall | v21 wall | FILO2 imbal | v21 imbal |
|--------|-------------|------------|--------|------------|----------|-------------|-----------|
| 300s   | 12,708,145  | 13,019,650 | +2.45% | 1024s      | 295s     | 1.37        | 1.02      |
| 600s   | 12,708,145  | 12,892,443 | +1.45% |  824s      | 465s     | 1.37        | 1.02      |

Обрати внимание: FILO2 на n=100k даёт **идентичный obj при budget=300s и
600s**, потому что preprocessing+routemin в обоих случаях > budget, и
COREOPT-loop запускается с 0 итерациями. Результат — детерминированный
post-routemin solution.

## Воспроизведение

```bash
# FILO2 single seed
python3 baseline/filo2withoutcode/run_filo2_baseline.py \
  --input data/mtsp/generated_multifamily/uniform_n50000_m5_r01.txt \
  --m 5 --time-limit 300 --seed 1 \
  --output-dir data/results/audit/filo2_v21_largeN_20260430/filo2_n50k_uniform_m5 \
  --extra-arg=--routemin-iterations --extra-arg=30

# v21 audit (3 seeds)
python3 experiments/run_audit.py \
  --solver lkh_v21_minsum_cap \
  --instances data/mtsp/generated_multifamily/uniform_n50000_m5_r01.txt \
  --seeds 3 \
  --budget-by-n 50000:300000 \
  --out-dir data/results/audit/filo2_v21_largeN_20260430/v21_n50k_uniform_m5 \
  --tag n50k_uniform_m5
```

FILO2 binary должен быть собран: см. `baseline/filo2withoutcode/build_filo2.sh`.
Адаптер вызывает Linux ELF `external/filo2/build/filo2`, поэтому запуск
требует WSL Ubuntu (или Linux хоста); из cmd/PowerShell через
`wsl.exe -d Ubuntu -- bash ...`.
