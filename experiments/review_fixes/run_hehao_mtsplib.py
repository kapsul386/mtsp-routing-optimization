"""
Прогон He-Hao memetic algorithm (Pengfei He, Jin-Kao Hao, EJOR 307(3), 2023)
на 16 mTSPLib-инстансах через WSL2.

Бинарь He-Hao — Linux ELF (`ma`), извлечённый из ~/AppData/Local/Temp/hehao_mtsp/.
Format вызова: `./ma <instance> <time_seconds> <seed>` (3 аргумента, не 4 как в README).

Шаги для каждого инстанса:
1) конвертация mTSPLib-формата (n m\\n coords) в He-Hao-формат (name EUC_2D n_customers m\\n cities);
2) копирование файла в /tmp в WSL;
3) запуск `./ma <name>.txt <time_s> <seed>`;
4) чтение certificate `/tmp/<name>_<seed>.txt` (routes + fitness);
5) пересчёт sum по евклидовой матрице (He-Hao возвращает только makespan).

Результат: experiments/review_fixes/hehao_mtsplib_results.json
"""

import json
import math
import re
import subprocess
import sys
import time
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')

ROOT = Path(__file__).resolve().parents[2]
MTSPLIB_DIR = ROOT / 'data' / 'mtsp' / 'mtsplib'
import os as _os
# Path to the He-Hao MA binary (Windows). Override via HEHAO_BIN env variable.
HEHAO_BIN_WIN = Path(_os.environ.get('HEHAO_BIN', _os.path.expandvars(r'%TEMP%\hehao_mtsp\minmx_mtsp\code\ma')))
OUT_JSON = ROOT / 'experiments' / 'review_fixes' / 'hehao_mtsplib_results.json'

TIME_LIMIT_S = 30
SEED = 1


def read_mtsplib(path: Path):
    """Читает mTSPLib-формат: 'n m' + n строк 'x y'. Возвращает (n, m, coords[N×2])."""
    with open(path, encoding='utf-8') as f:
        lines = [ln.strip() for ln in f.readlines() if ln.strip()]
    n, m = map(int, lines[0].split())
    coords = []
    for i in range(1, n + 1):
        parts = lines[i].split()
        coords.append((float(parts[0]), float(parts[1])))
    return n, m, coords


def write_hehao_format(out_path: Path, base: str, coords, m):
    """He-Hao format: 'name EUC_2D n_total m\\n' + 'idx x y' для всех cities (depot = idx 1).

    He-Hao reads `n_total` cities per header, treats city 1 as depot, customers internal
    indices 1..n_total-1 (= file cities 2..n_total). Mapping в~routes: depot=0, He-Hao
    customer c → file city c+1 → coords[c] in our 0-indexed array.
    """
    n_total = len(coords)
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(f'{base} EUC_2D {n_total} {m}\n')
        for i, (x, y) in enumerate(coords, 1):
            xs = f'{int(x)}' if x == int(x) else f'{x}'
            ys = f'{int(y)}' if y == int(y) else f'{y}'
            f.write(f'{i}\t{xs}\t{ys}\n')


def parse_certificate(cert_text: str):
    """Парсит He-Hao certificate, возвращает (makespan, routes::List[List[int]])."""
    lines = cert_text.split('\n')
    fitness = None
    routes = []
    state = 'waiting_for_fitness'
    for ln in lines:
        ln_stripped = ln.strip()
        if state == 'waiting_for_fitness':
            if ln_stripped.startswith("the best solution's fitness"):
                state = 'reading_fitness'
        elif state == 'reading_fitness':
            try:
                fitness = float(ln_stripped)
                state = 'waiting_for_routes'
            except ValueError:
                pass
        elif state == 'waiting_for_routes':
            if ln_stripped.startswith('the sequence'):
                state = 'reading_routes'
        elif state == 'reading_routes':
            if not ln_stripped or ln_stripped.startswith('the actuall'):
                state = 'done'
                break
            tokens = ln_stripped.split()
            try:
                route = [int(t) for t in tokens]
                routes.append(route)
            except ValueError:
                pass
    return fitness, routes


def compute_route_lengths(routes, coords):
    """Считает длину каждого маршрута.

    He-Hao convention: routes содержат 0 (depot) и индексы 1..n_total-1 для customer-ов.
    City 0 (depot) → coords[0]; He-Hao city `a` (a≥1) → coords[a] напрямую.
    Эта конвенция проверена эмпирически на berlin52_m2: recompute=4049.41 = HGA-fitness.
    """
    lengths = []
    for r in routes:
        L = 0.0
        for i in range(len(r) - 1):
            a, b = r[i], r[i + 1]
            ax, ay = coords[a]  # 0=depot, a>=1=customer (a directly indexes coords[])
            bx, by = coords[b]
            L += math.sqrt((ax - bx) ** 2 + (ay - by) ** 2)
        lengths.append(L)
    return lengths


def win_to_wsl(win_path):
    """Конвертирует C:\\foo\\bar в /mnt/c/foo/bar."""
    p = str(win_path).replace('\\', '/')
    if len(p) >= 2 and p[1] == ':':
        return f'/mnt/{p[0].lower()}{p[2:]}'
    return p


