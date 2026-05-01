# VROOM baseline

Production-style C++20 route optimization engine. Repo:
https://github.com/VROOM-Project/vroom

VROOM описывает себя как "open-source route optimization engine that
solves VRP in milliseconds" — нужен как «практический» industrial
baseline (не academic-quality, но хорошо тюнингованный production).

## Установка и сборка

```bash
# В WSL Ubuntu (или на Linux хосте):
sudo apt install -y g++ make libssl-dev libasio-dev   # one-time deps
bash baseline/vroom/install_vroom.sh    # git clone в external/vroom
bash baseline/vroom/build_vroom.sh      # make -j → external/vroom/bin/vroom
```

Если binary не собран, runner вернёт `status: "binary_not_built"`.

## VRP-постановка для mTSP

Конвертация mTSP → VROOM JSON формат (`mtsp_to_vroom_json.py`):

```json
{
  "vehicles": [
    {"id": 0, "profile": "car", "start_index": 0, "end_index": 0, "capacity": [C]},
    ...   // m vehicles
  ],
  "jobs": [
    {"id": 1, "location_index": 1, "delivery": [1]},
    ...   // n-1 customer jobs
  ],
  "matrices": {
    "car": {"durations": [[...]]}     // n×n int Euclidean L2
  }
}
```

VROOM работает с целочисленными durations — Euclidean distance
округляется до int. Capacity = ceil((n-1)/m), 1 unit demand per job.

## Использование

```bash
python baseline/vroom/run_vroom_baseline.py \
  --input data/mtsp/generated_multifamily/uniform_n1000_m5_r01.txt \
  --m 5 --seed 1 \
  --output-dir data/results/baselines/vroom/smoke
```

VROOM не имеет --time-limit как у академических solver'ов — он
завершается когда исчерпает свой эвристический пайплайн (милисекунды
для small VRP).

Параметры VROOM можно прокинуть через `--extra-arg`:
```bash
python ... --extra-arg=-x --extra-arg=10   # exploration level 0..5
```

## Scalability ceiling

VROOM хранит full distance matrix O(n²). Прикидки:
- n ≤ 5000: миллисекунды
- n ≤ 10000: секунды
- n ≤ 25000: minutes, ~5 ГБ RAM
- n ≥ 50000: возможно OOM на стандартной системе

Тестировать **up to n=25000**, ceiling уточнить sweep'ом.

## Output

VROOM-specific summary (cost, duration, computing_times, unassigned,
routes-count) сохраняется в результирующем JSON под ключом
`vroom_summary`. Стандартизированные `metrics` / `raw_metrics` —
совместимы с FILO2 / OR-Tools / другими. См. `baseline/ortools/README.md`.
