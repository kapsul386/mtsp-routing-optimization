"""
High-m sweep на N=10K для подтверждения H5 (преимущество ALNS-mTSP растёт с m).

Запускает alns_minsum на 4 family × 5 m-значениях = 20 инстансов:
  family ∈ {uniform, clustered-center, clustered-offset-depot, mixed-outliers}
  m ∈ {10, 30, 50, 80, 100}
  N = 10000

Time-budget = 70s/инстанс (как в основной сетке для $N \\leq 10K$).

Артефакт: experiments/review_fixes/high_m_sweep_results.csv
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

FAMILIES = ['uniform', 'clustered-center', 'clustered-offset-depot', 'mixed-outliers']
M_VALUES = [10, 30, 50, 80, 100]
TIME_BUDGET_MS = 70_000
SEED = 1

OUTPUT_CSV = ROOT / 'experiments' / 'review_fixes' / 'high_m_sweep_results.csv'


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


def run_one(inst_path: Path, m: int) -> dict:
    args = [
        str(EXE),
        '--input-file', inst_path.name,
        '--emit-routes', 'false',
        '--step', 'lkh_v21_minsum',
        '--seed', str(SEED),
        '--time-budget-ms', str(TIME_BUDGET_MS),
    ]
    print(f'  → {inst_path.name} (m={m}) ...', flush=True)
    proc = subprocess.run(
        args, cwd=str(inst_path.parent),
        capture_output=True, text=True,
        encoding='utf-8', errors='replace',
        timeout=TIME_BUDGET_MS // 1000 + 60,
    )
    if proc.returncode != 0:
        return {'error': proc.stderr.strip()[:300]}
    out = json.loads(proc.stdout)
    metadata = out.get('steps', [{}])[0].get('metadata') or out.get('metadata') or {}
    return {
        'minsum': _num(metadata.get('final_minsum') or out.get('objective')),
        'makespan': _num(metadata.get('final_max')),
        'gini': _num(metadata.get('final_gini')),
        'time_s': _num(out.get('time')),
        'valid': bool(out.get('valid')),
        'alns_iters': _int(metadata.get('alns_iters')),
    }


def main():
    rows = []
    total = len(FAMILIES) * len(M_VALUES)
    i = 0
    for fam in FAMILIES:
        for m in M_VALUES:
            i += 1
            inst_name = f'{fam}_n10000_m{m}_r01.txt'
            path = DATA / inst_name
            if not path.exists():
                print(f'  [{i}/{total}] {inst_name} НЕ НАЙДЕН')
                continue
            print(f'  [{i}/{total}] ', end='')
            try:
                r = run_one(path, m)
                if 'error' in r:
                    print(f'    ERROR: {r["error"][:200]}')
                    rows.append({'family': fam, 'n': 10000, 'm': m, 'instance': inst_name,
                                 'valid': False, 'error': r['error'][:200]})
                    continue
                row = {
                    'family': fam, 'n': 10000, 'm': m, 'instance': inst_name,
                    'minsum': r['minsum'], 'makespan': r['makespan'], 'gini': r['gini'],
                    'time_s': r['time_s'], 'valid': r['valid'], 'alns_iters': r['alns_iters'],
                }
                rows.append(row)
                print(f'    minsum={row["minsum"]:.0f}  max={row["makespan"]:.0f}  '
                      f'gini={row["gini"]:.3f}  t={row["time_s"]:.1f}s')
            except Exception as e:
                print(f'    EXCEPTION: {e}')
                rows.append({'family': fam, 'n': 10000, 'm': m, 'instance': inst_name,
                             'valid': False, 'error': str(e)[:200]})

    fieldnames = ['family', 'n', 'm', 'instance', 'valid', 'minsum', 'makespan',
                  'gini', 'time_s', 'alns_iters', 'error']
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
