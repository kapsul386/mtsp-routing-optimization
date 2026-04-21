from __future__ import annotations

import argparse
import json
import math
import random
from pathlib import Path


def parse_int_list(raw: str) -> list[int]:
    return [int(part.strip()) for part in raw.split(",") if part.strip()]


def parse_str_list(raw: str) -> list[str]:
    return [part.strip() for part in raw.split(",") if part.strip()]


def parse_int_values(raw: str | list[int] | None, fallback: list[int]) -> list[int]:
    if raw is None:
        return list(fallback)
    if isinstance(raw, list):
        return [int(value) for value in raw]
    return parse_int_list(raw)


def parse_str_values(raw: str | list[str] | None, fallback: list[str]) -> list[str]:
    if raw is None:
        return list(fallback)
    if isinstance(raw, list):
        return [str(value).strip() for value in raw if str(value).strip()]
    return parse_str_list(raw)


def parse_salesmen_overrides(raw: object) -> dict[int, list[int]]:
    if not isinstance(raw, dict):
        return {}

    overrides: dict[int, list[int]] = {}
    for node_count_raw, salesman_values in raw.items():
        node_count = int(node_count_raw)
        if isinstance(salesman_values, list):
            overrides[node_count] = [int(value) for value in salesman_values]
        elif isinstance(salesman_values, str):
            overrides[node_count] = parse_int_list(salesman_values)
        else:
            raise ValueError(
                f"Unsupported extra_salesmen_by_node_count value for n={node_count_raw}: {salesman_values!r}"
            )
    return overrides


def clamp(value: float, lower: float, upper: float) -> float:
    return max(lower, min(upper, value))


def rounded_point(x: float, y: float) -> tuple[float, float]:
    return round(x, 3), round(y, 3)


def euclidean(left: tuple[float, float], right: tuple[float, float]) -> float:
    return math.hypot(left[0] - right[0], left[1] - right[1])


def choose_depot_position(
    family: str,
    width: float,
    height: float,
    depot_margin_ratio: float,
    rng: random.Random,
) -> tuple[float, float]:
    if family != "clustered-offset-depot":
        return rounded_point(width / 2.0, height / 2.0)

    margin = min(width, height) * depot_margin_ratio
    candidates = [
        (margin, margin),
        (width / 2.0, margin),
        (width - margin, margin),
        (width - margin, height / 2.0),
        (width - margin, height - margin),
        (width / 2.0, height - margin),
        (margin, height - margin),
        (margin, height / 2.0),
    ]
    x, y = rng.choice(candidates)
    return rounded_point(x, y)


def generate_uniform_clients(
    client_count: int,
    width: float,
    height: float,
    rng: random.Random,
) -> list[tuple[float, float]]:
    return [rounded_point(rng.uniform(0.0, width), rng.uniform(0.0, height)) for _ in range(client_count)]


def sample_bounded_gaussian(
    center: tuple[float, float],
    sigma: float,
    width: float,
    height: float,
    rng: random.Random,
) -> tuple[float, float]:
    cx, cy = center
    for _ in range(32):
        x = rng.gauss(cx, sigma)
        y = rng.gauss(cy, sigma)
        if 0.0 <= x <= width and 0.0 <= y <= height:
            return rounded_point(x, y)
    return rounded_point(
        clamp(rng.gauss(cx, sigma), 0.0, width),
        clamp(rng.gauss(cy, sigma), 0.0, height),
    )


def generate_cluster_sizes(client_count: int, cluster_count: int, rng: random.Random) -> list[int]:
    sizes = [1] * cluster_count
    for _ in range(client_count - cluster_count):
        sizes[rng.randrange(cluster_count)] += 1
    rng.shuffle(sizes)
    return sizes


def generate_cluster_centers(
    cluster_count: int,
    width: float,
    height: float,
    depot: tuple[float, float],
    cluster_center_margin_ratio: float,
    cluster_center_separation_ratio: float,
    depot_separation_ratio: float,
    rng: random.Random,
) -> list[tuple[float, float]]:
    centers = []
    for index in range(cluster_count):
        centers.append(
            sample_cluster_center(
                index=index,
                cluster_count=cluster_count,
                width=width,
                height=height,
                depot=depot,
                existing_centers=centers,
                cluster_center_margin_ratio=cluster_center_margin_ratio,
                cluster_center_separation_ratio=cluster_center_separation_ratio,
                depot_separation_ratio=depot_separation_ratio,
                rng=rng,
            )
        )
    return centers


