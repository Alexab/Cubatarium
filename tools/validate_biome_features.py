#!/usr/bin/env python3
"""Validate biome feature weights reference objects from object_features.json."""

from __future__ import annotations

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BIOME_DIR = REPO / "content" / "worldgen_packs" / "default" / "biomes"
OBJECT_FEATURES = REPO / "content" / "object_features.json"

EXPECTED_BIOMES = (
    "plains",
    "forest",
    "desert",
    "hills",
    "tundra",
    "savanna",
    "foothills",
    "scrubland",
    "cold_steppe",
)


def object_names(features: dict) -> set[str]:
    names: set[str] = set()
    for pool in ("vegetation", "ground_cover", "decoration", "structures"):
        for rule in features.get(pool, []):
            if isinstance(rule, dict) and rule.get("object"):
                names.add(rule["object"])
            if isinstance(rule, dict) and rule.get("block"):
                names.add(rule["block"])
    return names


def validate() -> list[str]:
    failures: list[str] = []
    features = json.loads(OBJECT_FEATURES.read_text(encoding="utf-8"))
    known = object_names(features)

    for biome_id in EXPECTED_BIOMES:
        path = BIOME_DIR / f"{biome_id}.json"
        if not path.is_file():
            failures.append(f"missing biome file {path.name}")
            continue
        biome = json.loads(path.read_text(encoding="utf-8"))
        biome_features = biome.get("features", {})
        if not biome_features:
            failures.append(f"{biome_id}: missing features section")
            continue
        has_veg_pool = False
        for pool_name, pool in biome_features.items():
            if pool_name not in ("vegetation", "ground_cover", "decoration"):
                failures.append(f"{biome_id}: unknown feature pool {pool_name!r}")
                continue
            if pool_name in ("vegetation", "ground_cover") and pool:
                has_veg_pool = True
            if not isinstance(pool, dict):
                failures.append(f"{biome_id}: {pool_name} must be an object")
                continue
            for key in pool:
                if key in known:
                    continue
                if f"{key}_mapgen" in known or key.endswith("_mapgen"):
                    failures.append(
                        f"{biome_id}: legacy feature key {key!r}; use object_features name"
                    )
                else:
                    failures.append(
                        f"{biome_id}: feature {key!r} not found in object_features.json"
                    )
        if not has_veg_pool:
            failures.append(f"{biome_id}: no vegetation or ground_cover weights")

    rule_biomes: dict[str, set[str]] = {b: set() for b in EXPECTED_BIOMES}
    for pool in ("vegetation", "ground_cover"):
        for rule in features.get(pool, []):
            if not isinstance(rule, dict):
                continue
            for biome_id in rule.get("biomes", []):
                if biome_id in rule_biomes:
                    rule_biomes[biome_id].add(pool)
    for biome_id in EXPECTED_BIOMES:
        if not rule_biomes[biome_id]:
            failures.append(
                f"{biome_id}: no vegetation/ground_cover rules in object_features.json"
            )

    return failures


def main() -> int:
    failures = validate()
    if failures:
        print("validate_biome_features: FAILED", file=sys.stderr)
        for line in failures:
            print(f"  - {line}", file=sys.stderr)
        return 1
    print("validate_biome_features: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
