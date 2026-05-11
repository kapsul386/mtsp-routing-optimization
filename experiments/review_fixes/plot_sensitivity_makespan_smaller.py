"""
Plot sensitivity-makespan на меньших N (alns_minmax работает корректно).

Двухпанельный плот:
  (a) MINSUM vs budget для 6 ячеек (3 family × 2 N)
  (b) ALNS-iters vs budget — диагностика «когда ALNS phase достигается»

Артефакт: data/results/figures/fig_sensitivity_makespan_smaller.png
"""

import csv
import sys
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

sys.stdout.reconfigure(encoding='utf-8')

ROOT = Path(__file__).resolve().parents[2]
CSV = ROOT / 'experiments' / 'review_fixes' / 'sensitivity_makespan_smaller_results.csv'
FIGS = ROOT / 'data' / 'results' / 'figures'
FIGS.mkdir(parents=True, exist_ok=True)


def load():
    by_inst = {}
    with open(CSV, encoding='utf-8') as f:
        for r in csv.DictReader(f):
            if r.get('valid', '').lower() != 'true':
                continue
            inst = r['instance']
            by_inst.setdefault(inst, []).append({
                'budget_s': int(r['budget_s']),
                'minsum': float(r.get('minsum', 0) or 0),
                'makespan': float(r.get('makespan', 0) or 0),
                'gini': float(r.get('gini', 0) or 0),
                'alns_iters': int(float(r.get('alns_iters', 0) or 0)),
                't_actual_s': float(r.get('t_actual_s', 0) or 0),
            })
    for inst in by_inst:
        by_inst[inst].sort(key=lambda x: x['budget_s'])
    return by_inst


# Mapping: instance → (label, color, marker, n_value)
INSTANCE_META = {
    'uniform_n10000_m5_r01.txt':                ('uniform N=10K',                'tab:blue',   'o', 10000),
    'clustered-center_n10000_m5_r01.txt':       ('clust-center N=10K',           'tab:orange', 's', 10000),
    'clustered-offset-depot_n10000_m5_r01.txt': ('clust-offset N=10K',           'tab:green',  '^', 10000),
    'uniform_n25000_m5_r01.txt':                ('uniform N=25K',                'tab:red',    'o', 25000),
    'clustered-center_n25000_m5_r01.txt':       ('clust-center N=25K',           'tab:purple', 's', 25000),
    'clustered-offset-depot_n25000_m5_r01.txt': ('clust-offset N=25K',           'tab:brown',  '^', 25000),
}


def main():
    by_inst = load()
    if not by_inst:
        print('No data')
        return

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5))

    # Panel (a): makespan vs budget
    for inst, rows in sorted(by_inst.items()):
        if inst not in INSTANCE_META:
            continue
        label, color, marker, _ = INSTANCE_META[inst]
        xs = [r['budget_s'] for r in rows]
        ys = [r['makespan'] for r in rows]
        ax1.plot(xs, ys, marker=marker, color=color, label=label, linewidth=1.8, markersize=8, alpha=0.85)

    ax1.set_xlabel('time-budget, s', fontsize=11)
    ax1.set_ylabel('makespan (max route length)', fontsize=11)
    ax1.set_title('(a) Makespan vs budget (alns_minmax)', fontsize=11)
    ax1.set_xscale('log')
    ax1.set_yscale('log')
    ax1.set_xticks([30, 70, 150, 350])
    ax1.set_xticklabels(['30', '70', '150', '350'])
    ax1.grid(True, which='both', alpha=0.3, linestyle='--')
    ax1.legend(loc='best', fontsize=9, ncol=2)

    # Panel (b): ALNS iters vs budget
    for inst, rows in sorted(by_inst.items()):
        if inst not in INSTANCE_META:
            continue
        label, color, marker, _ = INSTANCE_META[inst]
        xs = [r['budget_s'] for r in rows]
        ys = [r['alns_iters'] for r in rows]
        ax2.plot(xs, ys, marker=marker, color=color, label=label, linewidth=1.8, markersize=8, alpha=0.85)

    ax2.set_xlabel('time-budget, s', fontsize=11)
    ax2.set_ylabel('ALNS-iterations', fontsize=11)
    ax2.set_title('(б) ALNS-итераций vs budget (0 = pre-phase only)', fontsize=11)
    ax2.set_xscale('log')
    ax2.set_xticks([30, 70, 150, 350])
    ax2.set_xticklabels(['30', '70', '150', '350'])
    ax2.grid(True, which='both', alpha=0.3, linestyle='--')
    ax2.axhline(0, color='red', linewidth=1.0, alpha=0.6, linestyle='--', label='_nolegend_')
    ax2.text(150, 2, 'iters=0: ALNS phase\nне достигается', ha='center', fontsize=9,
             color='red', style='italic',
             bbox=dict(boxstyle='round', facecolor='white', edgecolor='red', alpha=0.8))
    ax2.legend(loc='upper left', fontsize=9, ncol=2)

    fig.suptitle('Sensitivity-анализ alns_minmax на N∈{10K, 25K} с budget∈{30, 70, 150, 350}\,c',
                 fontsize=12, fontweight='bold', y=1.02)
    fig.tight_layout()
    out = FIGS / 'fig_sensitivity_makespan_smaller.png'
    fig.savefig(out, dpi=130, bbox_inches='tight')
    plt.close(fig)
    print(f'  → {out.name}')


if __name__ == '__main__':
    main()
