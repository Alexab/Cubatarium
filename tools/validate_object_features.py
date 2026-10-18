#!/usr/bin/env python3
"""Validate content/object_features.json against objects/ and manifest."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[1]
FEATURES = REPO / "content" / "object_features.json"
MANIFEST = REPO / "tools" / "object_manifest.yaml"
OBJECTS = REPO / "objects"
DEFAULT_BLOCKS = REPO / "resource_packs" / "minetest_default_16" / "blocks"

VALID_SURFACE_CONSTRAINTS = {
    "any_land",
    "grass",
    "near_water",
    "water_surface",
    "underwater_floor",
}


def load_object_names() -> set[str]:
    names: set[str] = set()
    for path in OBJECTS.rglob("*.json"):
        if not path.is_file():
            continue
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        name = data.get("name") or path.stem
        names.add(name)
    return names


def load_block_names() -> set[str]:
    names: set[str] = set()
    for path in DEFAULT_BLOCKS.glob("*.json"):
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        name = data.get("name") or path.stem
        names.add(name)
    return names


def validate_ecology(ecology: object, label: str, errors: list[str]) -> None:
    if ecology is None:
        return
    if not isinstance(ecology, dict):
        errors.append(f"{label}: ecology must be an object")
        return

    for axis in ("temperature", "moisture"):
        if axis not in ecology:
            continue
        value = ecology[axis]
        if (
            not isinstance(value, list)
            or len(value) != 2
            or any(isinstance(v, bool) or not isinstance(v, (int, float)) for v in value)
            or not all(math.isfinite(v) for v in value)
            or not (0 <= value[0] < value[1] <= 1)
        ):
            errors.append(f"{label}: invalid ecology {axis} range")

    if "elevation" in ecology:
        value = ecology["elevation"]
        if (
            not isinstance(value, list)
            or len(value) != 2
            or any(isinstance(v, bool) or not isinstance(v, int) for v in value)
            or value[0] > value[1]
        ):
            errors.append(f"{label}: invalid ecology elevation range")

    if "max_slope" in ecology:
        slope = ecology["max_slope"]
        if (
            isinstance(slope, bool)
            or not isinstance(slope, (int, float))
            or not math.isfinite(slope)
            or slope < 0
        ):
            errors.append(f"{label}: ecology max_slope must be non-negative")


def manifest_worldgen() -> dict[str, list[dict]]:
    manifest = yaml.safe_load(MANIFEST.read_text(encoding="utf-8")) or {}
    objects = manifest.get("objects") or {}
    out: dict[str, list[dict]] = {}
    for name, meta in objects.items():
        wg = meta.get("worldgen")
        if isinstance(wg, dict):
            out.setdefault(name, []).append(wg)
        elif isinstance(wg, list):
            out[name] = [entry for entry in wg if isinstance(entry, dict)]
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Error when manifest worldgen fields diverge from JSON (non-calibrated)",
    )
    args = parser.parse_args()

    errors: list[str] = []
    warnings: list[str] = []

    if not FEATURES.is_file():
        errors.append(f"missing {FEATURES}")
        print_errors(errors, warnings)
        return 1

    object_names = load_object_names()
    block_names = load_block_names()
    manifest_wg = manifest_worldgen()
    data = json.loads(FEATURES.read_text(encoding="utf-8-sig"))

    json_objects_by_pool: dict[str, set[str]] = {
        "vegetation": set(),
        "decoration": set(),
        "structures": set(),
    }

    for pool_name in ("vegetation", "decoration", "structures"):
        rules = data.get(pool_name, [])
        if not isinstance(rules, list):
            errors.append(f"{pool_name}: expected array")
            continue

        seen_keys: set[tuple[str, tuple[str, ...]]] = set()
        for idx, rule in enumerate(rules):
            if not isinstance(rule, dict):
                errors.append(f"{pool_name}[{idx}]: not an object")
                continue

            mode = rule.get("mode", "object")
            object_name = rule.get("object")
            block = rule.get("block")
            biomes = rule.get("biomes") or []
            weight = rule.get("weight", 1)

            surface = rule.get("surface_constraint")
            if surface is not None and surface not in VALID_SURFACE_CONSTRAINTS:
                errors.append(
                    f"{pool_name}[{idx}]: invalid surface_constraint {surface!r}"
                )
            if surface == "underwater_floor" and mode != "scatter_blocks":
                errors.append(
                    f"{pool_name}[{idx}]: underwater_floor currently requires scatter_blocks"
                )
            validate_ecology(rule.get("ecology"), f"{pool_name}[{idx}]", errors)

            if not biomes:
                errors.append(f"{pool_name}[{idx}]: empty biomes")

            if weight < 1 or weight > 10:
                errors.append(
                    f"{pool_name}[{idx}]: weight {weight} not in [1, 10]"
                )

            if mode == "scatter_blocks":
                if not block:
                    errors.append(f"{pool_name}[{idx}]: scatter_blocks missing block")
                elif block not in block_names:
                    errors.append(
                        f"{pool_name}[{idx}]: unknown scatter block {block!r}"
                    )
                scatter_key = f"scatter:{block}"
                json_objects_by_pool[pool_name].add(scatter_key)
                if pool_name in ("vegetation", "decoration"):
                    spacing = rule.get("spacing")
                    if spacing is None:
                        errors.append(f"{pool_name}[{idx}]: missing spacing")
                    elif spacing < 8 or spacing > 256:
                        errors.append(
                            f"{pool_name}[{idx}]: spacing {spacing} not in [8, 256]"
                        )
                key = (scatter_key, tuple(sorted(biomes)))
                if key in seen_keys:
                    warnings.append(
                        f"{pool_name}[{idx}]: duplicate scatter rule {key}"
                    )
                seen_keys.add(key)
                continue

            if mode == "procedural_tree":
                species = rule.get("species")
                profile = rule.get("tree_profile")
                if not isinstance(species, str) or not species:
                    errors.append(f"{pool_name}[{idx}]: missing procedural species")
                    continue
                if not isinstance(profile, dict):
                    errors.append(f"{pool_name}[{idx}]: missing tree_profile")
                    continue
                for block_field, fallback in (
                    ("trunk_block", "tree_log"),
                    ("leaves_block", "tree_leaves"),
                ):
                    block_name = profile.get(block_field, fallback)
                    if block_name not in block_names and fallback not in block_names:
                        errors.append(
                            f"{pool_name}[{idx}]: {block_field} {block_name!r} "
                            "has no default-pack fallback"
                        )
                height_min = profile.get("height_min", 5)
                height_max = profile.get("height_max", 8)
                if (
                    not isinstance(height_min, int)
                    or not isinstance(height_max, int)
                    or not (2 <= height_min <= height_max <= 40)
                ):
                    errors.append(f"{pool_name}[{idx}]: invalid tree height range")
                spacing = rule.get("spacing")
                if spacing is None:
                    errors.append(f"{pool_name}[{idx}]: missing spacing")
                elif not isinstance(spacing, int) or spacing < 8 or spacing > 256:
                    errors.append(
                        f"{pool_name}[{idx}]: spacing {spacing} not in [8, 256]"
                    )
                key = (f"procedural:{species}", tuple(sorted(biomes)))
                if key in seen_keys:
                    warnings.append(
                        f"{pool_name}[{idx}]: duplicate procedural rule {key}"
                    )
                seen_keys.add(key)
                continue

            if not object_name:
                errors.append(f"{pool_name}[{idx}]: missing object")
                continue

            json_objects_by_pool[pool_name].add(object_name)

            if object_name not in object_names:
                errors.append(f"{pool_name}[{idx}]: unknown object {object_name!r}")

            if pool_name in ("vegetation", "decoration"):
                spacing = rule.get("spacing")
                if spacing is None:
                    errors.append(f"{pool_name}[{idx}]: missing spacing")
                elif not rule.get("calibrated") and (
                    spacing < 8 or spacing > 256
                ):
                    errors.append(
                        f"{pool_name}[{idx}]: spacing {spacing} not in [8, 256]"
                    )
                elif rule.get("calibrated") and spacing < 8:
                    errors.append(f"{pool_name}[{idx}]: spacing {spacing} < 8")
            elif pool_name == "structures":
                chance = rule.get("chance_per_column")
                if chance is None:
                    errors.append(f"{pool_name}[{idx}]: missing chance_per_column")
                elif chance < 64 or chance > 10000:
                    errors.append(
                        f"{pool_name}[{idx}]: chance_per_column {chance} not in [64, 10000]"
                    )

            if rule.get("calibrated"):
                key = (object_name, tuple(sorted(biomes)))
                if key in seen_keys:
                    warnings.append(f"{pool_name}[{idx}]: duplicate calibrated rule {key}")
                seen_keys.add(key)
                continue

            if object_name in manifest_wg and args.strict:
                manifest_entries = manifest_wg[object_name]
                matched = False
                for wg in manifest_entries:
                    if tuple(sorted(wg.get("biomes") or [])) != tuple(sorted(biomes)):
                        continue
                    matched = True
                    for field in ("spacing", "weight", "seed_offset"):
                        if field in wg and wg[field] != rule.get(field):
                            errors.append(
                                f"{pool_name}[{idx}]: {object_name} {field} drift "
                                f"(manifest {wg[field]!r} vs json {rule.get(field)!r})"
                            )
                    manifest_surface = wg.get("surface_constraint")
                    if manifest_surface and manifest_surface != surface:
                        errors.append(
                            f"{pool_name}[{idx}]: {object_name} surface_constraint drift"
                        )
                if not matched and len(manifest_entries) == 1:
                    warnings.append(
                        f"{pool_name}[{idx}]: {object_name!r} biomes differ from manifest "
                        "(mark calibrated: true if intentional)"
                    )
            elif object_name not in manifest_wg:
                warnings.append(
                    f"{pool_name}[{idx}]: {object_name!r} in JSON but no worldgen in manifest"
                )

            key = (object_name, tuple(sorted(biomes)))
            if key in seen_keys:
                warnings.append(f"{pool_name}[{idx}]: duplicate rule {key}")
            seen_keys.add(key)

    for name, wg_list in manifest_wg.items():
        for wg in wg_list:
            pool = wg.get("pool")
            if pool not in json_objects_by_pool:
                continue
            mode = wg.get("mode", "object")
            if mode == "scatter_blocks":
                block = wg.get("block", "?")
                key = f"scatter:{block}"
            else:
                key = name
            if key not in json_objects_by_pool[pool]:
                warnings.append(
                    f"manifest worldgen for {name!r} missing from object_features.json "
                    f"(run generate_object_features.py)"
                )

    print_errors(errors, warnings)
    return 1 if errors else 0


def print_errors(errors: list[str], warnings: list[str]) -> None:
    for warn in warnings:
        print(f"  WARNING: {warn}", file=sys.stderr)
    if errors:
        print("validate_object_features: FAILED", file=sys.stderr)
        for err in errors:
            print(f"  {err}", file=sys.stderr)
    else:
        print("validate_object_features: OK")


if __name__ == "__main__":
    raise SystemExit(main())
