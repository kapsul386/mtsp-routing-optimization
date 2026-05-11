"""
Sensitivity-анализ alns_minmax на меньших масштабах N=10K, 25K — где budget bug
не проявляется (см. диагностический результат для N=100K в Б.5).

Цель: подтвердить что alns_minmax работоспособен на N <= 25K и выходит на плато
после ~150-350с, аналогично alns_minsum.

Сетка: 2 N × 3 family × 4 budget = 24 runs.
Time-budget = {30, 70, 150, 350} секунд.

Артефакт: sensitivity_makespan_smaller_results.csv
"""

import csv
import json
import subprocess
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / 'build' / 'src' / 'Release' / 'mtsp.exe'
DATA = ROOT / 'data' / 'mtsp' / 'generated_multifamily'

INSTANCES = [
    'uniform_n10000_m5_r01.txt',
    'clustered-center_n10000_m5_r01.txt',
    'clustered-offset-depot_n10000_m5_r01.txt',
    'uniform_n25000_m5_r01.txt',
    'clustered-center_n25000_m5_r01.txt',
    'clustered-offset-depot_n25000_m5_r01.txt',
]
BUDGETS_S = [30, 70, 150, 350]
SEED = 1

OUTPUT_CSV = ROOT / 'experiments' / 'review_fixes' / 'sensitivity_makespan_smaller_results.csv'


def _num(v, default=0.0):
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def _int(v, default=0):
    try:
        return int(float(v))
    except (TypeError, ValueError):
        return default


def run_one(instance_path: Path, budget_s: int) -> dict:
    args = [
        str(EXE),
        '--input-file', instance_path.name,
        '--emit-routes', 'false',
        '--step', 'lkh_v21_minmax',
        '--seed', str(SEED),
        '--time-budget-ms', str(budget_s * 1000),
    ]
    print(f'  → {instance_path.name} budget={budget_s}s ...', flush=True)
    proc = subprocess.run(
        args, cwd=str(instance_path.parent),
        capture_output=True, text=True,
        encoding='utf-8', errors='replace',
        timeout=budget_s * 4 + 60,  # generous timeout: 4× budget + 1 min for safety
    )
    if proc.returncode != 0:
        return {'error': proc.stderr.strip()[:300]}
    out = json.loads(proc.stdout)
    metadata = out.get('steps', [{}])[0].get('metadata') or out.get('metadata') or {}
    return {
        'objective': _num(out.get('objective')),
        'minsum': _num(metadata.get('final_minsum')),
        'makespan': _num(metadata.get('final_max')),
        'gini': _num(metadata.get('final_gini')),
        'time_s': _num(out.get('time')),
        'valid': bool(out.get('valid')),
        'alns_iters': _int(metadata.get('alns_iters')),
    }


def main():
    rows = []
    for inst in INSTANCES:
        path = DATA / inst
        if not path.exists():
            print(f'NOT FOUND: {path.name}')
            continue
        print(f'\n=== Инстанс: {inst} ===')
        for budget in BUDGETS_S:
            try:
                r = run_one(path, budget)
                if 'error' in r:
                    print(f'    ERROR: {r["error"][:200]}')
                    rows.append({'instance': inst, 'budget_s': budget, 'valid': False, 'error': r['error'][:200]})
                    continue
                row = {
                    'instance': inst, 'budget_s': budget,
                    'valid': r['valid'],
                    'minsum': r['minsum'] or r['objective'],
                    'makespan': r['makespan'],
                    'gini': r['gini'],
                    't_actual_s': r['time_s'],
                    'alns_iters': r['alns_iters'],
                    'overrun_x': r['time_s'] / budget if budget > 0 else 0,
                }
                rows.append(row)
                print(f'    minsum={row["minsum"]:.0f}  max={row["makespan"]:.0f}  '
                      f'gini={row["gini"]:.3f}  t={row["t_actual_s"]:.1f}s '
                      f'(overrun ×{row["overrun_x"]:.2f})  iters={row["alns_iters"]}')
            except subprocess.TimeoutExpired:
                print(f'    TIMEOUT (4× overrun)')
                rows.append({'instance': inst, 'budget_s': budget, 'valid': False, 'error': 'timeout'})
            except Exception as e:
                print(f'    EXCEPTION: {e}')
                rows.append({'instance': inst, 'budget_s': budget, 'valid': False, 'error': str(e)[:200]})

    fieldnames = ['instance', 'budget_s', 'valid', 'minsum', 'makespan', 'gini',
                  't_actual_s', 'alns_iters', 'overrun_x', 'error']
    with open(OUTPUT_CSV, 'w', encoding='utf-8', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for row in rows:
            for k in fieldnames:
                row.setdefault(k, '')
            w.writerow(row)
    print(f'\nЗаписано: {OUTPUT_CSV}')
    print(f'Валидных: {sum(1 for r in rows if r.get("valid"))}/{len(rows)}')


if __name__ == '__main__':
    main()
