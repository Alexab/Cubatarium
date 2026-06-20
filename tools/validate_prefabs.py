#!/usr/bin/env python3
"""Validate Cubatarium prefab JSON files."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import yaml

from prefab_bounds import prefab_bounds

REPO = Path(__file__).resolve().parents[1]
PREFABS = REPO / "prefabs"
CANONICAL = REPO / "tools" / "canonical_blocks.yaml"
PACKS = REPO / "resource_packs"


def known_block_names() -> set[str]:
    names: set[str] = set()
    data = yaml.safe_load(CANONICAL.read_text(encoding="utf-8")) or {}
    names.update((data.get("tier_a") or {}).keys())
    names.update((data.get("tier_b") or {}).keys())
    if PACKS.is_dir():
        for pack in PACKS.iterdir():
            blocks_dir = pack / "blocks"
            if not blocks_dir.is_dir():
                continue
            for path in blocks_dir.glob("*.json"):
                try:
                    block = json.loads(path.read_text(encoding="utf-8-sig"))
                except (OSError, json.JSONDecodeError):
                    continue
                if block.get("name"):
                    names.add(block["name"])
    return names


def iter_prefab_files() -> list[Path]:
    files: list[Path] = []
    if not PREFABS.is_dir():
        return files
    for path in PREFABS.rglob("*.json"):
        if path.is_file():
            files.append(path)
    return sorted(files)


def check_semantic_rules(
    path: Path,
    name: str,
    data: dict,
    bounds: dict,
    *,
    warn_legacy: bool,
) -> tuple[list[str], list[str]]:
    errors: list[str] = []
    warnings: list[str] = []
    category = data.get("category") or "misc"

    if category == "plants" and name.startswith("tree_"):
        if bounds["size_y"] < 4:
            errors.append(
                f"{path}: tree {name!r} height {bounds['size_y']} < 4 (dy span)"
            )
        if not bounds["has_log"] and not bounds["has_cactus"]:
            errors.append(f"{path}: tree {name!r} has no tree_log or cactus stem")

    if category == "plants" and name.startswith("bush_"):
        if bounds["size_y"] < 2:
            errors.append(
                f"{path}: bush {name!r} height {bounds['size_y']} < 2"
            )
        if bounds["block_count"] < 9:
            errors.append(
                f"{path}: bush {name!r} block count {bounds['block_count']} < 9"
            )

    if category == "plants" and name.startswith("cactus_"):
        if bounds["size_y"] < 3:
            errors.append(
                f"{path}: cactus {name!r} height {bounds['size_y']} < 3"
            )

    if category == "misc" and name in ("tree_small", "tree_large"):
        msg = f"{path}: legacy misc tree {name!r} (size_y={bounds['size_y']})"
        if warn_legacy:
            warnings.append(msg)
        else:
            errors.append(msg)

    return errors, warnings


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Cubatarium prefab JSON")
    parser.add_argument(
        "--warn-legacy",
        action="store_true",
        default=True,
        help="Warn instead of fail for legacy misc trees (default)",
    )
    parser.add_argument(
        "--strict-legacy",
        action="store_true",
        help="Fail on legacy misc trees",
    )
    args = parser.parse_args()
    warn_legacy = args.warn_legacy and not args.strict_legacy

    known = known_block_names()
    errors: list[str] = []
    warnings: list[str] = []
    names_seen: dict[str, Path] = {}

    for path in iter_prefab_files():
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"{path}: parse error: {exc}")
            continue

        name = data.get("name") or path.stem
        if name in names_seen:
            errors.append(f"duplicate prefab name {name!r}: {path} and {names_seen[name]}")
        names_seen[name] = path

        blocks = data.get("blocks")
        if not isinstance(blocks, list) or not blocks:
            errors.append(f"{path}: empty or missing blocks[]")
            continue

        coords: set[tuple[int, int, int]] = set()
        max_coord = 0
        valid_blocks: list[dict] = []
        for block in blocks:
            if not isinstance(block, dict):
                errors.append(f"{path}: invalid block entry")
                continue
            try:
                dx, dy, dz = int(block["dx"]), int(block["dy"]), int(block["dz"])
                btype = block["type"]
            except (KeyError, TypeError, ValueError):
                errors.append(f"{path}: malformed block entry")
                continue
            if btype not in known:
                errors.append(f"{path}: unknown block type {btype!r}")
            key = (dx, dy, dz)
            if key in coords:
                errors.append(f"{path}: duplicate voxel at {key}")
            coords.add(key)
            max_coord = max(max_coord, abs(dx), abs(dy), abs(dz))
            valid_blocks.append(block)

        if max_coord > 64:
            errors.append(f"{path}: bounds exceed 64 ({max_coord})")
        if len(blocks) > 10000:
            errors.append(f"{path}: too many blocks ({len(blocks)})")

        bounds = prefab_bounds(valid_blocks)
        sem_errors, sem_warnings = check_semantic_rules(
            path, name, data, bounds, warn_legacy=warn_legacy
        )
        errors.extend(sem_errors)
        warnings.extend(sem_warnings)

    for warn in warnings:
        print(f"  WARNING: {warn}", file=sys.stderr)

    if errors:
        print("validate_prefabs: FAILED", file=sys.stderr)
        for err in errors:
            print(f"  {err}", file=sys.stderr)
        return 1

    print(f"validate_prefabs: OK ({len(names_seen)} prefabs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
