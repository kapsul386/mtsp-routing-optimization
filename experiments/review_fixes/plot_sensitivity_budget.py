"""
Plot sensitivity-анализа: качество (MINSUM, makespan) vs time-budget {50,150,350,1000}s
на 3 flagship-инстансах.

Читает sensitivity_budget_results.csv, строит:
1) figure: minsum vs budget (3 кривые на 1 графике, по одной на инстанс);
2) figure: makespan vs budget (аналогично);
3) figure: alns_iters vs budget (диагностика — сколько итераций ALNS успело).

Артефакты:
  data/results/figures/fig_sensitivity_minsum.png
  data/results/figures/fig_sensitivity_makespan.png
  data/results/figures/fig_sensitivity_iters.png
"""

import csv
import sys
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

sys.stdout.reconfigure(encoding='utf-8')

ROOT = Path(__file__).resolve().parents[2]
CSV = ROOT / 'experiments' / 'review_fixes' / 'sensitivity_budget_results.csv'
FIGS = ROOT / 'data' / 'results' / 'figures'
FIGS.mkdir(parents=True, exist_ok=True)

INSTANCE_LABELS = {
    'uniform_n100000_m5_r01.txt': ('uniform', 'tab:blue', 'o'),
    'clustered-center_n100000_m5_r01.txt': ('clustered-center', 'tab:orange', 's'),
    'clustered-offset-depot_n100000_m5_r01.txt': ('clustered-offset-depot', 'tab:green', '^'),
}


def load():
    """{instance: [{budget_s, minsum, makespan, alns_iters, t_actual_s}, ...]}"""
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
                'alns_iters': int(float(r.get('alns_iters', 0) or 0)),
                'alns_accepts': int(float(r.get('alns_accepts', 0) or 0)),
                't_actual_s': float(r.get('t_actual_s', 0) or 0),
            })
    for inst in by_inst:
        by_inst[inst].sort(key=lambda x: x['budget_s'])
    return by_inst


def plot_metric(by_inst, metric, ylabel, fname, log_x=True, log_y=False):
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for inst, rows in sorted(by_inst.items()):
        if inst not in INSTANCE_LABELS:
            continue
        label, color, marker = INSTANCE_LABELS[inst]
        xs = [r['budget_s'] for r in rows]
        ys = [r[metric] for r in rows]
        ax.plot(xs, ys, marker=marker, color=color, label=label, linewidth=2, markersize=8)

    ax.set_xlabel('time-budget, s', fontsize=11)
    ax.set_ylabel(ylabel, fontsize=11)
    if log_x:
        ax.set_xscale('log')
    if log_y:
        ax.set_yscale('log')
    ax.set_xticks([50, 150, 350, 1000])
    ax.set_xticklabels(['50', '150', '350', '1000'])
    ax.grid(True, which='both', alpha=0.3, linestyle='--')
    ax.legend(loc='best', fontsize=10)
    fig.tight_layout()
    out = FIGS / fname
    fig.savefig(out, dpi=130)
    plt.close(fig)
    print(f'  → {out.name}')


def plot_panel_minsum(by_inst, fname):
    """Двухпанельный плот: absolute MINSUM + relative-to-baseline (50s) для diminishing returns."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 4.8))
    for inst, rows in sorted(by_inst.items()):
        if inst not in INSTANCE_LABELS:
            continue
        label, color, marker = INSTANCE_LABELS[inst]
        xs = [r['budget_s'] for r in rows]
        ys = [r['minsum'] for r in rows]
        baseline = ys[0] if ys else 1
        rel = [100 * (y / baseline - 1) for y in ys]  # % relative to 50s
        ax1.plot(xs, ys, marker=marker, color=color, label=label, linewidth=2, markersize=8)
        ax2.plot(xs, rel, marker=marker, color=color, label=label, linewidth=2, markersize=8)

    for ax in (ax1, ax2):
        ax.set_xlabel('time-budget, s', fontsize=11)
        ax.set_xscale('log')
        ax.set_xticks([50, 150, 350, 1000])
        ax.set_xticklabels(['50', '150', '350', '1000'])
        ax.grid(True, which='both', alpha=0.3, linestyle='--')

    ax1.set_ylabel('MINSUM (sum of route lengths)', fontsize=11)
    ax1.set_title('(a) Absolute MINSUM', fontsize=11)
    ax2.set_ylabel('Relative to T=50‍s, %', fontsize=11)
    ax2.set_title('(b) Diminishing returns', fontsize=11)
    ax2.axhline(0, color='gray', linewidth=0.7, alpha=0.6)
    ax1.legend(loc='best', fontsize=10)
    ax2.legend(loc='best', fontsize=10)
    fig.tight_layout()
    out = FIGS / fname
    fig.savefig(out, dpi=130)
    plt.close(fig)
    print(f'  → {out.name}')


def print_summary(by_inst):
    """Печатает краткую сводку «качество vs бюджет»."""
    print('\n=== Sensitivity сводка ===')
    print(f'{"Instance":40s}  {"50s":>10s}  {"150s":>10s}  {"350s":>10s}  {"1000s":>10s}  {"Δ(50→1000)":>12s}')
    for inst, rows in sorted(by_inst.items()):
        if inst not in INSTANCE_LABELS:
            continue
        label = INSTANCE_LABELS[inst][0]
        # build mapping budget→minsum
        by_b = {r['budget_s']: r['minsum'] for r in rows}
        m50 = by_b.get(50, float('nan'))
        m150 = by_b.get(150, float('nan'))
        m350 = by_b.get(350, float('nan'))
        m1000 = by_b.get(1000, float('nan'))
        if m50 and m1000:
            delta = 100 * (m1000 - m50) / m50
            print(f'{label:40s}  {m50:>10.0f}  {m150:>10.0f}  {m350:>10.0f}  {m1000:>10.0f}  {delta:>11.2f}%')


def main():
    if not CSV.exists():
        print(f'sensitivity_budget_results.csv ещё не существует: {CSV}')
        sys.exit(1)
    by_inst = load()
    if not by_inst:
        print('Нет валидных запусков')
        sys.exit(1)
    print(f'Загружено: {sum(len(v) for v in by_inst.values())} строк, {len(by_inst)} инстансов')

    plot_metric(by_inst, 'minsum', 'MINSUM (sum of route lengths)', 'fig_sensitivity_minsum.png', log_x=True)
    plot_metric(by_inst, 'makespan', 'Makespan (max route length)', 'fig_sensitivity_makespan.png', log_x=True)
    plot_metric(by_inst, 'alns_iters', 'ALNS iterations', 'fig_sensitivity_iters.png', log_x=True, log_y=True)
    plot_panel_minsum(by_inst, 'fig_sensitivity_panel.png')

    print_summary(by_inst)
    print('Готово.')


if __name__ == '__main__':
    main()
