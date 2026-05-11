"""
Аггрегация multi-seed HGA-результатов: mean/min/max/std + BCa-CI на makespan и sum.

Читает hga_mtsplib_multiseed.json (16 инстансов × 5 seeds), вычисляет:
- mean, std, cv для makespan и sum
- BCa-95%-CI для среднего makespan/sum (через scipy.stats.bootstrap)

Артефакт: hga_multiseed_stats.csv
"""

import csv
import json
import sys
from pathlib import Path

import numpy as np
from scipy.stats import bootstrap

sys.stdout.reconfigure(encoding='utf-8')

ROOT = Path(__file__).resolve().parents[2]
IN_JSON = ROOT / 'experiments' / 'review_fixes' / 'hga_mtsplib_multiseed.json'
OUT_CSV = ROOT / 'experiments' / 'review_fixes' / 'hga_multiseed_stats.csv'

RNG_SEED = 20260508


def bca_ci(values, n_resamples=10000):
    """BCa 95% CI среднего."""
    arr = np.asarray(values, dtype=float)
    if len(arr) < 3 or arr.std() == 0:
        # ноль-вариация: вернём само значение как точечную оценку
        v = float(arr.mean())
        return v, v
    res = bootstrap(
        (arr,),
        np.mean,
        confidence_level=0.95,
        n_resamples=n_resamples,
        method='BCa',
        random_state=RNG_SEED,
    )
    return float(res.confidence_interval.low), float(res.confidence_interval.high)


def main():
    with open(IN_JSON, encoding='utf-8') as f:
        data = json.load(f)

    rows = []
    for r in data['results']:
        if 'error' in r:
            continue
        seeds = r['seed_results']
        mks = np.array([s['makespan'] for s in seeds])
        sms = np.array([s['sum'] for s in seeds])
        ts = np.array([s['time_s'] for s in seeds])

        mks_lo, mks_hi = bca_ci(mks)
        sm_lo, sm_hi = bca_ci(sms)

        # base + m из имени файла
        fn = r['instance']
        stem = fn[:-4] if fn.endswith('.txt') else fn
        if '_m' in stem:
            base, m_str = stem.rsplit('_m', 1)
            m = int(m_str)
        else:
            base, m = stem, 0

        rows.append({
            'instance': fn,
            'base': base,
            'n': r['n'],
            'm': m,
            'n_seeds': len(seeds),
            'makespan_mean': float(mks.mean()),
            'makespan_std': float(mks.std(ddof=1)) if len(mks) > 1 else 0.0,
            'makespan_cv_pct': 100 * float(mks.std(ddof=1) / mks.mean()) if mks.mean() > 0 and len(mks) > 1 else 0.0,
            'makespan_min': float(mks.min()),
            'makespan_max': float(mks.max()),
            'makespan_bca_lo': mks_lo,
            'makespan_bca_hi': mks_hi,
            'sum_mean': float(sms.mean()),
            'sum_std': float(sms.std(ddof=1)) if len(sms) > 1 else 0.0,
            'sum_cv_pct': 100 * float(sms.std(ddof=1) / sms.mean()) if sms.mean() > 0 and len(sms) > 1 else 0.0,
            'sum_bca_lo': sm_lo,
            'sum_bca_hi': sm_hi,
            'time_s_mean': float(ts.mean()),
        })

    # печатаем
    print(f'{"base":<10} m  n  {"mks_mean":>10}  {"mks_cv%":>8}  {"sum_mean":>10}  {"sum_cv%":>8}')
    for row in rows:
        print(f'{row["base"]:<10} {row["m"]:<2d} {row["n"]:<3d} '
              f'{row["makespan_mean"]:>10.2f}  {row["makespan_cv_pct"]:>8.3f}  '
              f'{row["sum_mean"]:>10.2f}  {row["sum_cv_pct"]:>8.3f}')

    # writeCSV
    fieldnames = list(rows[0].keys())
    with open(OUT_CSV, 'w', encoding='utf-8', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)
    print(f'\nЗаписано: {OUT_CSV}')

    # сводная статистика
    cv_mks = [r['makespan_cv_pct'] for r in rows]
    cv_sum = [r['sum_cv_pct'] for r in rows]
    print(f'\n=== Стабильность HGA по 5 seed-ам (16 ячеек) ===')
    print(f'  makespan cv: mean={np.mean(cv_mks):.3f}%  median={np.median(cv_mks):.3f}%  max={np.max(cv_mks):.3f}%')
    print(f'  sum cv:      mean={np.mean(cv_sum):.3f}%  median={np.median(cv_sum):.3f}%  max={np.max(cv_sum):.3f}%')


if __name__ == '__main__':
    main()
