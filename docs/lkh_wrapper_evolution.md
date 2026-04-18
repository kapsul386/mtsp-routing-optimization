# План эволюции `lkh-wrapper`

## Зачем

Текущую реализацию `lkh-wrapper` нужно улучшать итеративно и сравнивать с уже зафиксированным baseline на одном и том же наборе инстансов.

## Принцип работы

1. Текущая стабильная версия фиксируется как отдельный solver-файл и отдельное имя солвера.
2. Каждое следующее улучшение делается в новом `.cpp`-файле.
3. Сравнение версий проводится на одном и том же benchmark-конфиге.
4. После сравнения решаем, оставлять ли новую версию как основной `lkh-wrapper`.

## Текущее соглашение по версиям

- baseline: `lkh-wrapper-v1`
- актуальный рабочий solver: `lkh-wrapper`
- будущие версии: `lkh-wrapper-v2`, `lkh-wrapper-v3`, ...

## Как сравнивать версии

Используется отдельный конфиг:

- [experiments/lkh_versions_config.json](C:/Users/ddkup/курчас/mtsp-routing-optimization/experiments/lkh_versions_config.json)

Запуск:

```bash
python experiments/run_benchmarks.py --config experiments/lkh_versions_config.json
python experiments/build_report.py --config experiments/lkh_versions_config.json
```

Результаты складываются в:

- `data/results/lkh_versions_results.csv`
- `data/results/lkh_versions_summary.csv`
- `data/results/lkh_versions_report.md`

## Что фиксировать для каждой новой версии

- какая именно идея добавлена;
- как изменилось качество `MINSUM`;
- как изменилось время;
- на каких размерах задач улучшение устойчиво, а на каких нет.
