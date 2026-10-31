#!/usr/bin/env python3
"""Assign natural spawn zones (biomes / active time / weights) to species.

Each natural-spawnable species gets `catalog.spawn_biomes` (WorldGen biome
registry ids; missing key or empty list = any biome), an optional
`catalog.active_time` ("day"|"night"|"any"; hostiles default to night in code)
and a rarity-tuned `catalog.spawn_weight`. Run with --apply to write the
catalog files, without flags to print the plan.

First-pass data authored during the spawn-zones audit (2026-10-31); tune the
tables below freely and re-run.
"""

import argparse
import json
import os
import sys

ROOT = os.path.join(os.path.dirname(__file__), "..", "models", "creatures")

ALL_LAND = ["plains", "forest", "savanna", "scrubland", "hills", "foothills"]

BIOMES = {
    # Terrestrial passive
    "cow": ALL_LAND[:5],
    "sheep": ALL_LAND[:5],
    "pig": ALL_LAND[:5],
    "chicken": ALL_LAND[:5],
    "bunny": ["plains", "forest", "savanna", "foothills", "hills"],
    "rat": ALL_LAND + ["cold_steppe"],
    "kitten": ["plains", "forest", "savanna"],
    "warthog": ["savanna", "scrubland", "plains"],
    "tortoise": ["beach", "plains", "savanna", "forest"],
    "hedgehog": ["forest", "plains", "foothills"],
    "badger": ["forest", "hills", "foothills", "cold_steppe"],
    "fox": ["forest", "tundra", "cold_steppe", "foothills", "plains"],
    "panda": ["forest"],
    "golem": ["mountains", "hills", "jagged_peaks", "plains"],
    # Terrestrial hostile
    "wolf": ["forest", "tundra", "cold_steppe", "snowy_slopes", "foothills"],
    "zombie": ALL_LAND,
    "skeleton": ALL_LAND,
    "dirt_monster": ["plains", "forest", "hills", "foothills", "lush_caves"],
    "sand_monster": ["desert", "savanna", "scrubland", "beach"],
    "stone_monster": ["mountains", "hills", "jagged_peaks", "foothills",
                      "dripstone_caves"],
    "mese_monster": ["plains", "savanna", "scrubland", "hills"],
    "ogre": ["savanna", "scrubland", "plains", "forest"],
    "orc": ["plains", "savanna", "hills", "foothills", "forest"],
    "oerkki": ["deep_dark", "dripstone_caves", "lush_caves"],
    "dungeon_master": ["deep_dark", "dripstone_caves"],
    "land_guard": ["plains", "savanna", "foothills", "hills"],
    "tree_monster": ["forest", "lush_caves"],
    "treeman": ["forest", "hills"],
    "spider": ["forest", "plains", "savanna", "hills"],
    # Aerial
    "bee": ["plains", "forest", "savanna", "foothills"],
    "butterfly": ["plains", "forest", "savanna", "foothills"],
    "owl": ["forest", "tundra", "cold_steppe", "foothills"],
    "puffin": ["beach", "tundra", "snowy_slopes", "cold_steppe", "ocean"],
    "wasp": ["forest", "plains", "savanna"],
    "fire_spirit": ["desert", "savanna", "mountains"],
    # Aquatic
    "dolphin": ["ocean"],
    "shark": ["ocean"],
    "whale": ["ocean"],
    "squid": ["ocean"],
    "octopus": ["ocean"],
    "seahorse": ["ocean", "beach"],
    "stingray": ["ocean", "beach"],
    "trout": ["ocean", "beach"],
    "manatee": ["ocean", "beach"],
    "water_dragon": ["ocean"],
    "crab": ["beach", "ocean"],
    "lobster": ["ocean", "beach"],
    "hermitcrab": ["beach", "ocean"],
    # Amphibious (cold shores)
    "penguin": ["tundra", "snowy_slopes", "cold_steppe", "beach", "ocean"],
    "seal": ["tundra", "snowy_slopes", "cold_steppe", "beach", "ocean"],
    # Lava
    "lava_flan": ["deep_dark", "dripstone_caves", "lush_caves", "mountains"],
}

ACTIVE_TIME = {
    "bee": "day",
    "butterfly": "day",
    "owl": "night",
}

WEIGHTS = {
    1.5: ["bee", "butterfly", "trout", "squid"],
    1.2: ["cow", "sheep", "pig", "chicken", "bunny", "rat"],
    1.0: ["fox", "badger", "hedgehog", "kitten", "panda", "owl", "puffin",
          "crab", "lobster", "hermitcrab", "octopus", "stingray", "seahorse",
          "warthog", "tortoise", "manatee", "dolphin", "penguin", "seal"],
    0.7: ["wolf", "spider", "zombie", "skeleton", "sand_monster",
          "dirt_monster", "stone_monster", "oerkki", "orc", "ogre", "wasp",
          "mese_monster", "tree_monster", "land_guard"],
    0.4: ["shark", "golem", "treeman", "water_dragon", "dungeon_master",
          "fire_spirit", "lava_flan", "whale"],
}

VALID_BIOMES = {
    "plains", "forest", "desert", "hills", "tundra", "savanna", "foothills",
    "scrubland", "cold_steppe", "ocean", "beach", "mountains", "snowy_slopes",
    "jagged_peaks", "lush_caves", "dripstone_caves", "deep_dark",
}


def weight_for(species):
    for weight, group in WEIGHTS.items():
        if species in group:
            return weight
    return 1.0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true",
                        help="write the catalog files (default: dry run)")
    args = parser.parse_args()

    covered = set(BIOMES) | set(ACTIVE_TIME)
    for group in WEIGHTS.values():
        covered.update(group)
    assigned = []
    for species in sorted(os.listdir(ROOT)):
        path = os.path.join(ROOT, species, "creature.json")
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
        if not data.get("catalog", {}).get("natural_spawnable", False):
            continue
        if species not in covered:
            print(f"MISSING ZONES: {species}", file=sys.stderr)
            sys.exit(1)
        bad = [b for b in BIOMES[species] if b not in VALID_BIOMES]
        if bad:
            print(f"UNKNOWN BIOME {bad} for {species}", file=sys.stderr)
            sys.exit(1)
        catalog = data["catalog"]
        catalog["spawn_biomes"] = BIOMES[species]
        catalog["spawn_weight"] = weight_for(species)
        if species in ACTIVE_TIME:
            catalog["active_time"] = ACTIVE_TIME[species]
        else:
            catalog.pop("active_time", None)
        assigned.append(species)
        if args.apply:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                json.dump(data, f, indent=2, ensure_ascii=False)
                f.write("\n")
    mode = "wrote" if args.apply else "planned (dry run)"
    print(f"{mode} {len(assigned)} species: {', '.join(assigned)}")


if __name__ == "__main__":
    main()
