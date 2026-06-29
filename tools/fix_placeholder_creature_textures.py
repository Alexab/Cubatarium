#!/usr/bin/env python3
"""Point placeholder species at available Luanti proxy textures and refresh LICENSE."""

from __future__ import annotations

import json
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
SOURCES = TOOLS / "creature_luanti_sources.yaml"

PROXY_TEXTURE: dict[str, str] = {
    "kitten": "mobs_animal/textures/mobs_bunny_grey.png",
    "warthog": "animalworld/textures/awildboar.png",
    "mese_monster": "mobs_monster/textures/mobs_stone_monster.png",
    "lava_flan": "mobs_monster/textures/mobs_dirt_monster.png",
    "dolphin": "animalworld/textures/texturetrout.png",
    "whale": "animalworld/textures/textureshark.png",
    "water_dragon": "animalworld/textures/textureshark.png",
    "octopus": "animalworld/textures/texturesquid.png",
}

LICENSE_TEXT = """Luanti mob texture (proxy import for Cubatarium).
Source mod textures per tools/creature_luanti_sources.yaml.
License: MIT / CC BY-SA as stated in upstream TenPlus1 / Skandarella mods.
See docs/CREDITS.md.
"""


def main() -> int:
    data = yaml.safe_load(SOURCES.read_text(encoding="utf-8"))
    species = data.setdefault("species", {})
    for sid, tex in PROXY_TEXTURE.items():
        if sid not in species:
            continue
        entry = species[sid]
        entry["texture"] = tex
        entry["proxy_note"] = f"placeholder proxy -> {tex}"
        lic = ROOT / "models" / "creatures" / sid / "LICENSE.txt"
        lic.write_text(LICENSE_TEXT, encoding="utf-8")
        print(f"proxy {sid} -> {tex}")
    SOURCES.write_text(yaml.safe_dump(data, sort_keys=False, allow_unicode=True), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
