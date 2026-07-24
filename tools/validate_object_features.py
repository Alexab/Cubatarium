#!/usr/bin/env python3
"""Validate content/object_features.json against objects/ and manifest."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[1]
FEATURES = REPO / "content" / "object_features.json"
MANIFEST = REPO / "tools" / "object_manifest.yaml"
OBJECTS = REPO / "objects"

VALID_SURFACE_CONSTRAINTS = {
    "any_land",
    "grass",
    "near_water",
    "water_surface",
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

            if not biomes:
                errors.append(f"{pool_name}[{idx}]: empty biomes")

            if weight < 1 or weight > 10:
                errors.append(
                    f"{pool_name}[{idx}]: weight {weight} not in [1, 10]"
                )

            if mode == "scatter_blocks":
                if not block:
                    errors.append(f"{pool_name}[{idx}]: scatter_blocks missing block")
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
