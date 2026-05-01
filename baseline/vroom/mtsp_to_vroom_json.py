#!/usr/bin/env python3
"""Convert mTSP instance into VROOM JSON input format."""

from __future__ import annotations

import math
import sys
from pathlib import Path
from typing import Iterable, Tuple

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
FILO2_DIR = REPO_ROOT / "baseline" / "filo2withoutcode"
sys.path.insert(0, str(FILO2_DIR))

Point = Tuple[float, float]


def build_vroom_payload(coords: list[Point], m: int, capacity_each: int, profile: str = "car") -> dict:
    n = len(coords)

    # Euclidean L2 matrix (rounded to int — VROOM expects int durations).
    durations = [[0] * n for _ in range(n)]
    for i in range(n):
        xi, yi = coords[i]
        for j in range(i + 1, n):
            xj, yj = coords[j]
            d = int(round(math.hypot(xi - xj, yi - yj)))
            durations[i][j] = d
            durations[j][i] = d

    vehicles = [
        {
            "id": v,
            "profile": profile,
            "start_index": 0,
            "end_index": 0,
            "capacity": [capacity_each],
        }
        for v in range(m)
    ]

    jobs = [
        {
            "id": idx,
            "location_index": idx,
            "delivery": [1],
        }
        for idx in range(1, n)
    ]

    return {
        "vehicles": vehicles,
        "jobs": jobs,
        "matrices": {profile: {"durations": durations}},
    }
