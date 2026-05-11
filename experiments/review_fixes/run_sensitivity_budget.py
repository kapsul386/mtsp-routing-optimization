"""
Sensitivity-анализ time-budget на 3 flagship-инстансах.

Запускает alns_minsum (lkh_v21_minsum) на трёх инстансах N=100K, m=5
с бюджетами {50, 150, 350, 1000} секунд.

Результат: CSV с (instance, budget_s, minsum, gini, makespan, max_route, t_actual_s),
плюс anytime-trace для построения графика "качество vs бюджет".

Артефакт: experiments/review_fixes/sensitivity_budget_results.csv
"""

import csv
import json
import sys
import subprocess
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / 'build' / 'src' / 'Release' / 'mtsp.exe'
DATA = ROOT / 'data' / 'mtsp' / 'generated_multifamily'

INSTANCES = [
    'uniform_n100000_m5_r01.txt',
    'clustered-center_n100000_m5_r01.txt',
    'clustered-offset-depot_n100000_m5_r01.txt',
]
BUDGETS_S = [50, 150, 350, 1000]

OUTPUT_CSV = ROOT / 'experiments' / 'review_fixes' / 'sensitivity_budget_results.csv'
SEED = 1


def run_one(instance_path: Path, budget_s: int) -> dict:
    """Один запуск alns_minsum с заданным time-budget-ms."""
    budget_ms = budget_s * 1000
    args = [
        str(EXE),
        '--input-file', instance_path.name,
        '--emit-routes', 'false',  # чтобы не таскать огромный JSON с маршрутами
        '--step', 'lkh_v21_minsum',
        '--seed', str(SEED),
        '--time-budget-ms', str(budget_ms),
    ]
    print(f'  → {instance_path.name} budget={budget_s}s ...', flush=True)
    process = subprocess.run(
        args,
        cwd=str(instance_path.parent),
        capture_output=True,
        text=True,
        encoding='utf-8',
        errors='replace',
    )
    if process.returncode != 0:
        return {'error': process.stderr.strip()[:500]}

    out = json.loads(process.stdout)
    metadata = out.get('steps', [{}])[0].get('metadata') or out.get('metadata') or {}

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

    return {
        'objective': _num(out.get('objective', 0.0)),
        'time_s': _num(out.get('time', 0.0)),
        'valid': bool(out.get('valid', False)),
        'final_minsum': _num(metadata.get('final_minsum', 0.0)),
        'final_max': _num(metadata.get('final_max', 0.0)),
        'alns_iters': _int(metadata.get('alns_iters', 0)),
        'alns_accepts': _int(metadata.get('alns_accepts', 0)),
    }


def main():
    if not EXE.exists():
        sys.stderr.write(f'mtsp.exe not found: {EXE}\n')
        sys.exit(1)

    rows = []
    for inst_name in INSTANCES:
        path = DATA / inst_name
        if not path.exists():
            sys.stderr.write(f'instance not found: {path}\n')
            continue
        print(f'\n=== Инстанс: {inst_name} ===')
        for budget in BUDGETS_S:
            result = run_one(path, budget)
            if 'error' in result:
                print(f'    ERROR: {result["error"][:200]}')
                rows.append({
                    'instance': inst_name,
                    'budget_s': budget,
                    'valid': False,
                    'error': result['error'][:200],
                })
                continue
            row = {
                'instance': inst_name,
                'budget_s': budget,
                'valid': result['valid'],
                'minsum': result['final_minsum'] or result['objective'],
                'makespan': result['final_max'],
                't_actual_s': result['time_s'],
                'alns_iters': result['alns_iters'],
                'alns_accepts': result['alns_accepts'],
            }
            rows.append(row)
            print(f'    minsum={row["minsum"]:.0f}  max={row["makespan"]:.0f}  '
                  f't={row["t_actual_s"]:.1f}s  iters={row["alns_iters"]}')

    fieldnames = ['instance', 'budget_s', 'valid', 'minsum', 'makespan',
                  't_actual_s', 'alns_iters', 'alns_accepts', 'error']
    with open(OUTPUT_CSV, 'w', encoding='utf-8', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for row in rows:
            for k in fieldnames:
                row.setdefault(k, '')
            w.writerow(row)

    print(f'\n=== Готово: {len(rows)} runs ===')
    print(f'CSV: {OUTPUT_CSV}')


if __name__ == '__main__':
    main()
