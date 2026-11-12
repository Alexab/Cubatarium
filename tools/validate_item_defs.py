#!/usr/bin/env python3
"""Validate content/items/*.json against completeness gate from the CC0 import plan."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "content" / "items"
MANIFEST = ROOT / "tools" / "item_model_manifest.json"

from item_critical_ids import CRITICAL_IDS  # noqa: E402 - shared set
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
    return False


def has_any_model(item_id: str, model: str) -> bool:
    if model_exists(model):
        return True
    parts = ROOT / "models" / "items" / f"{item_id}.json"
    gltf = ROOT / "models" / "items" / item_id / "model.gltf"
    glb = ROOT / "models" / "items" / item_id / "model.glb"
    return parts.is_file() or gltf.is_file() or glb.is_file()


def is_critical(data: dict, item_id: str) -> bool:
    if item_id in CRITICAL_IDS:
        return True
    tool = data.get("tool") or {}
    if tool.get("groupcaps"):
        return True
    armor = data.get("armor") or {}
    if armor.get("slots"):
        return True
    use = data.get("use") or {}
    action = use.get("action") or data.get("use_action") or ""
    return action in ("eat", "drink")


def main() -> int:
    errors: list[str] = []
    def_ids: set[str] = set()
    for path in sorted(CONTENT.glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        def_ids.add(data.get("id") or path.stem)
        item_id = data.get("id") or path.stem
        if item_id != path.stem:
            errors.append(f"{path.name}: id={item_id} != filename")
        model = data.get("model", "")
        if not has_any_model(item_id, model):
            if is_critical(data, item_id) and not data.get("hidden"):
                errors.append(f"{item_id}: critical item missing model ({model})")
            elif not data.get("hidden"):
                errors.append(f"{item_id}: model missing ({model})")
        types = set(data.get("types") or [])
        if not (types & KNOWN_TYPES):
            errors.append(f"{item_id}: types has no known itemType {types}")
        dig = types & {"mining", "cutting", "digging"}
        if dig:
            caps = (data.get("tool") or {}).get("groupcaps") or {}
            if not caps:
                errors.append(f"{item_id}: dig types without tool.groupcaps")
        if (
            "combat" in types
            and "armor" not in types
            and "shield" not in item_id
            and item_id not in {"arrow"}
        ):
            dmg = (data.get("tool") or {}).get("damage") or {}
            ranged = data.get("ranged") or {}
            if not dmg and not ranged.get("enabled"):
                errors.append(f"{item_id}: combat without tool.damage")
        if "armor" in types:
            armor = data.get("armor") or {}
            if not armor.get("slots"):
                errors.append(f"{item_id}: armor without slots")
            if not armor.get("armor_groups"):
                errors.append(f"{item_id}: armor without armor_groups")
        # Soft gates for upcoming schema (warn-level via errors after content lands).
        ranged = data.get("ranged") or {}
        if ranged.get("enabled"):
            rng = ranged.get("range", ranged.get("range_blocks"))
            if rng is None:
                errors.append(f"{item_id}: ranged.enabled without range/range_blocks")
        block = data.get("block") or {}
        if block.get("enabled"):
            armor = data.get("armor") or {}
            if not armor.get("armor_groups"):
                errors.append(
                    f"{item_id}: block.enabled without armor.armor_groups"
                )
        if is_critical(data, item_id) and data.get("hidden"):
            errors.append(f"{item_id}: critical item must not be hidden")
    # Manifest ids must stay a subset of live defs: re-running the CC0 import
    # pipeline must never resurrect pruned items.
    if MANIFEST.is_file():
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        for mid in manifest.get("items", {}):
            if mid not in def_ids:
                errors.append(f"manifest item without def: {mid}")
    if errors:
        print("validate_item_defs FAILED:")
        for e in errors:
            print(" ", e)
        return 1
    print(f"validate_item_defs OK ({len(list(CONTENT.glob('*.json')))} items)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
