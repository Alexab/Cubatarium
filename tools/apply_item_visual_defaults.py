#!/usr/bin/env python3
"""Fill content/items/*.json visual.wield_scale from category defaults (no overwrite)."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTENT = ROOT / "content" / "items"

OVERRIDES = {
    "wood_bow": 1.4,
    "wood_shield": 1.5,
    "iron_shield": 1.5,
    "wood_torch": 1.1,
    "iron_lantern": 1.15,
    "wood_spear": 1.8,
    "stone_spear": 1.8,
    "iron_spear": 1.85,
    "apple": 0.95,
    "bread": 0.95,
}


def default_scale(data: dict, item_id: str) -> float:
    if item_id in OVERRIDES:
        return OVERRIDES[item_id]
    types = set(data.get("types") or [])
    ranged = data.get("ranged") or {}
    block = data.get("block") or {}
    if ranged.get("enabled") or "bow" in item_id:
        return 1.4
    if block.get("enabled") or "shield" in item_id:
        return 1.5
    if "spear" in item_id:
        return 1.8
    if types & {"mining", "cutting", "digging"}:
        return 1.5
    if any(k in item_id for k in ("sword", "dagger", "knife")):
        return 1.35
    use = data.get("use") or {}
    action = use.get("action") or data.get("use_action") or ""
    if action in ("eat", "drink"):
        return 0.95
    if "armor" in types:
        return 1.0
    return 1.25


def main() -> int:
    updated = 0
    for path in sorted(CONTENT.glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        item_id = data.get("id") or path.stem
        visual = data.get("visual")
        if not isinstance(visual, dict):
            visual = {}
        if "wield_scale" in visual:
            continue
        visual["wield_scale"] = default_scale(data, item_id)
        # category swing hints for tools/weapons/food
        swing = visual.get("swing")
        if not isinstance(swing, dict):
            swing = {}
        types = set(data.get("types") or [])
        if not swing.get("dig") and types & {"mining", "cutting", "digging"}:
            swing["dig"] = "dig_tool"
            swing.setdefault("melee", "dig_tool")
        if not swing.get("melee"):
            if "spear" in item_id:
                swing["melee"] = "thrust_spear"
            elif any(k in item_id for k in ("sword", "dagger", "knife")):
                swing["melee"] = "slash_weapon"
        if swing:
            visual["swing"] = swing
        use_map = visual.get("use")
        if not isinstance(use_map, dict):
            use_map = {}
        action = (data.get("use") or {}).get("action") or data.get("use_action")
        if action == "eat" and not use_map.get("eat"):
            use_map["eat"] = "eat_hand"
        if action == "drink" and not use_map.get("drink"):
            use_map["drink"] = "eat_hand"
        if "bow" in item_id and not use_map.get("ranged"):
            use_map["ranged"] = "draw_bow"
        if "shield" in item_id and not use_map.get("block"):
            use_map["block"] = "raise_shield"
        if use_map:
            visual["use"] = use_map
        data["visual"] = visual
        path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        updated += 1
    print(f"apply_item_visual_defaults: updated {updated} items")
    return 0


if __name__ == "__main__":
    sys.exit(main())
