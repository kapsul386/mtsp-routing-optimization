# `data/results/audit/` — Wilcoxon-сравнения и audit-эксперименты

Каждая поддиректория содержит набор JSON-файлов одного бенч-прогона
(`runs/<instance>__seedNNN.json`) и `summary.json` с агрегатом mean/std/min/max.
Все эти артефакты воспроизводимы через `experiments/run_audit.py` —
не редактировать вручную.

## Активные результаты (используются в отчёте)

| Директория            | Что внутри                                                                             | Где упомянуто |
|-----------------------|----------------------------------------------------------------------------------------|---------------|
| `baseline/`           | 30 runs (10 seeds × 3 flagship): n=10k/50k/100k, `lkh_v21_minsum`, бюджеты 60/180/380s | Раздел 5.8: variance audit (cv 0.17–0.59%) |
| `baseline_today/`     | 5 seeds × n=100k 380s, original v21 binary, today's wall-clock                          | Раздел 5.9: apples-to-apples baseline для B v3 |
| `candidate_B3/`       | 5 seeds × n=100k 380s, B v3 binary (time-based reheat)                                  | Раздел 5.9 |
| `h2h_n10k_m100/`      | Flagship cap vs baseline: 5 seeds × clustered-offset-depot n=10k m=100 (60s)            | Раздел 5.9 (главная таблица cap-фикса) |
| `multi_m100/`         | Multi-instance cap vs baseline: 3 high-m инстансов (clustered-center, mixed-outliers, n10k_m100) | Раздел 5.9: обобщение flagship-результата |
| `profile_n100k/`      | Single seed n=100k 380s, профильный анализ                                              | Раздел 5.8 (анализ throughput-кривой) |
| `anytime_sweep/`      | Тест A: 4 budgets × 3 solvers × 5 seeds на flagship                                     | Раздел 5.10 (anytime profile) |
| `m_sweep_cc/`         | Тест B: m ∈ {3,5,10,30,50,80,100} × 5 seeds × 60s на clustered-center                   | Раздел 5.11 (m-sensitivity) |

## Архивные результаты

`_archive/` содержит ранее сделанные но не вошедшие в финальный отчёт
бенчмарки. Сохранены для воспроизводимости / истории, но не цитируются:

| Директория                       | Почему в архиве |
|----------------------------------|-----------------|
| `_smoke/`, `_smoke_cap/`, `_smoke_n10k/`, `_smoke_v2/` | Smoke-тесты компилятора и базовой работоспособности; больше не нужны |
| `candidate_B/`                   | Промежуточная версия B v1 (iter-threshold) — не дала эффекта |
| `candidate_B2/`                  | Промежуточная версия B v2 (time-threshold с другой семантикой reset) — не дала эффекта |
| `m_sweep_uniform_partial/`       | Первая попытка m-sweep на uniform: семейство не содержит m≥10 |

## Воспроизведение

```powershell
# Например, baseline:
python experiments\run_audit.py `
  --solver lkh_v21_minsum `
  --instances data\mtsp\generated_multifamily\uniform_n10000_m5_r01.txt `
              data\mtsp\generated_multifamily\uniform_n50000_m5_r01.txt `
              data\mtsp\generated_multifamily\uniform_n100000_m5_r01.txt `
  --seeds 10 `
  --budget-by-n "10000:60000,50000:180000,100000:380000" `
  --out-dir data\results\audit\baseline `
  --tag baseline
```

Парные сравнения через `experiments\compare_runs.py`:

```powershell
python experiments\compare_runs.py `
  --baseline data\results\audit\h2h_n10k_m100\v21_today `
  --candidate data\results\audit\h2h_n10k_m100\v21_cap_075
```
