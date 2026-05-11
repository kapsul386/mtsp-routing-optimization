"""
Сравнение HGA (Mahmoudinazlou-Kwon, CO&R 2024) с alns_minsum на 16 mTSPLib-инстансах.

Для каждой $(base, m)$-ячейки берётся среднее по seed-ам v21-результата
и сопоставляется с одним HGA-прогоном (HGA имеет внутри multi-run, нам это видно
как один stable result).

Главные метрики сравнения:
  - makespan (HGA оптимизирует MINMAX, поэтому это его native objective);
  - sum (MINSUM, native у v21);
  - время работы.

Результат: experiments/review_fixes/hga_vs_v21_mtsplib.csv
"""

import csv
import json
import sys
from pathlib import Path

import numpy as np

sys.stdout.reconfigure(encoding='utf-8')
csv.field_size_limit(2**31 - 1)

ROOT = Path(__file__).resolve().parents[2]
HGA_JSON = ROOT / 'experiments' / 'review_fixes' / 'hga_mtsplib_results.json'
V21_CSV = ROOT / 'experiments' / 'review_fixes' / 'mtsplib_v21_results.csv'
OUT_CSV = ROOT / 'experiments' / 'review_fixes' / 'hga_vs_v21_mtsplib.csv'


def load_v21(solver_name):
    """Возвращает {(base, m): [{seed, sum, makespan, time}, ...]} для указанного решателя."""
    d = {}
    with open(V21_CSV, encoding='utf-8') as f:
        for r in csv.DictReader(f):
            if r['solver'] != solver_name or r.get('valid', '').lower() != 'true':
                continue
            base = r['base']
            m = int(r['m'])
            key = (base, m)
            d.setdefault(key, []).append({
                'seed': int(r['seed']),
                'sum': float(r['sum']),
                'makespan': float(r['makespan']),
                'time_s': float(r['time_seconds']),
            })
    return d


def load_hga():
    """Возвращает {(base, m): {sum, makespan, time}}."""
    with open(HGA_JSON, encoding='utf-8') as f:
        data = json.load(f)
    out = {}
    for r in data['results']:
        if 'error' in r:
            continue
        # filename: 'eil51_m2.txt' -> base='eil51', m=2
        fn = r['instance']
        if not fn.endswith('.txt'):
            continue
        stem = fn[:-4]
        if '_m' not in stem:
            continue
        base, m_str = stem.rsplit('_m', 1)
        m = int(m_str)
        out[(base, m)] = {
            'sum': float(r['sum']),
            'makespan': float(r['makespan']),
            'time_s': float(r['time_s']),
            'n': int(r['n']),
        }
    return out


def compare_one(v21, hga, label):
    print(f'\n=== {label} vs HGA ===')
    print(f'{"base":10s} {"m":3s} {"v21_sum":>11s} {"hga_sum":>11s} {"Δsum%":>7s}  '
          f'{"v21_max":>11s} {"hga_max":>11s} {"Δmax%":>7s}  {"v21_t":>6s} {"hga_t":>6s}')
    rows = []
    keys = sorted(set(v21.keys()) & set(hga.keys()))
    for k in keys:
        base, m = k
        v_sums = np.array([x['sum'] for x in v21[k]])
        v_maxs = np.array([x['makespan'] for x in v21[k]])
        v_times = np.array([x['time_s'] for x in v21[k]])
        h = hga[k]
        v_sum = v_sums.mean()
        v_max = v_maxs.mean()
        d_sum = 100 * (v_sum - h['sum']) / h['sum'] if h['sum'] > 0 else 0
        d_max = 100 * (v_max - h['makespan']) / h['makespan'] if h['makespan'] > 0 else 0
        print(f'{base:10s} {m:3d} {v_sum:11.2f} {h["sum"]:11.2f} {d_sum:+7.2f}  '
              f'{v_max:11.2f} {h["makespan"]:11.2f} {d_max:+7.2f}  '
              f'{v_times.mean():6.1f} {h["time_s"]:6.1f}')
        rows.append({
            'solver': label,
            'base': base,
            'n': h['n'],
            'm': m,
            'v21_sum_mean': v_sum,
            'v21_makespan_mean': v_max,
            'v21_seeds': len(v21[k]),
            'v21_time_s_mean': v_times.mean(),
            'hga_sum': h['sum'],
            'hga_makespan': h['makespan'],
            'hga_time_s': h['time_s'],
            'delta_sum_pct': d_sum,
            'delta_makespan_pct': d_max,
        })
    if rows:
        sums = np.array([r['delta_sum_pct'] for r in rows])
        maxs = np.array([r['delta_makespan_pct'] for r in rows])
        v_wins_sum = sum(1 for r in rows if r['delta_sum_pct'] < 0)
        v_wins_max = sum(1 for r in rows if r['delta_makespan_pct'] < 0)
        print(f'  ΔSUM:      mean={sums.mean():+.2f}%   median={np.median(sums):+.2f}%   '
              f'v21 побед: {v_wins_sum}/{len(rows)}')
        print(f'  ΔMAKESPAN: mean={maxs.mean():+.2f}%   median={np.median(maxs):+.2f}%   '
              f'v21 побед: {v_wins_max}/{len(rows)}')
    return rows


def main():
    hga = load_hga()
    all_rows = []
    for solver in ['lkh_v21_minsum', 'lkh_v21_minsum_cap', 'lkh_v21_minmax']:
        v21 = load_v21(solver)
        if not v21:
            continue
        all_rows.extend(compare_one(v21, hga, solver))

    if all_rows:
        fieldnames = list(all_rows[0].keys())
        with open(OUT_CSV, 'w', encoding='utf-8', newline='') as f:
            w = csv.DictWriter(f, fieldnames=fieldnames)
            w.writeheader()
            w.writerows(all_rows)
        print(f'\nЗаписано: {OUT_CSV}')


if __name__ == '__main__':
    main()
