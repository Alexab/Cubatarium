#!/usr/bin/env python3
"""Inject display_name fields into tools/canonical_blocks.yaml from a built-in map."""

from __future__ import annotations

from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[1]
CANONICAL = REPO / "tools" / "canonical_blocks.yaml"

DISPLAY_NAMES: dict[str, str] = {
    # tier_a
    "bedrock": "Bedrock",
    "stone": "Stone",
    "dirt": "Dirt",
    "grass": "Grass Block",
    "sand": "Sand",
    "sandstone": "Sandstone",
    "gravel": "Gravel",
    "snow": "Snow",
    "clay": "Clay",
    "ice": "Ice",
    "hellrock": "Hellrock",
    "water": "Water",
    "lava": "Lava",
    "fire": "Fire",
    "wood": "Wood",
    "tree_log": "Tree Log",
    "tree_leaves": "Leaves",
    # tier_b
    "glass": "Glass",
    "obsidian": "Obsidian",
    "hellsand": "Soul Sand",
    "whiteStone": "Quartz Block",
    "web": "Cobweb",
    "bookshelf": "Bookshelf",
    "oreCoal": "Coal Ore",
    "oreIron": "Iron Ore",
    "oreGold": "Gold Ore",
    "oreDiamond": "Diamond Ore",
    "oreEmerald": "Emerald Ore",
    "oreLapis": "Lapis Ore",
    "oreRedstone": "Redstone Ore",
    "blockIron": "Iron Block",
    "blockGold": "Gold Block",
    "blockDiamond": "Diamond Block",
    "blockEmerald": "Emerald Block",
    "blockLapis": "Lapis Block",
    "blockRedstone": "Redstone Block",
    "stoneMoss": "Mossy Stone",
    "stonebricksmooth": "Stone Bricks",
    "stonebricksmooth_carved": "Chiseled Stone Bricks",
    "stonebricksmooth_cracked": "Cracked Stone Bricks",
    "stonebricksmooth_mossy": "Mossy Stone Bricks",
    "cactus": "Cactus",
    "melon": "Melon",
    "wool_0": "White Wool",
    "wool_1": "Orange Wool",
    "wool_2": "Magenta Wool",
    "wool_3": "Light Blue Wool",
    "wool_4": "Yellow Wool",
    "wool_5": "Lime Wool",
    "wool_6": "Pink Wool",
    "wool_7": "Gray Wool",
    "wool_8": "Light Gray Wool",
    "wool_9": "Cyan Wool",
    "wool_10": "Purple Wool",
    "wool_11": "Blue Wool",
    "wool_12": "Brown Wool",
    "wool_13": "Green Wool",
    "wool_14": "Red Wool",
    "wool_15": "Black Wool",
}


def main() -> int:
    data = yaml.safe_load(CANONICAL.read_text(encoding="utf-8")) or {}
    for tier in ("tier_a", "tier_b"):
        tier_data = data.get(tier, {})
        if not isinstance(tier_data, dict):
            continue
        for name, spec in tier_data.items():
            if not isinstance(spec, dict):
                continue
            if name in DISPLAY_NAMES:
                spec["display_name"] = DISPLAY_NAMES[name]
    CANONICAL.write_text(
        yaml.dump(data, default_flow_style=False, allow_unicode=True, sort_keys=False),
        encoding="utf-8",
    )
    print(f"Updated display_name in {CANONICAL}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
