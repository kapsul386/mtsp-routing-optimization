# v14 Patch

## Что лежит в этой папке

- `mtsp_lkh_wrapper_v14.cpp` — новая полноценная версия solver'а под именем
  `lkh-wrapper-v14`. Это копия `v12.cpp` + правки B (квадратичный repair),
  metadata, repair timing, OpenMP polish, anneal force-on на n>=50k.
  v12 не трогается, остается фиксированным baseline.

- `CMakeLists.txt` — обновленная сборка `src/CMakeLists.txt`.
  Изменения относительно текущего `src/CMakeLists.txt` в репо:
  1. Добавлен `find_package(OpenMP)` + линковка `OpenMP::OpenMP_CXX`
     к `mtsp_core`, `tsp`, `mtsp` (нужно для правки D).
  2. Добавлен `if(NOT WIN32)` exclude для `baselines/lkh3_baseline.cpp` -
     это фикс для Linux-сборок где `windows.h` недоступен. На Windows эта
     ветка ничего не меняет, так что безопасно перезаписать.

## Куда класть

```
mtsp-routing-optimization/
  src/
    mtsp_lkh_wrapper_v14.cpp   <- сюда положить новый файл
    CMakeLists.txt              <- сюда заменить (или вручную добавить
                                    блок OpenMP к существующему)
```

CMake авто-подхватит новый `.cpp` через `file(GLOB_RECURSE LIB_SOURCES ...)`.

## Сборка

```powershell
cd C:\Users\ddkup\coursework\mtsp-routing-optimization
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

Если CMake скажет `OpenMP not found` — на Windows MSVC он обычно есть из
коробки. Если используется MinGW - убедиться что установлен `libgomp`.

## Использование

Идентично v12, плюс одна новая опция:

```powershell
.\build\src\Release\mtsp.exe `
  --input-file data\mtsp\generated_multifamily\uniform_n100000_m5_r01.txt `
  --step lkh-wrapper-v14 `
  --time-budget-ms 300000 `
  --seed 42
```

Опционально:
- `--omp-polish false` - выключить OpenMP polish (диагностика, или если OOM)
- `--threads 5` - ограничить число OMP потоков (default - auto)
- `--anneal-route-ms 80000` - переопределить дефолтный anneal-бюджет

## Что появилось в metadata

```
omp_polish: true|false|unavailable
omp_max_threads: <N>

improve_phase_budget_ms
improve_phase_passes_completed
improve_phase_passes_improved_minsum
improve_phase_passes_improved_balanced
improve_phase_minsum_delta
improve_phase_balanced_delta
improve_phase_elapsed_ms
improve_phase_start_minsum
improve_phase_end_minsum
polish_pass_<N>_minsum   (для каждого pass)
polish_pass_<N>_delta    (для каждого pass)

route_repair_early_insert_us
route_repair_early_cleanup_us
route_repair_first_insert_us
route_repair_first_cleanup_us
route_repair_late_insert_us
route_repair_late_cleanup_us

route_anneal_requested_ms   (что хотели; route_anneal_ms - что реально дали)
```

## Sweep-команда для проверки на n=100000, 300s

```powershell
mkdir runs
foreach ($seed in 1,2,3,4,5) {
  foreach ($ver in "v12","v14") {
    .\build\src\Release\mtsp.exe `
      --input-file data\mtsp\generated_multifamily\uniform_n100000_m5_r01.txt `
      --step "lkh-wrapper-$ver" `
      --time-budget-ms 300000 `
      --seed $seed `
      > "runs\${ver}_n100k_300s_seed${seed}.json" 2>&1
  }
}
```

После прогона: сравнить `objective` по 5 seed'ам (median + min) для
v12 vs v14, плюс посмотреть в metadata v14 действительно ли работает
`improve_phase_minsum_delta > 0` и `route_anneal_best_delta > 0`.

## Что НЕ вошло (отложено в v15)

- Правка C (точечный сброс don't-look bits): требует переписать
  `ApplyFirstImproving2OptV5` на queue-based loop, иначе регрессия
  ~1% objective. Это ~80-100 строк правок в `lkh_wrapper_v9/20_route_local_search.cpp`.
