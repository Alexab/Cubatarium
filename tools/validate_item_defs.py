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

# Keep in sync with tools/audit_item_visuals.py CRITICAL_IDS.
CRITICAL_IDS = {
    "hand",
    "wood_pickaxe",
    "wood_axe",
    "wood_shovel",
    "stone_pickaxe",
    "stone_axe",
    "stone_shovel",
    "iron_pickaxe",
    "iron_axe",
    "iron_shovel",
    "copper_pickaxe",
    "copper_axe",
    "copper_shovel",
    "tool_pickaxe_upgraded",
    "tool_axe_upgraded",
    "tool_shovel_upgraded",
    "wood_hammer",
    "stone_hammer",
    "iron_hammer",
    "wood_mallet",
    "iron_hoe",
    "tool_hammer_upgraded",
    "tool_hoe_upgraded",
    "wood_sword",
    "stone_sword",
    "iron_sword",
    "copper_sword",
    "gold_sword",
    "wood_spear",
    "stone_spear",
    "iron_spear",
    "iron_dagger",
    "stone_knife",
    "wood_bow",
    "wood_shield",
    "iron_shield",
    "leather_head",
    "leather_chest",
    "leather_arms",
    "leather_hands",
    "leather_legs",
    "leather_feet",
    "copper_head",
    "copper_chest",
    "copper_arms",
    "copper_hands",
    "copper_legs",
    "copper_feet",
    "iron_head",
    "iron_chest",
    "iron_arms",
    "iron_hands",
    "iron_legs",
    "iron_feet",
    "apple",
    "bread",
    "wood_torch",
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
    for path in sorted(CONTENT.glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
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
        if "combat" in types and "armor" not in types and "shield" not in item_id:
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
    if errors:
        print("validate_item_defs FAILED:")
        for e in errors:
            print(" ", e)
        return 1
    print(f"validate_item_defs OK ({len(list(CONTENT.glob('*.json')))} items)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
