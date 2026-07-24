#!/usr/bin/env python3
"""Generate content/prefab_features.json from tools/prefab_manifest.yaml."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[1]
MANIFEST = REPO / "tools" / "prefab_manifest.yaml"
OUT_JSON = REPO / "content" / "prefab_features.json"

VALID_SURFACE_CONSTRAINTS = {
    "any_land",
    "grass",
    "near_water",
    "water_surface",
}


def iter_worldgen_entries(meta: dict) -> list[dict]:
    wg = meta.get("worldgen")
    if not wg:
        return []
    if isinstance(wg, list):
        return [entry for entry in wg if isinstance(entry, dict)]
    if isinstance(wg, dict):
        return [wg]
    return []


def build_rule(name: str, meta: dict, wg: dict) -> dict | None:
    pool = wg.get("pool")
    if not pool:
        return None

    mode = wg.get("mode", "prefab")
    if mode == "scatter_blocks":
        entry = {
            "mode": "scatter_blocks",
            "block": wg.get("block", ""),
            "attempts": wg.get("attempts", 4),
            "radius": wg.get("radius", 2),
            "biomes": wg.get("biomes", []),
            "weight": wg.get("weight", 1),
            "seed_offset": wg.get("seed_offset", 0),
        }
        if wg.get("spacing"):
            entry["spacing"] = wg["spacing"]
        if wg.get("sub_biomes"):
            entry["sub_biomes"] = wg["sub_biomes"]
        return entry

    entry = {
        "prefab": name,
        "biomes": wg.get("biomes", []),
        "weight": wg.get("weight", 1),
        "seed_offset": wg.get("seed_offset", 0),
    }
    if wg.get("spacing"):
        entry["spacing"] = wg["spacing"]
    if wg.get("chance_per_column"):
        entry["chance_per_column"] = wg["chance_per_column"]
    placement = meta.get("placement") or {}
    if placement.get("y_offset") is not None:
        entry["placement_y_offset"] = placement["y_offset"]
    if wg.get("sub_biomes"):
        entry["sub_biomes"] = wg["sub_biomes"]
    surface = wg.get("surface_constraint")
    if surface:
        entry["surface_constraint"] = surface
    return entry


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--merge-calibrated",
        action="store_true",
        help="Keep rules from existing JSON marked calibrated: true",
    )
    args = parser.parse_args()

    manifest = yaml.safe_load(MANIFEST.read_text(encoding="utf-8")) or {}
    prefabs = manifest.get("prefabs") or {}

    pools: dict[str, list] = {
        "vegetation": [],
        "ground_cover": [],
        "decoration": [],
        "structures": [],
    }

    for name, meta in sorted(prefabs.items()):
        if meta.get("import_status") not in (None, "ready"):
            continue
        for wg in iter_worldgen_entries(meta):
            pool = wg.get("pool")
            if pool not in pools:
                continue
            rule = build_rule(name, meta, wg)
            if rule:
                pools[pool].append(rule)

    calibrated_rules: dict[str, list] = {k: [] for k in pools}
    if args.merge_calibrated and OUT_JSON.is_file():
        existing = json.loads(OUT_JSON.read_text(encoding="utf-8-sig"))
        for pool_name in pools:
            for rule in existing.get(pool_name, []):
                if isinstance(rule, dict) and rule.get("calibrated"):
                    calibrated_rules[pool_name].append(rule)

    for pool_name in pools:
        pools[pool_name].extend(calibrated_rules[pool_name])

    doc = {"schema_version": 1, **pools}
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    total = sum(len(v) for v in pools.values())
    print(f"Wrote {OUT_JSON} ({total} rules)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
