"""Enrich a results CSV with per-row metrics derived from the `routes` column.

Computes:
  - sum_length     — total length across all routes (= objective for MINSUM)
  - max_length     — length of the longest route
  - min_length     — length of the shortest non-empty route (0 if all empty)
  - n_empty        — number of routes that contain only the depot ([0, 0])
  - n_singleton    — number of routes that visit exactly 1 customer
  - balance_max_avg — max_length / mean_length over non-empty routes
  - n_routes       — number of routes returned

Reads the input CSV, writes an enriched CSV with the same rows + new columns.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_instance_coords(path_field: str) -> list[tuple[float, float]] | None:
    p = (ROOT / path_field) if not Path(path_field).is_absolute() else Path(path_field)
    if not p.exists():
        return None
    lines = [ln.strip() for ln in p.read_text(encoding="utf-8").splitlines() if ln.strip()]
    coords = [tuple(map(float, ln.split())) for ln in lines[1:]]
    return coords


def route_length(route: list[int], coords: list[tuple[float, float]]) -> float:
    if len(route) < 2:
        return 0.0
    total = 0.0
    for a, b in zip(route, route[1:]):
        xa, ya = coords[a]
        xb, yb = coords[b]
        total += math.hypot(xa - xb, ya - yb)
    return total


def enrich_row(row: dict) -> dict:
    routes_raw = row.get("routes", "")
    try:
        routes: list[list[int]] = json.loads(routes_raw) if routes_raw else []
    except json.JSONDecodeError:
        routes = []

    coords = read_instance_coords(row.get("path", ""))
    if coords is None:
        row.update({k: "" for k in (
            "sum_length", "max_length", "min_length", "n_empty",
            "n_singleton", "balance_max_avg", "n_routes",
        )})
        return row

    lengths = [route_length(r, coords) for r in routes]
    n_routes = len(routes)
    n_empty = sum(1 for r in routes if len(r) <= 2)
    n_singleton = sum(1 for r in routes if len(r) == 3)
    nonzero = [l for l in lengths if l > 1e-12]
    sum_l = sum(lengths)
    max_l = max(lengths) if lengths else 0.0
    min_l = min(nonzero) if nonzero else 0.0
    mean_l = sum(nonzero) / len(nonzero) if nonzero else 0.0
    balance = (max_l / mean_l) if mean_l > 1e-12 else 0.0

    row.update({
        "sum_length": f"{sum_l:.6f}",
        "max_length": f"{max_l:.6f}",
        "min_length": f"{min_l:.6f}",
        "n_empty": str(n_empty),
        "n_singleton": str(n_singleton),
        "balance_max_avg": f"{balance:.4f}",
        "n_routes": str(n_routes),
    })
    return row


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True, help="path to results CSV (with `routes` column)")
    ap.add_argument("--output", required=True, help="path to enriched CSV")
    args = ap.parse_args()

    inp = Path(args.input)
    if not inp.is_absolute():
        inp = ROOT / inp
    out = Path(args.output)
    if not out.is_absolute():
        out = ROOT / out

    with inp.open("r", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        rows = [enrich_row(dict(r)) for r in reader]

    new_cols = ["sum_length", "max_length", "min_length", "n_empty",
                "n_singleton", "balance_max_avg", "n_routes"]
    fieldnames = list(rows[0].keys()) if rows else []
    for c in new_cols:
        if c not in fieldnames:
            fieldnames.append(c)

    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"Wrote {out} ({len(rows)} rows)")


if __name__ == "__main__":
    main()
