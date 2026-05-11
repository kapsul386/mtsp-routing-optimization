"""
Pareto-plot ALNS / HGA / He-Hao MA на 16 mTSPLib-ячейках.

Двухпанельный график:
  (a) sum vs makespan для всех решателей и всех ячеек (точки разных цветов)
  (b) то же, но нормированное на (HGA sum, HGA makespan) — каждая точка относительно HGA-эталона

Результат: data/results/figures/fig_pareto_mtsplib.png
"""

import csv
import json
import sys
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

sys.stdout.reconfigure(encoding='utf-8')
csv.field_size_limit(2**31 - 1)

ROOT = Path(__file__).resolve().parents[2]
HGA_JSON = ROOT / 'experiments' / 'review_fixes' / 'hga_mtsplib_results.json'
HEHAO_JSON = ROOT / 'experiments' / 'review_fixes' / 'hehao_mtsplib_results.json'
V21_CSV = ROOT / 'experiments' / 'review_fixes' / 'mtsplib_v21_results.csv'
OUT = ROOT / 'data' / 'results' / 'figures' / 'fig_pareto_mtsplib.png'


def load_json(path):
    with open(path, encoding='utf-8') as f:
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
        out[(base, int(m_str))] = {'sum': float(r['sum']), 'makespan': float(r['makespan'])}
    return out


def load_v21(solver):
    """{(base, m): mean dict}"""
    raw = {}
    with open(V21_CSV, encoding='utf-8') as f:
        for r in csv.DictReader(f):
            if r['solver'] != solver or r.get('valid', '').lower() != 'true':
                continue
            base, m = r['base'], int(r['m'])
            raw.setdefault((base, m), []).append({'sum': float(r['sum']), 'makespan': float(r['makespan'])})
    out = {}
    for k, runs in raw.items():
        out[k] = {
            'sum': np.mean([r['sum'] for r in runs]),
            'makespan': np.mean([r['makespan'] for r in runs]),
        }
    return out


SOLVERS = [
    ('alns_minsum',     'lkh_v21_minsum',     'tab:blue',     'o', 'load_v21'),
    ('alns_minsum_cap', 'lkh_v21_minsum_cap', 'tab:cyan',     's', 'load_v21'),
    ('alns_minmax',     'lkh_v21_minmax',     'tab:green',    '^', 'load_v21'),
    ('HGA',             None,                 'tab:red',      'D', 'hga'),
    ('He-Hao MA',       None,                 'tab:purple',   '*', 'hehao'),
]


def main():
    hga = load_json(HGA_JSON)
    hehao = load_json(HEHAO_JSON)
    minsum = load_v21('lkh_v21_minsum')
    minsum_cap = load_v21('lkh_v21_minsum_cap')
    minmax = load_v21('lkh_v21_minmax')

    data = {
        'alns_minsum': minsum,
        'alns_minsum_cap': minsum_cap,
        'alns_minmax': minmax,
        'HGA': hga,
        'He-Hao MA': hehao,
    }

    keys = sorted(set(hga.keys()) & set(hehao.keys()) & set(minsum.keys()) & set(minmax.keys()))
    print(f'Общие ячейки: {len(keys)}')

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13.5, 5.4))

    # Panel (a): absolute sum vs makespan, log-scaled
    for label, _, color, marker, _ in SOLVERS:
        d = data[label]
        xs = [d[k]['makespan'] for k in keys]
        ys = [d[k]['sum'] for k in keys]
        ax1.scatter(xs, ys, c=color, marker=marker, s=70, label=label, alpha=0.75, edgecolors='black', linewidths=0.5)

    ax1.set_xlabel('makespan (max route length)', fontsize=11)
    ax1.set_ylabel('sum (total route length)', fontsize=11)
    ax1.set_xscale('log')
    ax1.set_yscale('log')
    ax1.set_title('(a) MINSUM vs makespan, абсолютные значения', fontsize=11)
    ax1.grid(True, which='both', alpha=0.3, linestyle='--')
    ax1.legend(loc='upper left', fontsize=9, ncol=1)

    # Panel (b): normalized to HGA per cell
    for label, _, color, marker, _ in SOLVERS:
        if label == 'HGA':
            continue  # HGA is the reference (1.0, 1.0)
        d = data[label]
        xs, ys = [], []
        for k in keys:
            h = hga[k]
            xs.append(d[k]['makespan'] / h['makespan'])
            ys.append(d[k]['sum'] / h['sum'])
        ax2.scatter(xs, ys, c=color, marker=marker, s=70, label=label, alpha=0.75, edgecolors='black', linewidths=0.5)

    # HGA-точка (1, 1)
    ax2.scatter([1], [1], c='tab:red', marker='D', s=100, label='HGA (reference)', zorder=10, edgecolors='black', linewidths=0.7)
    # axis lines at 1.0
    ax2.axhline(1, color='tab:red', linewidth=0.7, alpha=0.4, linestyle='-')
    ax2.axvline(1, color='tab:red', linewidth=0.7, alpha=0.4, linestyle='-')

    ax2.set_xlabel('makespan / HGA makespan', fontsize=11)
    ax2.set_ylabel('sum / HGA sum', fontsize=11)
    ax2.set_xscale('log')
    ax2.set_title('(b) Нормировано на HGA-эталон', fontsize=11)
    ax2.grid(True, which='both', alpha=0.3, linestyle='--')
    ax2.legend(loc='upper left', fontsize=9, ncol=1)

    fig.tight_layout()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT, dpi=130)
    plt.close(fig)
    print(f'Saved: {OUT}')


if __name__ == '__main__':
    main()
