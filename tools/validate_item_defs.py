#!/usr/bin/env python3
"""Validate content/items/*.json against completeness gate from the CC0 import plan."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "content" / "items"
KNOWN_TYPES = {
    "mining",
    "cutting",
    "digging",
    "combat",
    "utility",
    "armor",
    "misc",
    "head",
    "chest",
    "arms",
    "hands",
    "legs",
    "feet",
    "tool",
}


def model_exists(model: str) -> bool:
    if not model:
        return False
    p = ROOT / model
    if p.is_file():
        return True
    # sibling folder convention
    return False


def main() -> int:
    errors: list[str] = []
    for path in sorted(CONTENT.glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        item_id = data.get("id") or path.stem
        if item_id != path.stem:
            errors.append(f"{path.name}: id={item_id} != filename")
        model = data.get("model", "")
        if not model_exists(model):
            # also accept models/items/<id>.json if model points to missing gltf but parts exist
            parts = ROOT / "models" / "items" / f"{item_id}.json"
            gltf = ROOT / "models" / "items" / item_id / "model.gltf"
            glb = ROOT / "models" / "items" / item_id / "model.glb"
            if not (parts.is_file() or gltf.is_file() or glb.is_file()):
                errors.append(f"{item_id}: model missing ({model})")
        types = set(data.get("types") or [])
        if not (types & KNOWN_TYPES):
            errors.append(f"{item_id}: types has no known itemType {types}")
        dig = types & {"mining", "cutting", "digging"}
        if dig:
            caps = (data.get("tool") or {}).get("groupcaps") or {}
            if not caps:
                errors.append(f"{item_id}: dig types without tool.groupcaps")
        if "combat" in types and "armor" not in types and "shield" not in item_id:
            dmg = (data.get("tool") or {}).get("damage") or {}
            if not dmg:
                errors.append(f"{item_id}: combat without tool.damage")
        if "armor" in types:
            armor = data.get("armor") or {}
            if not armor.get("slots"):
                errors.append(f"{item_id}: armor without slots")
            if not armor.get("armor_groups"):
                errors.append(f"{item_id}: armor without armor_groups")
    if errors:
        print("validate_item_defs FAILED:")
        for e in errors:
            print(" ", e)
        return 1
    print(f"validate_item_defs OK ({len(list(CONTENT.glob('*.json')))} items)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
