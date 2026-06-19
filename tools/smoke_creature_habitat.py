#!/usr/bin/env python3
"""Smoke checks for creature habitat JSON fields and catalog coverage."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODELS = ROOT / "models" / "creatures"

SHIP_SET = [
    "sheep",
    "wolf",
    "pig",
    "cow",
    "chicken",
    "oerkki",
    "skeleton",
    "sand_monster",
]

WAVE_ANIMAL = ["bunny", "rat", "panda", "kitten", "penguin", "bee", "warthog"]
WAVE_MONSTER = [
    "spider",
    "stone_monster",
    "tree_monster",
    "mese_monster",
    "dirt_monster",
    "dungeon_master",
    "fire_spirit",
    "land_guard",
    "lava_flan",
]
WAVE_DMOBS = [
    "fox",
    "badger",
    "hedgehog",
    "tortoise",
    "orc",
    "ogre",
    "golem",
    "treeman",
    "butterfly",
    "owl",
    "wasp",
]
WAVE_MARINE = [
    "trout",
    "shark",
    "squid",
    "stingray",
    "seahorse",
    "manatee",
    "lobster",
    "hermitcrab",
    "seal",
    "dolphin",
    "whale",
    "water_dragon",
    "crab",
    "octopus",
    "puffin",
]

REPRESENTATIVE = {
    "terrestrial": "sheep",
    "aquatic": "trout",
    "aerial": "bee",
    "amphibious": "seal",
    "lava": "lava_flan",
}


def load_creature(species: str) -> dict:
    path = MODELS / species / "creature.json"
    if not path.is_file():
        raise SystemExit(f"FAIL missing creature.json for {species}")
    return json.loads(path.read_text(encoding="utf-8"))


def check_habitat_field(species: str) -> None:
    creature = load_creature(species)
    habitat = creature.get("habitat")
    if habitat not in ("terrestrial", "aquatic", "aerial", "amphibious", "lava"):
        raise SystemExit(f"FAIL {species}: invalid habitat={habitat!r}")
    loc = creature.get("locomotion", {})
    can_fly = bool(loc.get("can_fly"))
    if habitat in ("aquatic", "aerial", "amphibious", "lava") and not can_fly:
        raise SystemExit(f"FAIL {species}: {habitat} mob must have can_fly")
    if habitat == "terrestrial" and species != "human" and species != "chicken":
        if creature.get("locomotion_archetype") == "aquatic":
            raise SystemExit(f"FAIL {species}: terrestrial with aquatic archetype")
    print(f"OK habitat {species}: {habitat}")


def check_wave(name: str, species_list: list[str]) -> None:
    for sid in species_list:
        check_habitat_field(sid)
    print(f"OK wave {name}: {len(species_list)} species")


def main() -> None:
    for sid in SHIP_SET:
        check_habitat_field(sid)
    check_wave("mobs_animal", WAVE_ANIMAL)
    check_wave("monster", WAVE_MONSTER)
    check_wave("dmobs", WAVE_DMOBS)
    check_wave("marine", WAVE_MARINE)

    for habitat, sid in REPRESENTATIVE.items():
        creature = load_creature(sid)
        if creature.get("habitat") != habitat:
            raise SystemExit(
                f"FAIL representative {sid}: expected habitat {habitat}"
            )
    print("OK representative habitat species")

    total = len(
        [p for p in MODELS.iterdir() if p.is_dir() and (p / "creature.json").is_file()]
    )
    if total < 50:
        raise SystemExit(f"FAIL catalog size {total} < 50")
    print(f"OK catalog size: {total} species")
    print("smoke_creature_habitat: all checks passed")


if __name__ == "__main__":
    main()
