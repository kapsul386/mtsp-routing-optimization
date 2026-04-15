from __future__ import annotations

import argparse
import json
import random
from pathlib import Path

import numpy as np


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate reproducible synthetic TSP benchmark tasks.")
    parser.add_argument("--config", default="experiments/tsp_config.json", help="Path to TSP benchmark config.")
    args = parser.parse_args()

    config = json.loads(Path(args.config).read_text(encoding="utf-8"))
    generation = config["generation"]

    task_dir = Path(config["task_dir"])
    task_dir.mkdir(parents=True, exist_ok=True)
    coords_npz = Path(config["coords_npz"])
    coords_npz.parent.mkdir(parents=True, exist_ok=True)

    node_counts = generation["node_counts"]
    repeats = int(generation["repeats"])
    base_seed = int(generation["base_seed"])
    lat_min = float(generation["lat_min"])
    lat_max = float(generation["lat_max"])
    lon_min = float(generation["lon_min"])
    lon_max = float(generation["lon_max"])

    max_nodes = 1 + sum(node_count - 1 for node_count in node_counts for _ in range(repeats))
    all_ids = np.arange(max_nodes, dtype=np.int64)
    rng = random.Random(base_seed)

    rows = []
    for idx in all_ids:
        lat = round(rng.uniform(lat_min, lat_max), 6)
        lon = round(rng.uniform(lon_min, lon_max), 6)
        rows.append([idx, lat, lon])
    np.savez(coords_npz, data=np.asarray(rows, dtype=np.float64))

    pointer = 1
    for node_count in node_counts:
        for repeat in range(1, repeats + 1):
            ids = [0]
            ids.extend(range(pointer, pointer + node_count - 1))
            pointer += node_count - 1

            task_name = f"n{node_count}_r{repeat:02d}.txt"
            task_path = task_dir / task_name
            task_path.write_text(
                f"{node_count}\n" + " ".join(str(node_id) for node_id in ids) + "\n",
                encoding="utf-8"
            )


if __name__ == "__main__":
    main()
