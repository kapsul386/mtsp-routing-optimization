from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class BaselinePolicy:
    small_max_nodes: int
    medium_max_nodes: int
    tiers: dict[str, list[str]]


def build_policy(config: dict) -> BaselinePolicy:
    small_max_nodes = int(config.get("small_max_nodes", 100))
    medium_max_nodes = int(config.get("medium_max_nodes", 1000))
    tiers = {
        tier_name: [str(name) for name in config.get("tiers", {}).get(tier_name, [])]
        for tier_name in ("small", "medium", "large")
    }
    return BaselinePolicy(
        small_max_nodes=small_max_nodes,
        medium_max_nodes=medium_max_nodes,
        tiers=tiers,
    )


def select_tier(node_count: int, policy: BaselinePolicy) -> str:
    if node_count <= policy.small_max_nodes:
        return "small"
    if node_count <= policy.medium_max_nodes:
        return "medium"
    return "large"


def allowed_baselines(node_count: int, policy: BaselinePolicy) -> list[str]:
    return list(policy.tiers.get(select_tier(node_count, policy), []))


def choose_best_candidate(rows: list[dict]) -> dict | None:
    valid_rows = [
        row for row in rows
        if str(row.get("valid", "")).lower() == "true" and str(row.get("objective", "")).strip() not in ("", "None")
    ]
    if not valid_rows:
        return None
    return min(
        valid_rows,
        key=lambda row: (
            float(row["objective"]),
            float(row.get("time_seconds", 0.0) or 0.0),
            str(row.get("baseline_solver", "")),
        ),
    )
