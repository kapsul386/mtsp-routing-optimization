"""
Парный Wilcoxon signed-rank + BCa-bootstrap для ALNS vs He-Hao MA на mTSPLib.

Источники:
  hehao_mtsplib_results.json (single-seed He-Hao, 16 ячеек)
  mtsplib_v21_results.csv (alns_minsum, alns_minsum_cap, alns_minmax — 5 seeds)

Для каждой ячейки берём mean ALNS-результата по 5 seed-ам, парим с He-Hao.
Вычисляем для каждого ALNS-варианта:
  - Wilcoxon p (two-sided)
  - Holm-corrected p
  - BCa 95% CI среднего Δ%
  - Cliff's δ
  - Cohen's d

Метрики: ΔMINSUM (по sum) и ΔMAKESPAN — отдельно.

Артефакт: alns_vs_hehao_stats.csv
"""

import csv
import json
import sys
from pathlib import Path

import numpy as np
from scipy.stats import bootstrap, wilcoxon

sys.stdout.reconfigure(encoding='utf-8')

ROOT = Path(__file__).resolve().parents[2]
HEHAO_JSON = ROOT / 'experiments' / 'review_fixes' / 'hehao_mtsplib_results.json'
V21_CSV = ROOT / 'experiments' / 'review_fixes' / 'mtsplib_v21_results.csv'
OUT_CSV = ROOT / 'experiments' / 'review_fixes' / 'alns_vs_hehao_stats.csv'

RNG_SEED = 20260508


def load_hehao():
    """{(base, m): {sum, makespan}}"""
    with open(HEHAO_JSON, encoding='utf-8') as f:
        data = json.load(f)
    out = {}
    for r in data['results']:
        if 'error' in r:
            continue
        fn = r['instance']
        stem = fn[:-4] if fn.endswith('.txt') else fn
        if '_m' not in stem:
            continue
        base, m_str = stem.rsplit('_m', 1)
        m = int(m_str)
        out[(base, m)] = {'sum': float(r['sum']), 'makespan': float(r['makespan'])}
    return out


def load_v21(solver_name):
    """{(base, m): [{sum, makespan}, ...]}"""
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
            })
    return d


def cliffs_delta(a, b):
    """Cliff's δ = (#(a < b) - #(a > b)) / (n_a × n_b). Sign convention: negative = a < b typically."""
    n_a, n_b = len(a), len(b)
    if n_a == 0 or n_b == 0:
        return 0.0
    less = sum(1 for x in a for y in b if x < y)
    more = sum(1 for x in a for y in b if x > y)
    return (less - more) / (n_a * n_b)


def cohen_d(deltas):
    arr = np.asarray(deltas, dtype=float)
    if len(arr) < 2 or arr.std(ddof=1) == 0:
        return 0.0
    return float(arr.mean() / arr.std(ddof=1))


def bca_ci(values, n_resamples=10000):
    arr = np.asarray(values, dtype=float)
    if len(arr) < 3 or arr.std() == 0:
        v = float(arr.mean())
        return v, v
    res = bootstrap((arr,), np.mean, confidence_level=0.95, n_resamples=n_resamples,
                    method='BCa', random_state=RNG_SEED)
    return float(res.confidence_interval.low), float(res.confidence_interval.high)


def compare(alns_data, hehao, label, metric):
    """Returns dict with all stats for one ALNS variant on one metric (sum or makespan)."""
    keys = sorted(set(alns_data.keys()) & set(hehao.keys()))
    deltas_pct = []
    alns_vals, hehao_vals = [], []
    n_alns_better = 0

    for k in keys:
        h = hehao[k][metric]
        a_runs = [r[metric] for r in alns_data[k]]
        a_mean = np.mean(a_runs)
        if h <= 0:
            continue
        delta_pct = 100 * (a_mean - h) / h
        deltas_pct.append(delta_pct)
        alns_vals.append(a_mean)
        hehao_vals.append(h)
        if a_mean < h:
            n_alns_better += 1

    deltas_arr = np.array(deltas_pct)
    if len(deltas_arr) >= 3:
        try:
            wstat, p = wilcoxon(alns_vals, hehao_vals, alternative='two-sided')
        except ValueError:
            wstat, p = 0, 1.0
    else:
        wstat, p = 0, 1.0
    lo, hi = bca_ci(deltas_arr)
    d = cliffs_delta(alns_vals, hehao_vals)
    coh = cohen_d(deltas_arr)

    return {
        'solver': label,
        'metric': metric,
        'n_cells': len(deltas_arr),
        'mean_delta_pct': float(deltas_arr.mean()) if len(deltas_arr) else 0,
        'median_delta_pct': float(np.median(deltas_arr)) if len(deltas_arr) else 0,
        'bca_lo_pct': lo,
        'bca_hi_pct': hi,
        'wilcoxon_p': float(p),
        'cliffs_delta': float(d),
        'cohen_d': coh,
        'alns_better_count': n_alns_better,
        'alns_better_total': len(deltas_arr),
    }


def holm_correct(rows, p_field='wilcoxon_p'):
    """In-place Holm-Bonferroni correction."""
    pairs = [(i, r[p_field]) for i, r in enumerate(rows)]
    pairs.sort(key=lambda x: x[1])
    n = len(pairs)
    for rank, (idx, p) in enumerate(pairs):
        adj = min(1.0, p * (n - rank))
        if rank > 0:
            prev_idx, _ = pairs[rank - 1]
            adj = max(adj, rows[prev_idx]['holm_p_adj'])
        rows[idx]['holm_p_adj'] = adj


def main():
    hehao = load_hehao()
    print(f'He-Hao: {len(hehao)} ячеек')

    variants = ['lkh_v21_minsum', 'lkh_v21_minsum_cap', 'lkh_v21_minmax']
    rows = []

    for v in variants:
        alns = load_v21(v)
        if not alns:
            continue
        for metric in ['sum', 'makespan']:
            row = compare(alns, hehao, v, metric)
            rows.append(row)

    # Holm-coррекция отдельно для sum и для makespan (3 сравнения каждое)
    sum_rows = [r for r in rows if r['metric'] == 'sum']
    max_rows = [r for r in rows if r['metric'] == 'makespan']
    holm_correct(sum_rows)
    holm_correct(max_rows)

    print(f'\n{"solver":<22} {"metric":<10} {"mean Δ%":>9} {"med Δ%":>9} '
          f'{"BCa lo":>8} {"BCa hi":>8} {"p":>10} {"p_adj":>10} {"δ":>7} {"d":>7} {"win":>7}')
    for r in rows:
        print(f'{r["solver"]:<22} {r["metric"]:<10} {r["mean_delta_pct"]:+9.2f} {r["median_delta_pct"]:+9.2f} '
              f'{r["bca_lo_pct"]:+8.2f} {r["bca_hi_pct"]:+8.2f} '
              f'{r["wilcoxon_p"]:10.3e} {r["holm_p_adj"]:10.3e} '
              f'{r["cliffs_delta"]:+7.3f} {r["cohen_d"]:+7.3f} '
              f'{r["alns_better_count"]}/{r["alns_better_total"]}')

    fieldnames = list(rows[0].keys())
    OUT_CSV.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT_CSV, 'w', encoding='utf-8', newline='') as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        w.writerows(rows)
    print(f'\nЗаписано: {OUT_CSV}')


if __name__ == '__main__':
    main()
