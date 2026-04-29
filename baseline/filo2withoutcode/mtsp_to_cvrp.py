#!/usr/bin/env python3
"""Convert this repository's MTSP instances to CVRPLIB-style CVRP files."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Iterable


Point = tuple[float, float]


def read_mtsp_instance(path: str | Path) -> tuple[int, int, list[Point]]:
    path = Path(path)
    text = path.read_text(encoding="utf-8").strip()
    if not text:
        raise ValueError(f"Empty MTSP instance: {path}")

    if text[0] == "{":
        payload = json.loads(text)
        n = int(payload["n"])
        m = int(payload["m"])
        coords = [(float(x), float(y)) for x, y in payload["coords"]]
        if len(coords) != n:
            raise ValueError(f"JSON coords length {len(coords)} does not match n={n}: {path}")
        return n, m, coords

    tokens = text.split()
    if len(tokens) < 2:
        raise ValueError(f"Could not read n and m from MTSP instance: {path}")

    n = int(tokens[0])
    m = int(tokens[1])
    expected = 2 + 2 * n
    if len(tokens) < expected:
        raise ValueError(f"MTSP instance has {len(tokens)} tokens, expected at least {expected}: {path}")

    coords: list[Point] = []
    pos = 2
    for _ in range(n):
        coords.append((float(tokens[pos]), float(tokens[pos + 1])))
        pos += 2
    return n, m, coords


def write_cvrp(points: Iterable[Point], m: int, out_path: str | Path, name: str = "converted_mtsp") -> int:
    points = list(points)
    if len(points) < 2:
        raise ValueError("CVRP conversion needs at least a depot and one customer")
    if m <= 0:
        raise ValueError("m must be positive")

    n = len(points)
    customers = n - 1
    capacity = math.ceil(customers / m)
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    with out_path.open("w", encoding="utf-8", newline="\n") as f:
        f.write(f"NAME : {name}\n")
        f.write(f"COMMENT : MTSP converted to CVRP, m={m}\n")
        f.write("TYPE : CVRP\n")
        f.write(f"DIMENSION : {n}\n")
        f.write("EDGE_WEIGHT_TYPE : EUC_2D\n")
        f.write(f"CAPACITY : {capacity}\n")
        f.write("NODE_COORD_SECTION\n")
        for new_id, (x, y) in enumerate(points, start=1):
            f.write(f"{new_id} {x:.6f} {y:.6f}\n")

        f.write("DEMAND_SECTION\n")
        f.write("1 0\n")
        for new_id in range(2, n + 1):
            f.write(f"{new_id} 1\n")

        f.write("DEPOT_SECTION\n")
        f.write("1\n")
        f.write("-1\n")
        f.write("EOF\n")

    return capacity


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--m", type=int)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    n, instance_m, coords = read_mtsp_instance(args.input)
    m = args.m if args.m is not None else instance_m
    capacity = write_cvrp(coords, m, args.output, name=Path(args.input).stem)
    print(json.dumps({"n": n, "m": m, "capacity": capacity, "output": args.output}, indent=2))


if __name__ == "__main__":
    main()
