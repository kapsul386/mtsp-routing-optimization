# План эволюции `lkh-wrapper`

## Зачем

Текущую реализацию `lkh-wrapper` нужно улучшать итеративно и сравнивать с уже зафиксированным baseline на одном и том же наборе инстансов.

## Принцип работы

1. Текущая стабильная версия фиксируется как отдельный solver-файл и отдельное имя солвера.
2. Каждое следующее улучшение делается в новом `.cpp`-файле.
3. Сравнение версий проводится на одном и том же benchmark-конфиге.
4. После сравнения либо переводим новую версию в основной benchmark, либо оставляем её экспериментальной.

## Текущее соглашение по версиям

- baseline для ретроспективного сравнения: `lkh-wrapper-v1`
- актуальный solver в основном benchmark: `lkh-wrapper-v2`
- будущие версии: `lkh-wrapper-v3`, `lkh-wrapper-v4`, ...

## Что зафиксировано в `lkh-wrapper-v2`

Версия `lkh-wrapper-v2` пробует приблизиться к идеям Lin-Kernighan через:

- candidate sets для ограничения поиска хорошими соседями;
- positive-gain 2-opt перед полным cleanup-проходом;
- простую one-step lookahead эвристику на этапе построения начального решения.

Именно эта версия теперь используется в основном конфиге
[experiments/config.json](C:/Users/ddkup/курчас/mtsp-routing-optimization/experiments/config.json).

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
