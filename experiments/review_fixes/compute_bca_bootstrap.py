"""
BCa-bootstrap recomputation for tables 1а and 4б.

Replaces percentile bootstrap CIs with bias-corrected and accelerated (BCa) CIs
following DiCiccio & Efron (Stat. Sci. 11(3), 1996, DOI 10.1214/ss/1032280214).

BCa is second-order accurate (O(1/n) coverage error vs O(1/sqrt(n)) for percentile).
Important when the bootstrap distribution of the statistic is skewed, which is our case
(Shapiro p < 0.02 for all 4 paired-difference distributions).

Outputs CSV with: comparison, n_pairs, mean, percentile_lo, percentile_hi, bca_lo, bca_hi.
"""

import csv
import json
import sys
from pathlib import Path

import numpy as np
from scipy import stats

sys.stdout.reconfigure(encoding='utf-8')
csv.field_size_limit(2**31 - 1)

ROOT = Path(__file__).resolve().parent.parent.parent
RES = ROOT / 'data' / 'results'
FAIR_PATH = ROOT / 'experiments' / 'review_fixes' / 'fair_lkh3_results.csv'

RNG_SEED = 20251205  # фиксированный для воспроизводимости


def load_csv(path):
    with open(path, encoding='utf-8') as f:
        return list(csv.DictReader(f))


def stratum1_v21_minsum_objectives():
    """Возвращает {(family/instance, m): obj} для всех валидных запусков alns_minsum (lkh_v21_minsum)
    и lkh3-baseline на страт-1."""
    files = [
        RES / 'stratum1_modular_n100_200_results.csv',
        RES / 'stratum1_modular_n500_1000_results.csv',
    ]
    by_solver = {}  # solver -> {key: obj}
    for fp in files:
        if not fp.exists():
            continue
        for r in load_csv(fp):
            if r.get('valid', '').lower() != 'true':
                continue
            try:
                obj = float(r['objective'])
            except Exception:
                continue
            family = r['instance_family']
            inst = r['instance']
            key = f'{family}/{inst}/m={r["salesman_count"]}'
            solver = r['solver']
            by_solver.setdefault(solver, {})[key] = obj
    return by_solver


def fair_lkh3_objectives():
    """Возвращает {mode: {key: obj_in_v21_units}} (×1000 для согласованности шкалы)."""
    by_mode = {'minsum_default': {}, 'minsum_balanced': {}, 'minmax_balanced': {}}
    if not FAIR_PATH.exists():
        return by_mode
    for r in load_csv(FAIR_PATH):
        if r.get('valid', '').lower() != 'true':
            continue
        inst = r['instance']
        if inst.startswith('clustered-center_'):
            family = 'clustered-center'
        elif inst.startswith('uniform_'):
            family = 'uniform'
        else:
            family = 'uniform'
        key = f'{family}/{inst}/m={r["m"]}'
        mode = r['mode']
        if mode in by_mode:
            by_mode[mode][key] = float(r['sum']) * 1000.0  # match v21 units
    return by_mode


def compute_paired_relative(v21_obj, baseline_obj):
    """Возвращает массив парных относительных разностей в %: (v21 - baseline) / baseline * 100."""
    common = sorted(set(v21_obj.keys()) & set(baseline_obj.keys()))
    diffs = []
    for k in common:
        v = v21_obj[k]
        b = baseline_obj[k]
        if b > 0:
            diffs.append(100.0 * (v - b) / b)
    return np.array(diffs, dtype=float)


def bootstrap_ci(diffs, method='BCa', n_resamples=10000, ci=0.95, seed=RNG_SEED):
    """Возвращает (mean, lo, hi) для одного образца с указанным методом."""
    if len(diffs) < 5:
        m = float(np.mean(diffs)) if len(diffs) else 0.0
        return m, m, m
    rng = np.random.default_rng(seed)
    res = stats.bootstrap(
        (diffs,),
        statistic=np.mean,
        n_resamples=n_resamples,
        method=method,
        confidence_level=ci,
        rng=rng,
    )
    return float(np.mean(diffs)), float(res.confidence_interval.low), float(res.confidence_interval.high)


