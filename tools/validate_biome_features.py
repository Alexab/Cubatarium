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
    "mountains",
    "snowy_slopes",
)

BIOME_CONFIGS = EXPECTED_BIOMES + (
    "ocean",
    "beach",
    "jagged_peaks",
    "lush_caves",
    "dripstone_caves",
    "deep_dark",
)
CAVE_BIOMES = ("lush_caves", "dripstone_caves", "deep_dark")
CAVE_FEATURE_KINDS = {"moss_patch", "stalagmite", "amethyst_cluster"}
WORLDGEN_PACKS = ("minetest_default_16", "cubatarium_geology_cc0", "cubatarium_worldgen_cc0")


def object_names(features: dict) -> set[str]:
    names: set[str] = set()
    for pool in ("vegetation", "ground_cover", "decoration", "structures"):
        for rule in features.get(pool, []):
            if isinstance(rule, dict) and rule.get("object"):
                names.add(rule["object"])
            if isinstance(rule, dict) and rule.get("species"):
                names.add(rule["species"])
            if isinstance(rule, dict) and rule.get("block"):
                names.add(rule["block"])
    return names


def validate() -> list[str]:
    failures: list[str] = []
    features = json.loads(OBJECT_FEATURES.read_text(encoding="utf-8"))
    known = object_names(features)
    worldgen_refs = json.loads(
        (REPO / "content" / "worldgen_refs.json").read_text(encoding="utf-8")
    )
    known_blocks = {
        name
        for slot in worldgen_refs.get("slots", {}).values()
        for name in slot.get("block_names", [])
    }
    available_pack_blocks: set[str] = set()
    for pack_id in WORLDGEN_PACKS:
        blocks_dir = REPO / "resource_packs" / pack_id / "blocks"
        if not blocks_dir.is_dir():
            failures.append(f"missing recommended resource pack blocks: {pack_id}")
            continue
        for path in blocks_dir.glob("*.json"):
            try:
                block = json.loads(path.read_text(encoding="utf-8-sig"))
            except (OSError, json.JSONDecodeError) as exc:
                failures.append(f"{pack_id}/{path.name}: invalid block JSON ({exc})")
                continue
            if isinstance(block.get("name"), str):
                available_pack_blocks.add(block["name"])

    for slot in (
        "granite",
        "diorite",
        "andesite",
        "deepslate",
        "tuff",
        "calcite",
        "smooth_basalt",
        "amethyst_block",
        "moss_block",
        "rooted_dirt",
        "ore_coal",
        "ore_iron",
    ):
        spec = worldgen_refs.get("slots", {}).get(slot, {})
        names = spec.get("block_names", []) if isinstance(spec, dict) else []
        if not any(name in available_pack_blocks for name in names):
            failures.append(
                f"worldgen_refs: slot {slot!r} has no block in the recommended packs"
            )

    ore_config = json.loads(
        (REPO / "content" / "worldgen_packs" / "default" / "ores.json").read_text(
            encoding="utf-8"
        )
    )
    for rule in ore_config.get("ores", []):
        if not isinstance(rule, dict):
            failures.append("ores.json: ore rule must be an object")
            continue
        slot = rule.get("slot")
        if not slot or slot not in worldgen_refs.get("slots", {}):
            failures.append(f"ores.json: ore slot {slot!r} is not in worldgen_refs")

    cave_rules = features.get("cave_features", [])
    if not cave_rules:
        failures.append("object_features: missing cave_features rules")
    for rule in cave_rules:
        if not isinstance(rule, dict):
            failures.append("object_features: cave feature rule must be an object")
            continue
        if rule.get("biome") not in CAVE_BIOMES:
            failures.append(
                f"object_features: unknown cave biome {rule.get('biome')!r}"
            )
        if rule.get("kind") not in CAVE_FEATURE_KINDS:
            failures.append(
                f"object_features: unknown cave feature kind {rule.get('kind')!r}"
            )
        if rule.get("block") not in known_blocks:
            failures.append(
                f"object_features: cave block {rule.get('block')!r} is not in worldgen_refs"
            )
        if rule.get("block") not in available_pack_blocks:
            failures.append(
                f"object_features: cave block {rule.get('block')!r} is absent from the recommended packs"
            )

    for biome_id in BIOME_CONFIGS:
        path = BIOME_DIR / f"{biome_id}.json"
        if not path.is_file():
            failures.append(f"missing biome file {path.name}")
            continue
        biome = json.loads(path.read_text(encoding="utf-8"))
        biome_features = biome.get("features", {})
        if not isinstance(biome.get("palette"), dict):
            failures.append(f"{biome_id}: missing palette section")
        if biome_id not in EXPECTED_BIOMES:
            continue
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

    known_biomes = set(BIOME_CONFIGS)
    rule_biomes: dict[str, set[str]] = {b: set() for b in EXPECTED_BIOMES}
    for pool in ("vegetation", "ground_cover"):
        for rule in features.get(pool, []):
            if not isinstance(rule, dict):
                continue
            for biome_id in rule.get("biomes", []):
                if biome_id not in known_biomes:
                    failures.append(f"object_features: unknown biome {biome_id!r}")
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
