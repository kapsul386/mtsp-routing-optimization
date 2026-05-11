"""
Прямое сравнение ALNS-mTSP и FILO2 на N=10K с расширенной m-сеткой.

Цель — закрыть лакуну: для high-m режима (m∈{10,30,50,80,100}) у нас были
только ALNS-результаты (high_m_sweep_results.csv); FILO2 на тех же
инстансах не прогонялся. Этот скрипт даёт прямую parallel-проверку с
одинаковым time-budget=70s и стандартным набором FILO2-флагов:

  --optimization-seconds 70
  --routemin-iterations 15
  --seed 1

ALNS запускается с тем же time-budget=70s, --step lkh_v21_minsum, --seed 1.

Артефакт: filo2_vs_alns_high_m_results.csv (одна строка на пару (instance, solver)).
"""

import csv
import json
import math
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / 'build' / 'src' / 'Release' / 'mtsp.exe'
DATA = ROOT / 'data' / 'mtsp' / 'generated_multifamily'
FILO2_BIN_WSL = os.environ.get(
    'FILO2_WSL_BIN',
    '/mnt/c/Users/ddkup/coursework/mtsp-routing-optimization/external/filo2/build/filo2',
)
OUT_CSV = ROOT / 'experiments' / 'review_fixes' / 'filo2_vs_alns_high_m_results.csv'

FAMILIES = [
    'clustered-center',
    'clustered-offset-depot',
    'high-m-stress',
    'mixed',
    'mixed-outliers',
    'outliers',
]
M_VALUES = [10, 30, 50, 80, 100]
TIME_BUDGET_S = 70
SEED = 1


# -----------------------------------------------------------------------------
# Helpers (как в run_filo2_vs_alns_n100k.py)
# -----------------------------------------------------------------------------

def euclidean(p, q):
    return math.hypot(p[0] - q[0], p[1] - q[1])


def gini(values):
    nv = len(values)
    if nv == 0:
        return 0.0
    mean = sum(values) / nv
    if mean <= 0:
        return 0.0
    s = sum(abs(x - y) for x in values for y in values)
    return s / (2 * nv * nv * mean)


def load_mtsp(path):
    with open(path) as f:
        first = f.readline().split()
        n, m = int(first[0]), int(first[1])
        coords = []
        for _ in range(n):
            xy = f.readline().split()
            coords.append((float(xy[0]), float(xy[1])))
    return coords, n, m


def to_wsl_path(p):
    p = os.path.abspath(p).replace('\\', '/')
    if p[1:3] == ':/':
        p = '/mnt/' + p[0].lower() + p[2:]
    return p


def write_cvrp(coords, n, m, out_path, scale=1000, demand_scale=10):
    base_cap = math.ceil((n - 1) / m)
    capacity = base_cap * demand_scale
    with open(out_path, 'w') as f:
        f.write('NAME : high_m_filo2_compare\n')
        f.write('TYPE : CVRP\n')
        f.write(f'DIMENSION : {n}\n')
        f.write('EDGE_WEIGHT_TYPE : EUC_2D\n')
        f.write(f'CAPACITY : {capacity}\n')
        f.write('NODE_COORD_SECTION\n')
        for i, (x, y) in enumerate(coords, start=1):
            f.write(f'{i} {int(round(x * scale))} {int(round(y * scale))}\n')
        f.write('DEMAND_SECTION\n')
        f.write('1 0\n')
        for i in range(2, n + 1):
            f.write(f'{i} {demand_scale}\n')
        f.write('DEPOT_SECTION\n1\n-1\nEOF\n')
    return capacity


def parse_filo2_solution(sol_path, coords, n_total):
    routes = []
    if not os.path.exists(sol_path):
        return routes
    with open(sol_path) as f:
        lines = [ln.strip() for ln in f.readlines()]
    for ln in lines:
        if ln.startswith('Route'):
            parts = ln.split(':')
            if len(parts) < 2:
                continue
            cities = [int(x) for x in parts[1].split()]
            route = [0] + [c - 1 for c in cities] + [0]
            routes.append(route)
    return routes


