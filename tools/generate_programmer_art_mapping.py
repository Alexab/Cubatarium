#!/usr/bin/env python3
"""Generate tools/programmer_art_mapping.yaml from ProgrammerArt blocks + manifests."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

import yaml

REPO = Path(__file__).resolve().parents[1]
RESEARCH = Path(r"E:/Work/Home/CubatariumTextureResearch")
PA_TEX = RESEARCH / "programmer_art" / "textures" / "blocks"

sys.path.insert(0, str(REPO / "tools"))
from analyze_texture_packs import index_pack  # noqa: E402
from stem_mapping_common import block_spec, face_stems, load_manifest_blocks  # noqa: E402

# ProgrammerArt uses Minecraft 1.7+ block texture names.
PROGRAMMER_ART_ALIASES: dict[str, list[str]] = {
    "grass_side": ["grass_side", "grass_block_side"],
    "grass_top": ["grass_top", "grass_block_top"],
    "grass_top_green": ["grass_top", "grass_block_top"],
    "wood": ["wood", "planks_oak", "oak_planks"],
    "tree_side": ["tree_side", "log_oak", "oak_log"],
    "tree_top": ["tree_top", "log_oak_top", "oak_log_top"],
    "tree_birch": ["tree_birch", "log_birch"],
    "tree_spruce": ["tree_spruce", "log_spruce"],
    "tree_jungle": ["tree_jungle", "log_jungle"],
    "wood_birch": ["wood_birch", "planks_birch", "birch_planks"],
    "wood_spruce": ["wood_spruce", "planks_spruce", "spruce_planks"],
    "wood_jungle": ["wood_jungle", "planks_jungle", "jungle_planks"],
    "leaves_opaque": ["leaves_opaque", "leaves_oak", "leaves"],
    "leaves": ["leaves", "leaves_oak"],
    "leaves_spruce_opaque": ["leaves_spruce", "leaves_spruce_opaque"],
    "leaves_jungle_opaque": ["leaves_jungle", "leaves_jungle_opaque"],
    "oreCoal": ["oreCoal", "coal_ore"],
    "oreIron": ["oreIron", "iron_ore"],
    "oreGold": ["oreGold", "gold_ore"],
    "oreDiamond": ["oreDiamond", "diamond_ore"],
    "oreEmerald": ["oreEmerald", "emerald_ore"],
    "oreLapis": ["oreLapis", "lapis_ore"],
    "oreRedstone": ["oreRedstone", "redstone_ore"],
    "blockIron": ["blockIron", "iron_block"],
    "blockGold": ["blockGold", "gold_block"],
    "blockDiamond": ["blockDiamond", "diamond_block"],
    "blockEmerald": ["blockEmerald", "emerald_block"],
    "blockLapis": ["blockLapis", "lapis_block"],
    "blockRedstone": ["blockRedstone", "redstone_block"],
    "hellrock": ["hellrock", "netherrack"],
    "hellsand": ["hellsand", "soul_sand"],
    "whiteStone": ["whiteStone", "end_stone"],
    "netherBrick": ["netherBrick", "nether_brick"],
    "lightgem": ["lightgem", "glowstone"],
    "stonebricksmooth": ["stonebricksmooth", "stone_bricks"],
    "stonebricksmooth_cracked": ["stonebricksmooth_cracked", "cracked_stone_bricks"],
    "stonebricksmooth_mossy": ["stonebricksmooth_mossy", "mossy_stone_bricks"],
    "stonebricksmooth_carved": ["stonebricksmooth_carved", "chiseled_stone_bricks"],
    "sandstone_carved": ["sandstone_carved", "chiseled_sandstone"],
    "sandstone_smooth": ["sandstone_smooth", "sandstone_top", "smooth_sandstone"],
    "snow_side": ["snow_side", "snow_layer_side"],
    "doorWood_lower": ["doorWood_lower", "door_wood_lower"],
    "doorWood_upper": ["doorWood_upper", "door_wood_upper"],
    "redstoneLight": ["redstoneLight", "redstone_lamp"],
    "redstoneLight_lit": ["redstoneLight_lit", "redstone_lamp_on"],
    "redstoneDust_cross": ["redstoneDust_cross", "redstone_dust_dot"],
    "redstoneDust_line": ["redstoneDust_line", "redstone_dust_line"],
    "stoneslab_side": ["stoneslab_side", "stone_slab_side"],
    "stoneslab_top": ["stoneslab_top", "stone_slab_top"],
    "thinglass_top": ["thinglass_top", "glass_pane_top"],
    "netherquartz": ["netherquartz", "quartz_ore"],
    "tallgrass": ["tallgrass", "tall_grass"],
    "waterlily": ["waterlily", "waterlily"],
    "mobSpawner": ["mobSpawner", "mob_spawner"],
    "commandBlock": ["commandBlock", "command_block"],
    "musicBlock": ["musicBlock", "noteblock"],
    "redtorch": ["redtorch", "redstone_torch_off"],
    "redtorch_lit": ["redtorch_lit", "redstone_torch_on"],
    "fenceIron": ["fenceIron", "iron_bars"],
    "tripWire": ["tripWire", "trip_wire"],
    "tripWireSource": ["tripWireSource", "trip_wire_hook"],
    "goldenRail": ["goldenRail", "golden_rail"],
    "goldenRail_powered": ["goldenRail_powered", "golden_rail_powered"],
    "detectorRail": ["detectorRail", "detector_rail"],
    "detectorRail_on": ["detectorRail_on", "detector_rail_on"],
    "activatorRail": ["activatorRail", "activator_rail"],
    "activatorRail_powered": ["activatorRail_powered", "activator_rail_on"],
    "rail_turn": ["rail_turn", "rail_curved"],
    "cloth_0": ["cloth_0", "wool_colored_white"],
    "cloth_1": ["cloth_1", "wool_colored_orange"],
    "cloth_2": ["cloth_2", "wool_colored_magenta"],
    "cloth_3": ["cloth_3", "wool_colored_light_blue"],
    "cloth_4": ["cloth_4", "wool_colored_yellow"],
    "cloth_5": ["cloth_5", "wool_colored_lime"],
    "cloth_6": ["cloth_6", "wool_colored_pink"],
    "cloth_7": ["cloth_7", "wool_colored_gray"],
    "cloth_8": ["cloth_8", "wool_colored_silver"],
    "cloth_9": ["cloth_9", "wool_colored_cyan"],
    "cloth_10": ["cloth_10", "wool_colored_purple"],
    "cloth_11": ["cloth_11", "wool_colored_blue"],
    "cloth_12": ["cloth_12", "wool_colored_brown"],
    "cloth_13": ["cloth_13", "wool_colored_green"],
    "cloth_14": ["cloth_14", "wool_colored_red"],
    "cloth_15": ["cloth_15", "wool_colored_black"],
}


def candidate_names(stem: str) -> list[str]:
    names = [stem.lower()]
    if stem in PROGRAMMER_ART_ALIASES:
        names.extend(s.lower() for s in PROGRAMMER_ART_ALIASES[stem])
    seen: set[str] = set()
    out: list[str] = []
    for n in names:
        if n not in seen:
            seen.add(n)
            out.append(n)
    return out


def resolve_stem(stem: str, index: dict[str, list[Path]]) -> str | None:
    for name in candidate_names(stem):
        if name in index:
            return name
    for key in index:
        if stem.lower() == key or stem.lower() in key:
            return key
    return None


def main() -> int:
    if not PA_TEX.is_dir():
        raise SystemExit(f"Missing {PA_TEX} — run download_texture_packs.py --pack programmer_art")

    index = index_pack(PA_TEX)
    blocks_out: dict[str, Any] = {}
    textures_out: dict[str, str] = {}
    skipped: list[str] = []

    for entry in load_manifest_blocks():
        name = entry["name"]
        stems = face_stems(entry)
        resolved: dict[str, str | None] = {}
        for stem in set(stems):
            hit = resolve_stem(stem, index)
            if hit and index[hit]:
                resolved[stem] = f"{hit}.png"
            else:
                resolved[stem] = None
        if any(v is None for v in resolved.values()):
            skipped.append(name)
            continue
        blocks_out[name] = block_spec(entry)
        for stem, rel in resolved.items():
            if stem not in textures_out and rel:
                textures_out[stem] = rel

    out = {
        "license": "CC-BY-4.0",
        "license_text": (
            "ProgrammerArt textures (CC BY 4.0).\n"
            "Copyright deathcap / ProgrammerArt contributors.\n"
            "Source: https://github.com/deathcap/ProgrammerArt\n"
        ),
        "blocks": blocks_out,
        "textures": textures_out,
    }
    path = REPO / "tools" / "programmer_art_mapping.yaml"
    path.write_text(
        yaml.dump(out, allow_unicode=True, sort_keys=False, default_flow_style=False),
        encoding="utf-8",
    )
    print(f"Wrote {path}: {len(blocks_out)} blocks, {len(textures_out)} textures, skipped {len(skipped)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