def generate_clients_from_cluster_centers(
    client_count: int,
    centers: list[tuple[float, float]],
    sigma: float,
    width: float,
    height: float,
    rng: random.Random,
) -> list[tuple[float, float]]:
    clients = []
    for center, size in zip(centers, generate_cluster_sizes(client_count, len(centers), rng)):
        for _ in range(size):
            clients.append(sample_bounded_gaussian(center, sigma, width, height, rng))
    rng.shuffle(clients)
    return clients


def sample_edge_candidate(width: float, height: float, band_ratio: float, rng: random.Random) -> tuple[float, float]:
    band = max(min(width, height) * band_ratio, 1e-6)
    edge = rng.randrange(4)
    if edge == 0:
        return rounded_point(rng.uniform(0.0, width), rng.uniform(0.0, band))
    if edge == 1:
        return rounded_point(rng.uniform(width - band, width), rng.uniform(0.0, height))
    if edge == 2:
        return rounded_point(rng.uniform(0.0, width), rng.uniform(height - band, height))
    return rounded_point(rng.uniform(0.0, band), rng.uniform(0.0, height))


def fallback_outlier_point(
    width: float,
    height: float,
    depot: tuple[float, float],
    centers: list[tuple[float, float]],
) -> tuple[float, float]:
    candidates = [
        (0.0, 0.0),
        (width / 2.0, 0.0),
        (width, 0.0),
        (width, height / 2.0),
        (width, height),
        (width / 2.0, height),
        (0.0, height),
        (0.0, height / 2.0),
    ]

    def score(candidate: tuple[float, float]) -> float:
        references = [depot] + centers
        return min(euclidean(candidate, point) for point in references)

    best = max(candidates, key=score)
    return rounded_point(best[0], best[1])


def generate_mixed_outliers_clients(
    client_count: int,
    width: float,
    height: float,
    depot: tuple[float, float],
    cluster_counts: list[int],
    cluster_spread_ratio: float,
    cluster_center_margin_ratio: float,
    cluster_center_separation_ratio: float,
    depot_separation_ratio: float,
    mixed_outlier_ratio_min: float,
    mixed_outlier_ratio_max: float,
    mixed_outlier_cluster_distance_ratio: float,
    mixed_outlier_depot_distance_ratio: float,
    mixed_outlier_edge_band_ratio: float,
    rng: random.Random,
) -> list[tuple[float, float]]:
    valid_cluster_counts = sorted({max(1, min(client_count, count)) for count in cluster_counts})
    cluster_count = rng.choice(valid_cluster_counts)
    sigma = max(min(width, height) * cluster_spread_ratio, 1e-6)

    outlier_ratio = rng.uniform(mixed_outlier_ratio_min, mixed_outlier_ratio_max)
    outlier_count = int(round(client_count * outlier_ratio))
    outlier_count = max(1, outlier_count)
    outlier_count = min(client_count - cluster_count, outlier_count) if client_count > cluster_count else 0
    clustered_count = client_count - outlier_count

    centers = generate_cluster_centers(
        cluster_count=cluster_count,
        width=width,
        height=height,
        depot=depot,
        cluster_center_margin_ratio=cluster_center_margin_ratio,
        cluster_center_separation_ratio=cluster_center_separation_ratio,
        depot_separation_ratio=depot_separation_ratio,
        rng=rng,
    )
    clients = generate_clients_from_cluster_centers(
        client_count=clustered_count,
        centers=centers,
        sigma=sigma,
        width=width,
        height=height,
        rng=rng,
    )

    min_size = min(width, height)
    min_cluster_distance = min_size * mixed_outlier_cluster_distance_ratio
    min_depot_distance = min_size * mixed_outlier_depot_distance_ratio
    min_outlier_separation = min_size * max(mixed_outlier_edge_band_ratio / 2.0, 0.05)
    outliers: list[tuple[float, float]] = []
    for _ in range(outlier_count):
        chosen: tuple[float, float] | None = None
        for _ in range(128):
            candidate = sample_edge_candidate(width, height, mixed_outlier_edge_band_ratio, rng)
            if euclidean(candidate, depot) < min_depot_distance:
                continue
            if any(euclidean(candidate, center) < min_cluster_distance for center in centers):
                continue
            if any(euclidean(candidate, other) < min_outlier_separation for other in outliers):
                continue
            chosen = candidate
            break

        if chosen is None:
            chosen = fallback_outlier_point(width, height, depot, centers)
        outliers.append(chosen)

    clients.extend(outliers)
    rng.shuffle(clients)
    return clients


