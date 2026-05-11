"""
Multi-seed-вариант He-Hao MA на 16 mTSPLib-ячейках.

Аналогично HGA-multiseed: 5 внешних seed-ов на инстанс для variance-bounds.
Использует тот же wrapper run_hehao_mtsplib.py, импортирует его функции.

Артефакт: hehao_mtsplib_multiseed.json
"""

import json
import re
import sys
import time
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')
sys.path.insert(0, str(Path(__file__).resolve().parent))

from run_hehao_mtsplib import HEHAO_BIN_WIN, MTSPLIB_DIR, run_hehao  # noqa: E402

OUT_JSON = Path(__file__).resolve().parents[2] / 'experiments' / 'review_fixes' / 'hehao_mtsplib_multiseed.json'

TIME_LIMIT_S = 30
SEEDS = [1, 2, 3, 4, 5]


def main():
    if not HEHAO_BIN_WIN.exists():
        sys.stderr.write(f'He-Hao binary not found: {HEHAO_BIN_WIN}\n')
        sys.exit(1)

    instances = sorted(MTSPLIB_DIR.glob('*.txt'))
    print(f'Multi-seed He-Hao: {len(instances)} инстансов × {len(SEEDS)} seeds × {TIME_LIMIT_S}s')

    results = []
    for i, path in enumerate(instances, 1):
        name = path.stem
        match = re.match(r'^(.+)_m(\d+)$', name)
        if not match:
            continue
        base, m = match.group(1), int(match.group(2))

        seed_results = []
        for seed in SEEDS:
            print(f'  [{i}/{len(instances)}] {path.name} seed={seed} ', end='', flush=True)
            try:
                r = run_hehao(path, base, m, TIME_LIMIT_S, seed)
                print(f'mks={r["makespan"]:.2f} sum={r["sum"]:.2f} t={r["time_s_actual"]:.1f}s')
                seed_results.append({
                    'seed': seed,
                    'makespan': r['makespan'],
                    'sum': r['sum'],
                    'time_s': r['time_s_actual'],
                    'n_routes': r['n_routes'],
                    'recompute_diff_max': r['recompute_diff_max'],
                })
            except Exception as e:
                print(f'ERROR: {e}')
                seed_results.append({'seed': seed, 'error': str(e)})

        # aggregate
        valid = [s for s in seed_results if 'error' not in s]
        if valid:
            mks = [s['makespan'] for s in valid]
            sms = [s['sum'] for s in valid]
            results.append({
                'instance': path.name,
                'n': r['n'] if 'r' in dir() else 0,
                'm': m,
                'n_seeds': len(valid),
                'makespan_mean': sum(mks) / len(mks),
                'makespan_min': min(mks),
                'makespan_max': max(mks),
                'sum_mean': sum(sms) / len(sms),
                'sum_min': min(sms),
                'sum_max': max(sms),
                'seed_results': seed_results,
            })
        else:
            results.append({'instance': path.name, 'm': m, 'error': 'all seeds failed', 'seed_results': seed_results})

    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT_JSON, 'w', encoding='utf-8') as f:
        json.dump({'time_limit_s': TIME_LIMIT_S, 'seeds': SEEDS, 'results': results}, f, indent=2)
    print(f'\nSaved: {OUT_JSON}')


if __name__ == '__main__':
    main()
