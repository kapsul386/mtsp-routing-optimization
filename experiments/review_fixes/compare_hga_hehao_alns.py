"""
3-сторонне сравнение на mTSPLib: HGA (Mahmoudinazlou-Kwon) vs He-Hao MA vs ALNS-mTSP-варианты.

Источники:
  hga_mtsplib_results.json — HGA single-run
  hehao_mtsplib_results.json — He-Hao single-seed
  mtsplib_v21_results.csv — alns_minsum, alns_minsum_cap, alns_minmax (5 seeds each)

Output: hga_hehao_alns_summary.csv + console-сводка.
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
HEHAO_JSON = ROOT / 'experiments' / 'review_fixes' / 'hehao_mtsplib_results.json'
V21_CSV = ROOT / 'experiments' / 'review_fixes' / 'mtsplib_v21_results.csv'
OUT_CSV = ROOT / 'experiments' / 'review_fixes' / 'hga_hehao_alns_summary.csv'


def parse_base_m(stem):
    if '_m' not in stem:
        return None, None
    base, m_str = stem.rsplit('_m', 1)
    try:
        return base, int(m_str)
    except ValueError:
        return None, None


def load_json_results(path, key_map):
    """Загружает JSON с results=[{instance, ...}]; возвращает {(base,m): {key: value}}."""
    if not path.exists():
        return {}
    with open(path, encoding='utf-8') as f:
        data = json.load(f)
    out = {}
    for r in data.get('results', []):
        if 'error' in r:
            continue
        fn = r.get('instance', '')
        stem = fn[:-4] if fn.endswith('.txt') else fn
        base, m = parse_base_m(stem)
        if base is None:
            continue
        out[(base, m)] = {k: r.get(v) for k, v in key_map.items()}
    return out


def load_v21(solver_name):
    d = {}
    with open(V21_CSV, encoding='utf-8') as f:
        for r in csv.DictReader(f):
            if r['solver'] != solver_name or r.get('valid', '').lower() != 'true':
                continue
            base = r['base']
            m = int(r['m'])
            d.setdefault((base, m), []).append({
                'sum': float(r['sum']),
                'makespan': float(r['makespan']),
                'time_s': float(r['time_seconds']),
            })
    return d


def main():
    hga = load_json_results(HGA_JSON, {'sum': 'sum', 'makespan': 'makespan', 'time_s': 'time_s', 'n': 'n'})
    hehao = load_json_results(HEHAO_JSON, {'sum': 'sum', 'makespan': 'makespan', 'time_s': 'time_s_actual', 'n': 'n'})
    print(f'Loaded: HGA {len(hga)} ячеек, He-Hao {len(hehao)} ячеек')

    if not hehao:
        print('He-Hao results not yet available')
        return

    alns_minsum = load_v21('lkh_v21_minsum')
    alns_minmax = load_v21('lkh_v21_minmax')

    # Сравнение HGA vs He-Hao на пересечении ячеек
    keys = sorted(set(hga.keys()) & set(hehao.keys()))
    print(f'\n=== HGA vs He-Hao MA на {len(keys)} ячейках ===')
    print(f'{"base":10s} {"m":>2s}  {"hga_sum":>10s}  {"hehao_sum":>10s}  {"Δsum%":>7s}  '
          f'{"hga_max":>10s}  {"hehao_max":>10s}  {"Δmax%":>7s}  {"hga_t":>5s}  {"hehao_t":>7s}')

    rows = []
    for k in keys:
        base, m = k
        h, e = hga[k], hehao[k]
        hsum, hmax = float(h['sum']), float(h['makespan'])
        esum, emax = float(e['sum']), float(e['makespan'])
        dsum = 100 * (esum - hsum) / hsum if hsum > 0 else 0
        dmax = 100 * (emax - hmax) / hmax if hmax > 0 else 0
        print(f'{base:10s} {m:>2d}  {hsum:>10.2f}  {esum:>10.2f}  {dsum:>+7.2f}  '
              f'{hmax:>10.2f}  {emax:>10.2f}  {dmax:>+7.2f}  {float(h["time_s"]):>5.1f}  {float(e["time_s"]):>7.1f}')

        # ALNS-MINSUM mean
        a_minsum = alns_minsum.get(k, [])
        a_minmax = alns_minmax.get(k, [])
        am_sum = np.mean([x['sum'] for x in a_minsum]) if a_minsum else None
        am_max = np.mean([x['makespan'] for x in a_minsum]) if a_minsum else None
        ax_sum = np.mean([x['sum'] for x in a_minmax]) if a_minmax else None
        ax_max = np.mean([x['makespan'] for x in a_minmax]) if a_minmax else None

        row = {
            'base': base, 'n': h['n'], 'm': m,
            'hga_sum': hsum, 'hga_makespan': hmax, 'hga_time_s': float(h['time_s']),
            'hehao_sum': esum, 'hehao_makespan': emax, 'hehao_time_s': float(e['time_s']),
            'delta_sum_hehao_vs_hga_pct': dsum,
            'delta_makespan_hehao_vs_hga_pct': dmax,
            'alns_minsum_mean_sum': am_sum,
            'alns_minsum_mean_makespan': am_max,
            'alns_minmax_mean_sum': ax_sum,
            'alns_minmax_mean_makespan': ax_max,
        }
        rows.append(row)

    if rows:
        # сводка по HGA-vs-He-Hao
        s = np.array([r['delta_sum_hehao_vs_hga_pct'] for r in rows])
        m = np.array([r['delta_makespan_hehao_vs_hga_pct'] for r in rows])
        wins_sum = sum(1 for r in rows if r['delta_sum_hehao_vs_hga_pct'] < 0)
        wins_max = sum(1 for r in rows if r['delta_makespan_hehao_vs_hga_pct'] < 0)
        print(f'\n  HGA vs He-Hao по~SUM: mean={s.mean():+.2f}%  median={np.median(s):+.2f}%  He-Hao лучше в {wins_sum}/{len(rows)}')
        print(f'  HGA vs He-Hao по~MAX: mean={m.mean():+.2f}%  median={np.median(m):+.2f}%  He-Hao лучше в {wins_max}/{len(rows)}')

        # write CSV
        fieldnames = list(rows[0].keys())
        with open(OUT_CSV, 'w', encoding='utf-8', newline='') as f:
            w = csv.DictWriter(f, fieldnames=fieldnames)
            w.writeheader()
            w.writerows(rows)
        print(f'\nЗаписано: {OUT_CSV}')


if __name__ == '__main__':
    main()
