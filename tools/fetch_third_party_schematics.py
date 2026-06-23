#!/usr/bin/env python3
"""Download open-source Minetest schematic sources into third_party/schematics/."""

from __future__ import annotations

import json
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "third_party" / "schematics"

MTG_BASE = "https://raw.githubusercontent.com/minetest-game/default/master/schematics"
MTG_FILES = [
    "acacia_bush.mts",
    "acacia_log.mts",
    "acacia_tree.mts",
    "acacia_tree_from_sapling.mts",
    "apple_log.mts",
    "apple_tree.mts",
    "apple_tree_from_sapling.mts",
    "aspen_log.mts",
    "aspen_tree.mts",
    "aspen_tree_from_sapling.mts",
    "blueberry_bush.mts",
    "bush.mts",
    "emergent_jungle_tree.mts",
    "emergent_jungle_tree_from_sapling.mts",
    "jungle_log.mts",
    "jungle_tree.mts",
    "jungle_tree_from_sapling.mts",
    "large_cactus.mts",
    "papyrus_on_dirt.mts",
    "papyrus_on_dry_dirt.mts",
    "pine_bush.mts",
    "pine_log.mts",
    "pine_tree.mts",
    "pine_tree_from_sapling.mts",
    "small_pine_tree.mts",
    "small_pine_tree_from_sapling.mts",
    "snowy_pine_tree_from_sapling.mts",
    "snowy_small_pine_tree_from_sapling.mts",
]

RUINED_BASE = "https://raw.githubusercontent.com/X-DE1/ruined_structures/main/schematics"
RUINED_FILES = [
    "desert.mts",
    "egypt.mts",
    "egypt2.mts",
    "ruined_house.mts",
    "ruined_house2.mts",
    "sandstone.mts",
    "stonehenge.mts",
    "temple.mts",
]


def fetch(url: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"GET {url}")
    with urllib.request.urlopen(url, timeout=60) as resp:
        dest.write_bytes(resp.read())


def main() -> int:
    sources: dict[str, dict] = {}
    for name in MTG_FILES:
        rel = f"mtg/{name}"
        fetch(f"{MTG_BASE}/{name}", OUT / rel)
        sources[rel] = {
            "license": "CC-BY-SA-3.0",
            "upstream": "minetest-game/default",
        }
    for name in RUINED_FILES:
        rel = f"ruined_structures/{name}"
        fetch(f"{RUINED_BASE}/{name}", OUT / rel)
        sources[rel] = {
            "license": "CC-BY-SA-4.0",
            "upstream": "X-DE1/ruined_structures",
        }

    meta = {
        "fetched_at": datetime.now(timezone.utc).isoformat(),
        "files": sources,
    }
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "SOURCES.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    print(f"Fetched {len(sources)} schematics -> {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