def fallback_cluster_center(
    index: int,
    cluster_count: int,
    width: float,
    height: float,
    margin: float,
) -> tuple[float, float]:
    radius = max(min(width, height) * 0.28, margin)
    angle = (2.0 * math.pi * index) / cluster_count
    x = clamp(width / 2.0 + radius * math.cos(angle), margin, width - margin)
    y = clamp(height / 2.0 + radius * math.sin(angle), margin, height - margin)
    return rounded_point(x, y)


def sample_cluster_center(
    index: int,
    cluster_count: int,
    width: float,
    height: float,
    depot: tuple[float, float],
    existing_centers: list[tuple[float, float]],
    cluster_center_margin_ratio: float,
    cluster_center_separation_ratio: float,
    depot_separation_ratio: float,
    rng: random.Random,
) -> tuple[float, float]:
    min_size = min(width, height)
    margin = min_size * cluster_center_margin_ratio
    center_separation = min_size * cluster_center_separation_ratio
    depot_separation = min_size * depot_separation_ratio

    for _ in range(128):
        candidate = (
            rng.uniform(margin, width - margin),
            rng.uniform(margin, height - margin),
        )
        if euclidean(candidate, depot) < depot_separation:
            continue
        if any(euclidean(candidate, other) < center_separation for other in existing_centers):
            continue
        return rounded_point(candidate[0], candidate[1])

    fallback = fallback_cluster_center(index, cluster_count, width, height, margin)
    distance_to_depot = euclidean(fallback, depot)
    if distance_to_depot < depot_separation:
        dx = fallback[0] - depot[0]
        dy = fallback[1] - depot[1]
        if dx == 0.0 and dy == 0.0:
            dx = 1.0
        length = math.hypot(dx, dy)
        push = depot_separation - distance_to_depot + min_size * 0.05
        fallback = rounded_point(
            clamp(fallback[0] + push * dx / length, margin, width - margin),
            clamp(fallback[1] + push * dy / length, margin, height - margin),
        )
    return fallback


def generate_clustered_clients(
    client_count: int,
    width: float,
    height: float,
    depot: tuple[float, float],
    cluster_counts: list[int],
    cluster_spread_ratio: float,
    cluster_center_margin_ratio: float,
    cluster_center_separation_ratio: float,
    depot_separation_ratio: float,
    rng: random.Random,
) -> list[tuple[float, float]]:
    valid_cluster_counts = sorted({max(1, min(client_count, count)) for count in cluster_counts})
    cluster_count = rng.choice(valid_cluster_counts)
    sigma = max(min(width, height) * cluster_spread_ratio, 1e-6)
    centers = generate_cluster_centers(
        cluster_count=cluster_count,
        width=width,
        height=height,
        depot=depot,
        cluster_center_margin_ratio=cluster_center_margin_ratio,
        cluster_center_separation_ratio=cluster_center_separation_ratio,
        depot_separation_ratio=depot_separation_ratio,
        rng=rng,
    )
    return generate_clients_from_cluster_centers(
        client_count=client_count,
        centers=centers,
        sigma=sigma,
        width=width,
        height=height,
        rng=rng,
    )


def generate_instance(
    family: str,
    node_count: int,
    width: float,
    height: float,
    seed: int,
    cluster_counts: list[int],
    cluster_spread_ratio: float,
    cluster_center_margin_ratio: float,
    cluster_center_separation_ratio: float,
    depot_margin_ratio: float,
    depot_separation_ratio: float,
    mixed_outlier_ratio_min: float,
    mixed_outlier_ratio_max: float,
    mixed_outlier_cluster_distance_ratio: float,
    mixed_outlier_depot_distance_ratio: float,
    mixed_outlier_edge_band_ratio: float,
) -> list[tuple[float, float]]:
    rng = random.Random(seed)
    depot = choose_depot_position(family, width, height, depot_margin_ratio, rng)
    client_count = node_count - 1

    if family == "uniform":
        clients = generate_uniform_clients(client_count, width, height, rng)
    elif family in {"clustered-center", "clustered-offset-depot"}:
        clients = generate_clustered_clients(
            client_count=client_count,
            width=width,
            height=height,
            depot=depot,
            cluster_counts=cluster_counts,
            cluster_spread_ratio=cluster_spread_ratio,
            cluster_center_margin_ratio=cluster_center_margin_ratio,
            cluster_center_separation_ratio=cluster_center_separation_ratio,
            depot_separation_ratio=depot_separation_ratio,
            rng=rng,
        )
    elif family == "mixed-outliers":
        clients = generate_mixed_outliers_clients(
            client_count=client_count,
            width=width,
            height=height,
            depot=depot,
            cluster_counts=cluster_counts,
            cluster_spread_ratio=cluster_spread_ratio,
            cluster_center_margin_ratio=cluster_center_margin_ratio,
            cluster_center_separation_ratio=cluster_center_separation_ratio,
            depot_separation_ratio=depot_separation_ratio,
            mixed_outlier_ratio_min=mixed_outlier_ratio_min,
            mixed_outlier_ratio_max=mixed_outlier_ratio_max,
            mixed_outlier_cluster_distance_ratio=mixed_outlier_cluster_distance_ratio,
            mixed_outlier_depot_distance_ratio=mixed_outlier_depot_distance_ratio,
            mixed_outlier_edge_band_ratio=mixed_outlier_edge_band_ratio,
            rng=rng,
        )
    else:
        raise ValueError(f"Unknown family: {family}")

    return [depot] + clients