def main():
    print('=== Загрузка данных ===')
    strat1 = stratum1_v21_minsum_objectives()
    print(f'Решатели на страт-1: {sorted(strat1.keys())}')

    fair = fair_lkh3_objectives()
    print(f'Fair-LKH-3 modes: {[(k, len(v)) for k, v in fair.items()]}')

    rows = []

    # === Таблица 1а: 4 v21-варианта vs lkh3-baseline (default-MINSUM на страт-1) ===
    print('\n=== Таблица 1а: BCa CIs ===')
    lkh3_default = strat1.get('lkh3-baseline', {})
    v21_variants = ['lkh_v21_minsum', 'lkh_v21_minsum_cap',
                    'lkh_v21_minsum_depot2m_plus', 'lkh-wrapper-v21']

    for v in v21_variants:
        v21 = strat1.get(v, {})
        diffs = compute_paired_relative(v21, lkh3_default)
        if len(diffs) == 0:
            continue
        m, perc_lo, perc_hi = bootstrap_ci(diffs, method='percentile')
        m2, bca_lo, bca_hi = bootstrap_ci(diffs, method='BCa')
        print(f'{v:35s} n={len(diffs)}  mean={m:+.3f}%  '
              f'perc=[{perc_lo:+.3f}, {perc_hi:+.3f}]  BCa=[{bca_lo:+.3f}, {bca_hi:+.3f}]')
        rows.append({
            'table': '1а',
            'comparison': f'{v} vs lkh3-baseline (default-MINSUM)',
            'n_pairs': len(diffs),
            'mean': m,
            'perc_lo': perc_lo,
            'perc_hi': perc_hi,
            'bca_lo': bca_lo,
            'bca_hi': bca_hi,
        })

    # === Таблица 4б: alns_minsum vs LKH-3 в трёх режимах (fair-baseline на страт-1) ===
    print('\n=== Таблица 4б: BCa CIs ===')
    v21_minsum = strat1.get('lkh_v21_minsum', {})
    for mode, lkh_data in fair.items():
        diffs = compute_paired_relative(v21_minsum, lkh_data)
        if len(diffs) == 0:
            continue
        m, perc_lo, perc_hi = bootstrap_ci(diffs, method='percentile')
        m2, bca_lo, bca_hi = bootstrap_ci(diffs, method='BCa')
        print(f'{mode:25s} n={len(diffs)}  mean={m:+.2f}%  '
              f'perc=[{perc_lo:+.2f}, {perc_hi:+.2f}]  BCa=[{bca_lo:+.2f}, {bca_hi:+.2f}]')
        rows.append({
            'table': '4б',
            'comparison': f'lkh_v21_minsum vs LKH-3 ({mode})',
            'n_pairs': len(diffs),
            'mean': m,
            'perc_lo': perc_lo,
            'perc_hi': perc_hi,
            'bca_lo': bca_lo,
            'bca_hi': bca_hi,
        })

    # Сохраняем CSV
    out = ROOT / 'experiments' / 'review_fixes' / 'bca_bootstrap_results.csv'
    with open(out, 'w', encoding='utf-8', newline='') as f:
        if rows:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)

    # Сохраняем JSON для отчёта
    out_json = ROOT / 'experiments' / 'review_fixes' / 'bca_bootstrap_results.json'
    with open(out_json, 'w', encoding='utf-8') as f:
        json.dump({'rng_seed': RNG_SEED, 'n_resamples': 10000, 'rows': rows}, f, indent=2, ensure_ascii=False)

    print(f'\nЗаписано: {out}')
    print(f'JSON: {out_json}')
    print(f'Total: {len(rows)} comparisons')


if __name__ == '__main__':
    main()
