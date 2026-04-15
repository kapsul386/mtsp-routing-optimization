from __future__ import annotations

from typing import List, Tuple

def is_valid_tour(route: List[int], n: int) -> bool:
    if not route or len(route) != n + 1:
        return False
    if route[0] != route[-1]:
        return False
    inner = route[:-1]
    if min(inner) < 0 or max(inner) >= n:
        return False
    return len(set(inner)) == n


def validate_tour(route: List[int], n: int) -> Tuple[bool, str]:
    if not route:
        return False, "Empty route"
    if route[0] != route[-1]:
        return False, "Route must return to start"
    if len(route) != n + 1:
        return False, f"Route length {len(route)} != n+1 {n+1}"
    inner = route[:-1]
    if len(set(inner)) != n:
        return False, "Route does not visit each node exactly once"
    if min(inner) < 0 or max(inner) >= n:
        return False, "Node index out of bounds"
    return True, "OK"