def compute_metrics(routes, coords):
    if not routes:
        return None
    lens = []
    for r in routes:
        L = 0.0
        for i in range(len(r) - 1):
            L += euclidean(coords[r[i]], coords[r[i + 1]])
        lens.append(L)
    if not lens:
        return None
    return {
        'sum': sum(lens),
        'makespan': max(lens),
        'min_route': min([L for L in lens if L > 0], default=0),
        'gini': gini(lens),
        'std': (sum((L - sum(lens) / len(lens)) ** 2 for L in lens) / len(lens)) ** 0.5,
        'n_nonempty': sum(1 for L in lens if L > 0),
        'n_routes': len(routes),
    }


# -----------------------------------------------------------------------------
# FILO2 launcher
# -----------------------------------------------------------------------------

def run_filo2(inst_filename, time_s, seed):
    mtsp_path = DATA / inst_filename
    coords, n, m = load_mtsp(mtsp_path)
    tmp = tempfile.mkdtemp(prefix='filo2_highm_', dir=str(ROOT / 'build'))
    try:
        cvrp_path = os.path.join(tmp, 'instance.vrp')
        write_cvrp(coords, n, m, cvrp_path)
        cvrp_wsl = to_wsl_path(cvrp_path)
        out_wsl = to_wsl_path(tmp) + '/'

        cmd = [
            'wsl', '-e', 'bash', '-c',
            f'{FILO2_BIN_WSL} {cvrp_wsl} --outpath {out_wsl} '
            f'--optimization-seconds {time_s} '
            f'--routemin-iterations 15 '
            f'--seed {seed} 2>&1'
        ]
        t0 = time.time()
        try:
            py_timeout = max(time_s * 3 + 60, time_s + 600)
            proc = subprocess.run(cmd, capture_output=True, text=False, timeout=py_timeout)
            t_actual = time.time() - t0
            stdout = proc.stdout.decode('utf-8', errors='replace')
            timed_out = False
        except subprocess.TimeoutExpired:
            t_actual = time.time() - t0
            stdout = ''
            timed_out = True

        sol_files = list(Path(tmp).glob('*.vrp.sol*'))
        if not sol_files:
            sol_files = list(Path(tmp).glob('*solution*'))
        sol = sol_files[0] if sol_files else None
        if sol is None:
            return {
                'solver': 'filo2',
                'instance': inst_filename,
                'valid': False,
                'timed_out': timed_out,
                't_actual_s': t_actual,
                'error': 'no_solution_file',
                'stdout_tail': stdout[-500:] if stdout else '',
            }

        routes = parse_filo2_solution(str(sol), coords, n)
        metrics = compute_metrics(routes, coords)
        if metrics is None:
            return {
                'solver': 'filo2',
                'instance': inst_filename,
                'valid': False,
                'timed_out': timed_out,
                't_actual_s': t_actual,
                'error': 'parse_failed',
            }
        out = {
            'solver': 'filo2',
            'instance': inst_filename,
            'valid': True,
            'timed_out': timed_out,
            't_actual_s': t_actual,
        }
        out.update(metrics)
        return out
    finally:
        try:
            import shutil
            shutil.rmtree(tmp, ignore_errors=True)
        except Exception:
            pass


# -----------------------------------------------------------------------------
# ALNS-mTSP launcher
# -----------------------------------------------------------------------------

def run_alns(inst_filename, time_s, seed):
    inst_path = DATA / inst_filename
    args = [
        str(EXE),
        '--input-file', inst_path.name,
        '--emit-routes', 'true',
        '--step', 'lkh_v21_minsum',
        '--seed', str(seed),
        '--time-budget-ms', str(time_s * 1000),
    ]
    t0 = time.time()
    proc = subprocess.run(args, cwd=str(inst_path.parent), capture_output=True,
                          text=True, encoding='utf-8', errors='replace',
                          timeout=time_s * 2 + 120)
    t_actual = time.time() - t0
    if proc.returncode != 0:
        return {
            'solver': 'alns_minsum',
            'instance': inst_filename,
            'valid': False,
            't_actual_s': t_actual,
            'error': proc.stderr.strip()[:300],
        }
    out_json = json.loads(proc.stdout)
    routes_field = out_json.get('steps', [{}])[0].get('routes') or out_json.get('routes')

    coords, n, _ = load_mtsp(inst_path)
    if isinstance(routes_field, str):
        routes = json.loads(routes_field)
    else:
        routes = routes_field
    metrics = compute_metrics(routes, coords) if routes else None

    out = {
        'solver': 'alns_minsum',
        'instance': inst_filename,
        'valid': bool(out_json.get('valid')),
        'timed_out': False,
        't_actual_s': t_actual,
    }
    if metrics:
        out.update(metrics)
    return out


