from __future__ import annotations

import argparse
import random
from pathlib import Path


def parse_int_list(raw: str) -> list[int]:
    return [int(part.strip()) for part in raw.split(",") if part.strip()]


def generate_instance(node_count: int, width: float, height: float, seed: int) -> list[tuple[float, float]]:
    rng = random.Random(seed)
    coords = [(width / 2.0, height / 2.0)]
    for _ in range(node_count - 1):
        coords.append((round(rng.uniform(0.0, width), 3), round(rng.uniform(0.0, height), 3)))
    return coords


def write_instance(path: Path, salesman_count: int, coords: list[tuple[float, float]]) -> None:
    lines = [f"{len(coords)} {salesman_count}"]
    lines.extend(f"{x} {y}" for x, y in coords)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate reproducible synthetic mTSP benchmark instances.")
    parser.add_argument("--output-dir", default="data/mtsp/generated", help="Directory for generated instances.")
    parser.add_argument("--node-counts", default="20,50,100", help="Comma-separated node counts.")
    parser.add_argument("--salesmen", default="2,3,5", help="Comma-separated salesman counts.")
    parser.add_argument("--repeats", type=int, default=3, help="Number of instances per (n, m) pair.")
    parser.add_argument("--base-seed", type=int, default=20260404, help="Base seed for reproducible generation.")
    parser.add_argument("--width", type=float, default=100.0, help="Synthetic map width.")
    parser.add_argument("--height", type=float, default=100.0, help="Synthetic map height.")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    node_counts = parse_int_list(args.node_counts)
    salesman_counts = parse_int_list(args.salesmen)

    for node_count in node_counts:
        for salesman_count in salesman_counts:
            for repeat in range(1, args.repeats + 1):
                seed = args.base_seed + node_count * 1000 + salesman_count * 100 + repeat
                coords = generate_instance(node_count, args.width, args.height, seed)
                filename = f"n{node_count}_m{salesman_count}_r{repeat:02d}.txt"
                write_instance(output_dir / filename, salesman_count, coords)


if __name__ == "__main__":
    main()
