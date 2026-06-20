#!/usr/bin/env python3
"""Validate Cubatarium prefab JSON files."""

from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path

import yaml

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


def main() -> int:
    known = known_block_names()
    errors: list[str] = []
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

        if max_coord > 64:
            errors.append(f"{path}: bounds exceed 64 ({max_coord})")
        if len(blocks) > 10000:
            errors.append(f"{path}: too many blocks ({len(blocks)})")

    if errors:
        print("validate_prefabs: FAILED", file=sys.stderr)
        for err in errors:
            print(f"  {err}", file=sys.stderr)
        return 1

    print(f"validate_prefabs: OK ({len(names_seen)} prefabs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
