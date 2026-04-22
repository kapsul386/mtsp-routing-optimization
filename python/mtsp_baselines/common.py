from __future__ import annotations

import csv
import json
import math
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


@dataclass(frozen=True)
class MtspInstanceData:
    node_count: int
    salesman_count: int
    coords: list[tuple[float, float]]


def resolve_path(path_str: str) -> Path:
    path = Path(path_str)
    return path if path.is_absolute() else ROOT / path


def normalize_display_path(path: Path) -> str:
    resolved = path.resolve()
    try:
        return str(resolved.relative_to(ROOT.resolve()))
    except ValueError:
        return str(resolved)


def load_csv(path: Path) -> list[dict]:
    with path.open("r", encoding="utf-8-sig", newline="") as fh:
        return list(csv.DictReader(fh))


def write_csv(path: Path, rows: list[dict], fieldnames: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def is_true(value: object) -> bool:
    return str(value).lower() == "true"


def read_mtsp_instance(path: Path) -> MtspInstanceData:
    lines = [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
    node_count, salesman_count = map(int, lines[0].split())
    coords = [tuple(map(float, line.split())) for line in lines[1:]]
    if len(coords) != node_count:
        raise ValueError(f"{path}: expected {node_count} coordinates, got {len(coords)}")
    return MtspInstanceData(
        node_count=node_count,
        salesman_count=salesman_count,
        coords=coords,
    )


def euclidean(coords: list[tuple[float, float]], a: int, b: int) -> float:
    ax, ay = coords[a]
    bx, by = coords[b]
    return math.hypot(ax - bx, ay - by)


def route_length(coords: list[tuple[float, float]], route: list[int]) -> float:
    return sum(euclidean(coords, left, right) for left, right in zip(route, route[1:]))


def objective_minsum(coords: list[tuple[float, float]], routes: list[list[int]]) -> float:
    return sum(route_length(coords, route) for route in routes)


def validate_routes(routes: list[list[int]], node_count: int) -> bool:
    if any(not route or route[0] != 0 or route[-1] != 0 for route in routes):
        return False
    visited = [node for route in routes for node in route[1:-1]]
    return sorted(visited) == list(range(1, node_count))


def routes_to_json(routes: list[list[int]]) -> str:
    return json.dumps(routes, ensure_ascii=False)