def run_hehao(inst_path: Path, base: str, m: int, time_s: int, seed: int):
    """Запускает He-Hao на инстансе через WSL. Возвращает dict с результатами."""
    # 1) конвертируем в He-Hao формат
    n_total, m_check, coords = read_mtsplib(inst_path)
    assert m_check == m

    # 2) формируем файл с He-Hao-форматом во временной windows-папке
    # Convention: header instance name = just `base` (no _m suffix),
    # file name = `<base>_m<m>.txt` for uniqueness on disk.
    # He-Hao writes certificate using instance name from header, so:
    #   cert path = /tmp/<base>_<m>_<seed>.txt
    tmp_dir = Path(_os.path.expandvars(r'%TEMP%\hehao_mtsp\run'))
    tmp_dir.mkdir(parents=True, exist_ok=True)
    file_stem = f'{base}_m{m}'
    win_inst = tmp_dir / f'{file_stem}.txt'
    write_hehao_format(win_inst, base, coords, m)  # header uses `base` only

    # 3) запуск ВСЁ В ОДНОЙ WSL-сессии (cp + run + cat) чтобы /tmp persisted
    cert_basename = f'{base}_{m}_{seed}'
    bash_script = (
        f'cp "{win_to_wsl(HEHAO_BIN_WIN)}" /tmp/ma && chmod +x /tmp/ma && '
        f'cp "{win_to_wsl(win_inst)}" /tmp/{file_stem}.txt && '
        f'cd /tmp && rm -f /tmp/{cert_basename}.txt && '
        f'timeout {time_s + 30} /tmp/ma {file_stem}.txt {time_s} {seed} 2>&1 | tail -3 && '
        f'echo \'__CERT_DELIM__\' && '
        f'cat /tmp/{cert_basename}.txt 2>/dev/null'
    )

    t1 = time.time()
    proc = subprocess.run(
        ['wsl.exe', '--', 'bash', '-c', bash_script],
        capture_output=True,
        text=True,
        encoding='utf-8',
        errors='replace',
        timeout=time_s + 60,
    )
    t2 = time.time()

    # split by __CERT_DELIM__
    full = proc.stdout or ''
    if '__CERT_DELIM__' in full:
        run_out, cert_text = full.split('__CERT_DELIM__', 1)
        cert_text = cert_text.lstrip('\n')
    else:
        run_out, cert_text = full, ''

    last_line = run_out.strip().split('\n')[-1] if run_out.strip() else ''

    fitness, routes = parse_certificate(cert_text)

    # 6) пересчитываем длины
    if routes:
        lengths = compute_route_lengths(routes, coords)
        recompute_max = max(lengths)
        recompute_sum = sum(lengths)
        diff_makespan = abs(recompute_max - (fitness or 0))
    else:
        lengths = []
        recompute_max = recompute_sum = 0.0
        diff_makespan = -1

    return {
        'instance': inst_path.name,
        'n': n_total,
        'm': m,
        'seed': seed,
        'time_s_actual': t2 - t1,
        'time_s_budget': time_s,
        'hehao_fitness': fitness,
        'makespan': recompute_max,
        'sum': recompute_sum,
        'route_lengths': lengths,
        'n_routes': len(routes),
        'recompute_diff_max': diff_makespan,
        'last_line': last_line,
        'returncode': proc.returncode,
    }


def main():
    if not HEHAO_BIN_WIN.exists():
        sys.stderr.write(f'He-Hao binary not found: {HEHAO_BIN_WIN}\n')
        sys.exit(1)
    if not MTSPLIB_DIR.exists():
        sys.stderr.write(f'mTSPLib dir not found: {MTSPLIB_DIR}\n')
        sys.exit(1)

    instances = sorted(MTSPLIB_DIR.glob('*.txt'))
    print(f'Найдено: {len(instances)} инстансов в {MTSPLIB_DIR}')
    print(f'Time-limit: {TIME_LIMIT_S}s/инстанс, seed={SEED}')

    results = []
    for i, path in enumerate(instances, 1):
        # извлекаем base + m из имени файла, например "berlin52_m2.txt" → ("berlin52", 2)
        name = path.stem
        match = re.match(r'^(.+)_m(\d+)$', name)
        if not match:
            print(f'  [{i}/{len(instances)}] {path.name} skipped (нет _mN суффикса)')
            continue
        base, m_str = match.group(1), int(match.group(2))
        try:
            print(f'  [{i}/{len(instances)}] {path.name}  ', end='', flush=True)
            r = run_hehao(path, base, m_str, TIME_LIMIT_S, SEED)
            print(f'makespan={r["makespan"]:.2f} sum={r["sum"]:.2f} t={r["time_s_actual"]:.1f}s '
                  f'(diff_check={r["recompute_diff_max"]:.4f})')
            results.append(r)
        except Exception as e:
            print(f'ERROR: {e}')
            results.append({'instance': path.name, 'error': str(e)})

    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT_JSON, 'w', encoding='utf-8') as f:
        json.dump({'time_limit_s': TIME_LIMIT_S, 'seed': SEED, 'results': results}, f, indent=2)
    print(f'\nSaved: {OUT_JSON}')


if __name__ == '__main__':
    main()
