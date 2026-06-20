#!/usr/bin/env python3
"""Validate content/prefab_features.json against prefabs and manifest."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[1]
FEATURES = REPO / "content" / "prefab_features.json"
MANIFEST = REPO / "tools" / "prefab_manifest.yaml"
PREFABS = REPO / "prefabs"


def load_prefab_names() -> set[str]:
    names: set[str] = set()
    for path in PREFABS.rglob("*.json"):
        if not path.is_file():
            continue
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        name = data.get("name") or path.stem
        names.add(name)
    return names


def manifest_worldgen() -> dict[str, dict]:
    manifest = yaml.safe_load(MANIFEST.read_text(encoding="utf-8")) or {}
    prefabs = manifest.get("prefabs") or {}
    out: dict[str, dict] = {}
    for name, meta in prefabs.items():
        wg = meta.get("worldgen")
        if isinstance(wg, dict):
            out[name] = wg
    return out


def main() -> int:
    errors: list[str] = []
    warnings: list[str] = []

    if not FEATURES.is_file():
        errors.append(f"missing {FEATURES}")
        print_errors(errors, warnings)
        return 1

    prefab_names = load_prefab_names()
    manifest_wg = manifest_worldgen()
    data = json.loads(FEATURES.read_text(encoding="utf-8-sig"))

    json_prefabs_by_pool: dict[str, set[str]] = {
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

            mode = rule.get("mode", "prefab")
            prefab = rule.get("prefab")
            block = rule.get("block")
            biomes = rule.get("biomes") or []
            weight = rule.get("weight", 1)

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
                json_prefabs_by_pool[pool_name].add(scatter_key)
                if pool_name in ("vegetation", "decoration"):
                    spacing = rule.get("spacing")
                    if spacing is None:
                        errors.append(f"{pool_name}[{idx}]: missing spacing")
                    elif spacing < 8 or spacing > 256:
                        errors.append(
                            f"{pool_name}[{idx}]: spacing {spacing} not in [8, 256]"
                        )
                if block and block not in manifest_wg:
                    pass  # scatter rules live only in JSON
                key = (scatter_key, tuple(sorted(biomes)))
                if key in seen_keys:
                    warnings.append(
                        f"{pool_name}[{idx}]: duplicate scatter rule {key}"
                    )
                seen_keys.add(key)
                continue

            if not prefab:
                errors.append(f"{pool_name}[{idx}]: missing prefab")
                continue

            json_prefabs_by_pool[pool_name].add(prefab)

            if prefab not in prefab_names:
                errors.append(f"{pool_name}[{idx}]: unknown prefab {prefab!r}")

            if pool_name in ("vegetation", "decoration"):
                spacing = rule.get("spacing")
                if spacing is None:
                    errors.append(f"{pool_name}[{idx}]: missing spacing")
                elif spacing < 8 or spacing > 256:
                    errors.append(
                        f"{pool_name}[{idx}]: spacing {spacing} not in [8, 256]"
                    )
            elif pool_name == "structures":
                chance = rule.get("chance_per_column")
                if chance is None:
                    errors.append(f"{pool_name}[{idx}]: missing chance_per_column")
                elif chance < 64 or chance > 10000:
                    errors.append(
                        f"{pool_name}[{idx}]: chance_per_column {chance} not in [64, 10000]"
                    )

            if prefab.startswith("tree_") and not prefab.endswith("_mapgen"):
                warnings.append(
                    f"{pool_name}[{idx}]: tree prefab {prefab!r} without _mapgen suffix"
                )

            if prefab in manifest_wg:
                pass
            elif prefab not in manifest_wg:
                warnings.append(
                    f"{pool_name}[{idx}]: {prefab!r} in JSON but no worldgen in manifest"
                )

            key = (prefab, tuple(sorted(biomes)))
            if key in seen_keys:
                warnings.append(f"{pool_name}[{idx}]: duplicate rule {key}")
            seen_keys.add(key)

    for name, wg in manifest_wg.items():
        pool = wg.get("pool")
        if pool not in json_prefabs_by_pool:
            continue
        mode = wg.get("mode", "prefab")
        if mode == "scatter_blocks":
            block = wg.get("block", "?")
            key = f"scatter:{block}"
        else:
            key = name
        if key not in json_prefabs_by_pool[pool]:
            warnings.append(
                f"manifest worldgen for {name!r} missing from prefab_features.json "
                f"(run generate_prefab_features.py)"
            )

    print_errors(errors, warnings)
    return 1 if errors else 0


def print_errors(errors: list[str], warnings: list[str]) -> None:
    for warn in warnings:
        print(f"  WARNING: {warn}", file=sys.stderr)
    if errors:
        print("validate_prefab_features: FAILED", file=sys.stderr)
        for err in errors:
            print(f"  {err}", file=sys.stderr)
    else:
        print("validate_prefab_features: OK")


if __name__ == "__main__":
    raise SystemExit(main())
