from __future__ import annotations

import re
from pathlib import Path


INSTANCE_NAME_RE = re.compile(r"^(?:(?P<family>.+)_)?n\d+_m\d+_r\d+\.txt$")
DEFAULT_INSTANCE_FAMILY = "uniform"
INSTANCE_FAMILY_ORDER = {
    "uniform": 0,
    "clustered-center": 1,
    "clustered-offset-depot": 2,
    "mixed-outliers": 3,
}


def infer_instance_family(value: str | None) -> str:
    if not value:
        return DEFAULT_INSTANCE_FAMILY

    match = INSTANCE_NAME_RE.match(Path(value).name)
    if match is None:
        return DEFAULT_INSTANCE_FAMILY
    return match.group("family") or DEFAULT_INSTANCE_FAMILY


def ensure_instance_family(row: dict[str, object], default_family: str = DEFAULT_INSTANCE_FAMILY) -> dict[str, object]:
    normalized = dict(row)
    family = str(normalized.get("instance_family", "")).strip()
    if family:
        normalized["instance_family"] = family
        return normalized

    for key in ("instance", "path"):
        value = str(normalized.get(key, "")).strip()
        if value:
            normalized["instance_family"] = infer_instance_family(value)
            return normalized

    normalized["instance_family"] = default_family
    return normalized


def ensure_instance_families(
    rows: list[dict[str, object]],
    default_family: str = DEFAULT_INSTANCE_FAMILY,
) -> list[dict[str, object]]:
    return [ensure_instance_family(row, default_family=default_family) for row in rows]


def instance_family_sort_key(family: str) -> tuple[int, str]:
    return INSTANCE_FAMILY_ORDER.get(family, len(INSTANCE_FAMILY_ORDER)), family