# -----------------------------------------------------------------------------
# Driver
# -----------------------------------------------------------------------------

def main():
    rows = []
    pairs = [(fam, m) for fam in FAMILIES for m in M_VALUES]
    total = len(pairs)
    t_start = time.time()
    for i, (fam, m) in enumerate(pairs, start=1):
        inst = f'{fam}_n10000_m{m}_r01.txt'
        path = DATA / inst
        if not path.exists():
            print(f'[{i}/{total}] NOT FOUND: {inst}', flush=True)
            continue

        elapsed_total = time.time() - t_start
        print(f'\n[{i}/{total}] === {inst}  (elapsed={elapsed_total:.0f}s) ===', flush=True)

        print(f'  → ALNS-mTSP (lkh_v21_minsum) budget={TIME_BUDGET_S}s ...', flush=True)
        try:
            r_alns = run_alns(inst, TIME_BUDGET_S, SEED)
            r_alns['family'] = fam
            r_alns['m'] = m
            if r_alns.get('valid'):
                print(f'    sum={r_alns.get("sum", 0):.0f}  max={r_alns.get("makespan", 0):.0f}  '
                      f'gini={r_alns.get("gini", 0):.3f}  routes={r_alns.get("n_nonempty", 0)}  '
                      f't={r_alns.get("t_actual_s", 0):.1f}s', flush=True)
            else:
                print(f'    INVALID: {r_alns.get("error", "unknown")[:200]}', flush=True)
        except Exception as e:
            print(f'    ERROR: {e}', flush=True)
            r_alns = {'solver': 'alns_minsum', 'instance': inst, 'family': fam, 'm': m,
                      'valid': False, 'error': str(e)[:200]}
        rows.append(r_alns)

        print(f'  → FILO2 (--routemin-iterations 15) budget={TIME_BUDGET_S}s ...', flush=True)
        try:
            r_filo = run_filo2(inst, TIME_BUDGET_S, SEED)
            r_filo['family'] = fam
            r_filo['m'] = m
            if r_filo.get('valid'):
                print(f'    sum={r_filo.get("sum", 0):.0f}  max={r_filo.get("makespan", 0):.0f}  '
                      f'gini={r_filo.get("gini", 0):.3f}  routes={r_filo.get("n_nonempty", 0)}  '
                      f't={r_filo.get("t_actual_s", 0):.1f}s', flush=True)
            else:
                print(f'    INVALID: {r_filo.get("error", "unknown")} t={r_filo.get("t_actual_s", 0):.1f}s',
                      flush=True)
        except Exception as e:
            print(f'    ERROR: {e}', flush=True)
            r_filo = {'solver': 'filo2', 'instance': inst, 'family': fam, 'm': m,
                      'valid': False, 'error': str(e)[:200]}
        rows.append(r_filo)

        # Промежуточный сброс CSV (на случай прерывания)
        fieldnames = ['solver', 'family', 'm', 'instance', 'valid', 'timed_out', 't_actual_s',
                      'sum', 'makespan', 'min_route', 'gini', 'std',
                      'n_nonempty', 'n_routes', 'error', 'stdout_tail']
        with open(OUT_CSV, 'w', encoding='utf-8', newline='') as f:
            w = csv.DictWriter(f, fieldnames=fieldnames)
            w.writeheader()
            for r in rows:
                for k in fieldnames:
                    r.setdefault(k, '')
                w.writerow(r)

    total_s = time.time() - t_start
    print(f'\n=== ГОТОВО за {total_s:.0f}s ({total_s/60:.1f} мин) ===')
    print(f'Записано: {OUT_CSV}')
    print(f'Всего строк: {len(rows)}')
    print(f'Валидных: {sum(1 for r in rows if r.get("valid"))}/{len(rows)}')


if __name__ == '__main__':
    main()
