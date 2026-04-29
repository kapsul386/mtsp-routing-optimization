#!/usr/bin/env python3
"""Parse FILO2/CVRPLIB-style solution files and evaluate MTSP metrics."""

from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path
from typing import Iterable

from mtsp_to_cvrp import Point, read_mtsp_instance


ROUTE_LINE_RE = re.compile(r"^\s*(?:route\s*)?(?:#?\s*\d+)?\s*[:=]\s*(?P<body>.+)$", re.IGNORECASE)
INT_RE = re.compile(r"-?\d+")


def _normalise_route_ids(ids: Iterable[int], dimension: int) -> list[int]:
    route: list[int] = []
    for node in ids:
        # FILO2 stores solutions using its internal 0-based indexing. The depot
        # is 0, and customers have the same ids as this repository's MTSP nodes.
        if node in (-1, 0):
            continue
        if 1 <= node < dimension:
            route.append(node)
    return [0, *route, 0]


def parse_routes_from_text(text: str, dimension: int) -> list[list[int]]:
    routes: list[list[int]] = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        lower = stripped.lower()
        if any(key in lower for key in ("cost", "time", "iteration", "capacity", "demand", "name", "type")):
            continue

        match = ROUTE_LINE_RE.match(stripped)
        if not match:
            continue
        has_route_word = "route" in lower
        if not has_route_word and not re.match(r"^\s*#?\d+\s*[:=]", stripped):
            continue

        body = match.group("body")
        ids = [int(token) for token in INT_RE.findall(body)]
        route = _normalise_route_ids(ids, dimension)
        if len(route) > 2:
            routes.append(route)
    return routes


def parse_routes_from_file(path: str | Path, dimension: int) -> list[list[int]]:
    return parse_routes_from_text(Path(path).read_text(encoding="utf-8", errors="replace"), dimension)


def distance(coords: list[Point], a: int, b: int) -> float:
    ax, ay = coords[a]
    bx, by = coords[b]
    return math.hypot(ax - bx, ay - by)


def route_length(route: list[int], coords: list[Point]) -> float:
    return sum(distance(coords, route[i - 1], route[i]) for i in range(1, len(route)))


def validate_cover(routes: list[list[int]], n: int) -> tuple[bool, list[int], list[int]]:
    seen: list[int] = []
    for route in routes:
        if len(route) < 2 or route[0] != 0 or route[-1] != 0:
            return False, [], list(range(1, n))
        seen.extend(node for node in route[1:-1] if node != 0)

    counts = [0] * n
    for node in seen:
        if node <= 0 or node >= n:
            return False, [], list(range(1, n))
        counts[node] += 1

    duplicates = [node for node in range(1, n) if counts[node] > 1]
    missing = [node for node in range(1, n) if counts[node] == 0]
    return not duplicates and not missing, duplicates, missing


def split_longest_route(routes: list[list[int]], coords: list[Point]) -> bool:
    candidates = [(route_length(route, coords), idx) for idx, route in enumerate(routes) if len(route) > 3]
    if not candidates:
        return False
    _, idx = max(candidates)
    customers = routes[idx][1:-1]
    cut = max(1, min(len(customers) - 1, len(customers) // 2))
    routes[idx] = [0, *customers[:cut], 0]
    routes.insert(idx + 1, [0, *customers[cut:], 0])
    return True


def _oriented(seq: list[int], reversed_order: bool) -> list[int]:
    return list(reversed(seq)) if reversed_order else list(seq)


def merge_best_pair(routes: list[list[int]], coords: list[Point]) -> bool:
    if len(routes) < 2:
        return False

    best: tuple[float, int, int, bool, bool] | None = None
    for i in range(len(routes)):
        seq_i = routes[i][1:-1]
        for j in range(i + 1, len(routes)):
            seq_j = routes[j][1:-1]
            for rev_i in (False, True):
                left = _oriented(seq_i, rev_i)
                if not left:
                    continue
                for rev_j in (False, True):
                    right = _oriented(seq_j, rev_j)
                    if not right:
                        continue
                    merged = [0, *left, *right, 0]
                    length = route_length(merged, coords)
                    if best is None or length < best[0]:
                        best = (length, i, j, rev_i, rev_j)

    if best is None:
        return False
    _, i, j, rev_i, rev_j = best
    merged_customers = [*_oriented(routes[i][1:-1], rev_i), *_oriented(routes[j][1:-1], rev_j)]
    routes[i] = [0, *merged_customers, 0]
    del routes[j]
    return True


def postprocess_to_m_routes(routes: list[list[int]], m: int, coords: list[Point]) -> tuple[list[list[int]], list[str]]:
    routes = [list(route) for route in routes]
    actions: list[str] = []
    while len(routes) < m:
        if split_longest_route(routes, coords):
            actions.append("split_longest")
        else:
            routes.append([0, 0])
            actions.append("add_empty_route")
    while len(routes) > m:
        if merge_best_pair(routes, coords):
            actions.append("merge_best_pair")
        else:
            break
    return routes, actions


def compute_metrics(routes: list[list[int]], coords: list[Point], m_requested: int) -> dict:
    lengths = [route_length(route, coords) for route in routes]
    total = sum(lengths)
    max_len = max(lengths) if lengths else 0.0
    avg = total / len(lengths) if lengths else 0.0
    valid, duplicates, missing = validate_cover(routes, len(coords))
    return {
        "routes_count": len(routes),
        "m_requested": m_requested,
        "exact_m": len(routes) == m_requested,
        "valid_cover": valid,
        "duplicates_count": len(duplicates),
        "missing_count": len(missing),
        "total_distance": total,
        "max_route_distance": max_len,
        "avg_route_distance": avg,
        "imbalance": (max_len / avg) if avg > 0 else None,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--solution", required=True)
    parser.add_argument("--mtsp-input", required=True)
    parser.add_argument("--m", type=int)
    parser.add_argument("--postprocess", choices=("none", "exact-m"), default="exact-m")
    args = parser.parse_args()

    _, instance_m, coords = read_mtsp_instance(args.mtsp_input)
    m = args.m if args.m is not None else instance_m
    routes = parse_routes_from_file(args.solution, len(coords))
    raw_metrics = compute_metrics(routes, coords, m)
    actions: list[str] = []
    final_routes = routes
    if args.postprocess == "exact-m":
        final_routes, actions = postprocess_to_m_routes(routes, m, coords)
    final_metrics = compute_metrics(final_routes, coords, m)
    print(json.dumps({"raw": raw_metrics, "final": final_metrics, "postprocess_actions": actions}, indent=2))


if __name__ == "__main__":
    main()