def write_instance(path: Path, salesman_count: int, coords: list[tuple[float, float]]) -> None:
    lines = [f"{len(coords)} {salesman_count}"]
    lines.extend(f"{x} {y}" for x, y in coords)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_filename(family: str, node_count: int, salesman_count: int, repeat: int) -> str:
    base_name = f"n{node_count}_m{salesman_count}_r{repeat:02d}.txt"
    return base_name if family == "uniform" else f"{family}_{base_name}"


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate reproducible synthetic mTSP benchmark instances.")
    parser.add_argument("--config", default="experiments/config.json", help="Path to the benchmark config JSON.")
    parser.add_argument("--output-dir", default=None, help="Directory for generated instances.")
    parser.add_argument("--node-counts", default=None, help="Comma-separated node counts.")
    parser.add_argument("--salesmen", default=None, help="Comma-separated salesman counts.")
    parser.add_argument("--repeats", type=int, default=None, help="Number of instances per (n, m) pair.")
    parser.add_argument("--base-seed", type=int, default=None, help="Base seed for reproducible generation.")
    parser.add_argument("--width", type=float, default=None, help="Synthetic map width.")
    parser.add_argument("--height", type=float, default=None, help="Synthetic map height.")
    parser.add_argument(
        "--families",
        default=None,
        help="Comma-separated instance families: uniform, clustered-center, clustered-offset-depot, mixed-outliers.",
    )
    parser.add_argument(
        "--cluster-counts",
        default=None,
        help="Comma-separated candidate cluster counts for clustered families.",
    )
    parser.add_argument(
        "--cluster-spread-ratio",
        type=float,
        default=None,
        help="Cluster standard deviation as a share of min(width, height).",
    )
    parser.add_argument(
        "--cluster-center-margin-ratio",
        type=float,
        default=None,
        help="Minimum margin from map border for sampled cluster centers.",
    )
    parser.add_argument(
        "--cluster-center-separation-ratio",
        type=float,
        default=None,
        help="Minimum separation between sampled cluster centers.",
    )
    parser.add_argument(
        "--depot-margin-ratio",
        type=float,
        default=None,
        help="Margin used when placing offset depots on map borders.",
    )
    parser.add_argument(
        "--depot-separation-ratio",
        type=float,
        default=None,
        help="Minimum separation between the depot and sampled cluster centers.",
    )
    parser.add_argument(
        "--mixed-outlier-ratio-min",
        type=float,
        default=None,
        help="Minimum share of outlier clients in mixed-outliers instances.",
    )
    parser.add_argument(
        "--mixed-outlier-ratio-max",
        type=float,
        default=None,
        help="Maximum share of outlier clients in mixed-outliers instances.",
    )
    parser.add_argument(
        "--mixed-outlier-cluster-distance-ratio",
        type=float,
        default=None,
        help="Minimum distance from outliers to cluster centers, as a share of min(width, height).",
    )
    parser.add_argument(
        "--mixed-outlier-depot-distance-ratio",
        type=float,
        default=None,
        help="Minimum distance from outliers to depot, as a share of min(width, height).",
    )
    parser.add_argument(
        "--mixed-outlier-edge-band-ratio",
        type=float,
        default=None,
        help="Border band width used to sample long-tail outliers.",
    )
    args = parser.parse_args()

    config_path = Path(args.config)
    config = json.loads(config_path.read_text(encoding="utf-8")) if config_path.exists() else {}
    generation = config.get("generation", {})

    output_dir = Path(args.output_dir or config.get("instance_dir", "data/mtsp/generated"))
    output_dir.mkdir(parents=True, exist_ok=True)

    node_counts = parse_int_values(args.node_counts, generation.get("node_counts", [20, 50, 100]))
    salesman_counts = parse_int_values(args.salesmen, generation.get("salesmen", [2, 3, 5]))
    extra_salesmen_by_node_count = parse_salesmen_overrides(generation.get("extra_salesmen_by_node_count", {}))
    families = parse_str_values(
        args.families,
        generation.get("families", ["uniform"]),
    )
    cluster_counts = parse_int_values(args.cluster_counts, generation.get("cluster_counts", [3, 4]))
    repeats = int(args.repeats if args.repeats is not None else generation.get("repeats", 3))
    base_seed = int(args.base_seed if args.base_seed is not None else generation.get("base_seed", 20260404))
    width = float(args.width if args.width is not None else generation.get("width", 100.0))
    height = float(args.height if args.height is not None else generation.get("height", 100.0))
    cluster_spread_ratio = float(
        args.cluster_spread_ratio
        if args.cluster_spread_ratio is not None
        else generation.get("cluster_spread_ratio", 0.08)
    )
    cluster_center_margin_ratio = float(
        args.cluster_center_margin_ratio
        if args.cluster_center_margin_ratio is not None
        else generation.get("cluster_center_margin_ratio", 0.18)
    )
    cluster_center_separation_ratio = float(
        args.cluster_center_separation_ratio
        if args.cluster_center_separation_ratio is not None
        else generation.get("cluster_center_separation_ratio", 0.2)
    )
    depot_margin_ratio = float(
        args.depot_margin_ratio
        if args.depot_margin_ratio is not None
        else generation.get("depot_margin_ratio", 0.08)
    )
    depot_separation_ratio = float(
        args.depot_separation_ratio
        if args.depot_separation_ratio is not None
        else generation.get("depot_separation_ratio", 0.18)
    )
    mixed_outlier_ratio_min = float(
        args.mixed_outlier_ratio_min
        if args.mixed_outlier_ratio_min is not None
        else generation.get("mixed_outlier_ratio_min", 0.1)
    )
    mixed_outlier_ratio_max = float(
        args.mixed_outlier_ratio_max
        if args.mixed_outlier_ratio_max is not None
        else generation.get("mixed_outlier_ratio_max", 0.2)
    )
    mixed_outlier_cluster_distance_ratio = float(
        args.mixed_outlier_cluster_distance_ratio
        if args.mixed_outlier_cluster_distance_ratio is not None
        else generation.get("mixed_outlier_cluster_distance_ratio", 0.3)
    )
    mixed_outlier_depot_distance_ratio = float(
        args.mixed_outlier_depot_distance_ratio
        if args.mixed_outlier_depot_distance_ratio is not None
        else generation.get("mixed_outlier_depot_distance_ratio", 0.22)
    )
    mixed_outlier_edge_band_ratio = float(
        args.mixed_outlier_edge_band_ratio
        if args.mixed_outlier_edge_band_ratio is not None
        else generation.get("mixed_outlier_edge_band_ratio", 0.12)
    )
    known_families = {"uniform", "clustered-center", "clustered-offset-depot", "mixed-outliers"}
    unknown_families = sorted(set(families) - known_families)
    if unknown_families:
        raise ValueError(f"Unknown families requested: {', '.join(unknown_families)}")
    if not 0.0 <= mixed_outlier_ratio_min <= mixed_outlier_ratio_max < 1.0:
        raise ValueError("mixed-outlier ratios must satisfy 0 <= min <= max < 1")

    for family in families:
        for node_count in node_counts:
            salesmen_for_node_count = sorted({
                *salesman_counts,
                *extra_salesmen_by_node_count.get(node_count, []),
            })
            for salesman_count in salesmen_for_node_count:
                for repeat in range(1, repeats + 1):
                    seed = base_seed + node_count * 1000 + salesman_count * 100 + repeat
                    coords = generate_instance(
                        family=family,
                        node_count=node_count,
                        width=width,
                        height=height,
                        seed=seed,
                        cluster_counts=cluster_counts,
                        cluster_spread_ratio=cluster_spread_ratio,
                        cluster_center_margin_ratio=cluster_center_margin_ratio,
                        cluster_center_separation_ratio=cluster_center_separation_ratio,
                        depot_margin_ratio=depot_margin_ratio,
                        depot_separation_ratio=depot_separation_ratio,
                        mixed_outlier_ratio_min=mixed_outlier_ratio_min,
                        mixed_outlier_ratio_max=mixed_outlier_ratio_max,
                        mixed_outlier_cluster_distance_ratio=mixed_outlier_cluster_distance_ratio,
                        mixed_outlier_depot_distance_ratio=mixed_outlier_depot_distance_ratio,
                        mixed_outlier_edge_band_ratio=mixed_outlier_edge_band_ratio,
                    )
                    write_instance(
                        output_dir / build_filename(family, node_count, salesman_count, repeat),
                        salesman_count,
                        coords,
                    )


if __name__ == "__main__":
    main()
