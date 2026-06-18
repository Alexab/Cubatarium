#!/usr/bin/env python3
"""Shared helpers for texture stem mapping and resource-pack YAML generation."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

import yaml

REPO = Path(__file__).resolve().parents[1]
RESEARCH = Path(r"E:/Work/Home/CubatariumTextureResearch")

MANIFEST_FILES = [
    REPO / "tools/block_manifest.json",
    REPO / "tools/block_manifest_supplement.json",
    REPO / "tools/block_manifest_animated.json",
]


def load_manifest_blocks() -> list[dict]:
    blocks: list[dict] = []
    seen: set[str] = set()
    for mf in MANIFEST_FILES:
        data = json.loads(mf.read_text(encoding="utf-8-sig"))
        for block in data.get("blocks", []):
            name = block["name"]
            if name not in seen:
                seen.add(name)
                blocks.append(block)

    # Core blocks defined in legacy models but omitted from manifests.
    models_dir = REPO / "models" / "blocks"
    if models_dir.is_dir():
        for mf in sorted(models_dir.glob("*.json")):
            if mf.name == "selection.json":
                continue
            data = json.loads(mf.read_text(encoding="utf-8-sig"))
            name = data.get("name")
            if not name or name in seen:
                continue
            seen.add(name)
            textures = data.get("textures", [])
            entry: dict[str, Any] = {"name": name, "id": data.get("id", 0)}
            if len(textures) == 6 and len(set(textures)) == 1:
                entry["uniform"] = textures[0]
            elif len(textures) >= 6:
                entry["faces"] = textures[:6]
            elif len(textures) == 1:
                entry["uniform"] = textures[0]
            else:
                continue
            if data.get("types"):
                entry["types"] = data["types"]
            if data.get("physics"):
                entry["physics"] = data["physics"]
            if data.get("render"):
                entry["render"] = data["render"]
            blocks.append(entry)
    return blocks


def face_stems(entry: dict) -> list[str]:
    if "uniform" in entry:
        return [entry["uniform"]] * 6
    faces = entry.get("faces", [])
    return faces[:6] if len(faces) >= 6 else list(faces)


def block_spec(entry: dict) -> dict | str:
    if "uniform" in entry:
        stem = entry["uniform"]
        if entry.get("physics_preset") or entry.get("render_transparent"):
            spec: dict[str, Any] = {
                "faces": [stem] * 6,
                "types": entry.get("types", ["natural"]),
            }
            if entry.get("physics_preset"):
                spec["physics"] = {"preset": entry["physics_preset"]}
            if entry.get("render_transparent"):
                style = "fluid" if entry.get("physics_preset") in ("water", "lava") else None
                spec["render"] = {"transparent": True}
                if style:
                    spec["render"]["style"] = style
            if entry["name"] in ("water", "lava"):
                spec["animation"] = {
                    "frame_count": 16 if entry["name"] == "water" else 8,
                    "frametime": 2,
                }
            return spec
        return stem
    faces = entry.get("faces", [])
    spec: dict[str, Any] = {"faces": faces, "types": entry.get("types", ["natural"])}
    if "physics" in entry:
        spec["physics"] = entry["physics"]
    elif entry.get("physics_preset"):
        spec["physics"] = {"preset": entry["physics_preset"]}
    if "render" in entry:
        spec["render"] = entry["render"]
    elif entry.get("render_transparent"):
        spec["render"] = {"transparent": True}
    if "animation" in entry:
        spec["animation"] = entry["animation"]
    return spec


def _ensure_png(name: str) -> str:
    name = name.strip().strip('"').strip("'")
    if not name.endswith(".png"):
        name = f"{name}.png"
    return name


def parse_mtg_tile_ref(tile: str) -> str | dict[str, Any]:
    """Parse Luanti tile string into file ref or composite dict."""
    tile = tile.strip()
    if tile.startswith("(") and tile.endswith(")"):
        tile = tile[1:-1].strip()
    if "^" in tile:
        base, overlay = tile.split("^", 1)
        return {
            "composite": {
                "base": _ensure_png(base),
                "overlay": _ensure_png(overlay),
            }
        }
    return _ensure_png(tile)


def mt_tiles_to_cubatarium_faces(tiles: list[str]) -> list[str]:
    """Convert MT tile list (up,down,right,left,back,front) to Cubatarium face stems placeholder."""
    while len(tiles) < 6:
        tiles.append(tiles[-1] if tiles else "default_stone.png")
    if len(tiles) > 6:
        tiles = tiles[:6]
    # MT: 0=+Y,1=-Y,2=+X,3=-X,4=+Z,5=-Z
    # Cubatarium: +Z,+X,-Z,-X,+Y,-Y
    order = [4, 2, 5, 3, 0, 1]
    return [tiles[i] for i in order]


def load_stem_map(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    data = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    return data.get("stems", data)


# Cubatarium legacy stem -> MTG texture (str path or composite dict)
MINETEST_STEM_MAP: dict[str, str | dict | None] = {
    "dirt": "default_dirt.png",
    "grass_side": "default_grass_side.png",
    "grass_top": "default_grass.png",
    "grass_top_green": "default_grass.png",
    "stone": "default_stone.png",
    "bedrock": "default_stone.png",
    "sand": "default_sand.png",
    "sandstone": "default_sandstone.png",
    "sandstone_carved": "default_sandstone_brick.png",
    "sandstone_smooth": "default_sandstone_block.png",
    "gravel": "default_gravel.png",
    "glass": "default_glass.png",
    "clay": "default_clay.png",
    "obsidian": "default_obsidian.png",
    "ice": "default_ice.png",
    "snow": "default_snow.png",
    "snow_side": "default_snow_side.png",
    "hellsand": "default_desert_sand.png",
    "hellrock": "default_obsidian.png",
    "whiteStone": "default_silver_sandstone.png",
    "bookshelf": "default_bookshelf.png",
    "brick": "default_brick.png",
    "stoneMoss": "default_mossycobble.png",
    "stonebricksmooth": "default_stone_brick.png",
    "stonebricksmooth_carved": "default_stone_brick.png",
    "stonebricksmooth_cracked": "default_cobble.png",
    "stonebricksmooth_mossy": "default_mossycobble.png",
    "stonebrick": "default_stone_brick.png",
    "oreCoal": {"composite": {"base": "default_stone.png", "overlay": "default_mineral_coal.png"}},
    "oreIron": {"composite": {"base": "default_stone.png", "overlay": "default_mineral_iron.png"}},
    "oreGold": {"composite": {"base": "default_stone.png", "overlay": "default_mineral_gold.png"}},
    "oreDiamond": {"composite": {"base": "default_stone.png", "overlay": "default_mineral_diamond.png"}},
    "oreRedstone": {"composite": {"base": "default_stone.png", "overlay": "default_mineral_mese.png"}},
    "blockIron": "default_steel_block.png",
    "blockGold": "default_gold_block.png",
    "blockDiamond": "default_diamond_block.png",
    "blockRedstone": "default_mese_block.png",
    "blockLapis": "default_copper_block.png",
    "blockEmerald": "default_diamond_block.png",
    "oreLapis": {"composite": {"base": "default_stone.png", "overlay": "default_mineral_copper.png"}},
    "oreEmerald": {"composite": {"base": "default_stone.png", "overlay": "default_mineral_diamond.png"}},
    "tree_side": "default_tree.png",
    "tree_top": "default_tree_top.png",
    "wood": "default_wood.png",
    "tree_birch": "default_aspen_tree.png",
    "tree_spruce": "default_pine_tree.png",
    "tree_jungle": "default_jungletree.png",
    "wood_birch": "default_aspen_wood.png",
    "wood_spruce": "default_pine_wood.png",
    "wood_jungle": "default_junglewood.png",
    "leaves_opaque": "default_leaves.png",
    "leaves": "default_leaves_simple.png",
    "leaves_spruce": "default_pine_needles.png",
    "leaves_spruce_opaque": "default_pine_needles.png",
    "leaves_jungle": "default_jungleleaves.png",
    "leaves_jungle_opaque": "default_jungleleaves.png",
    "cactus_side": "default_cactus_side.png",
    "cactus_top": "default_cactus_top.png",
    "cactus_bottom": "default_cactus_top.png",
    "melon_side": "default_cactus_side.png",
    "melon_top": "default_cactus_top.png",
    "netherBrick": "default_obsidian_brick.png",
    "lightgem": "default_meselamp.png",
    "sponge": "default_moss.png",
    "furnace_front": "default_furnace_front.png",
    "furnace_front_lit": "default_furnace_front_active.png",
    "furnace_side": "default_furnace_side.png",
    "furnace_top": "default_furnace_top.png",
    "furnace_bottom": "default_furnace_bottom.png",
    "workbench_top": "default_wood.png",
    "workbench_front": "default_tree.png",
    "workbench_side": "default_wood.png",
    "mycel_top": "default_moss.png",
    "mycel_side": "default_dirt.png",
    "farmland_dry": "default_dry_dirt.png",
    "farmland_wet": "default_dirt.png",
    "sapling": "default_sapling.png",
    "sapling_birch": "default_aspen_sapling.png",
    "sapling_spruce": "default_pine_sapling.png",
    "sapling_jungle": "default_junglesapling.png",
    "tallgrass": "default_grass_1.png",
    "fern": "default_fern_1.png",
    "deadbush": "default_dry_shrub.png",
    "flower": "default_dry_grass_1.png",
    "rose": "default_dry_grass_2.png",
    "reeds": "default_papyrus.png",
    "vine": "default_papyrus.png",
    "waterlily": "default_waterlily.png" if False else "default_dry_grass_3.png",
    "rail": "default_rail.png" if False else "default_fence_rail_wood.png",
    "torch": "default_torch_on_floor.png",
    "ladder": "default_ladder_wood.png",
    "redstoneLight": "default_meselamp.png",
    "redstoneLight_lit": "default_meselamp.png",
    "redstoneDust_cross": "default_mese_crystal_fragment.png",
    "redstoneDust_line": "default_mese_crystal_fragment.png",
    "doorWood_lower": "default_wood.png",
    "doorWood_upper": "default_wood.png",
    "stoneslab_side": "default_stone.png",
    "stoneslab_top": "default_stone.png",
    "thinglass_top": "default_glass.png",
    "pumpkin_side": "default_cactus_side.png",
    "pumpkin_top": "default_cactus_top.png",
    "pumpkin_face": "default_cactus_side.png",
    "tnt_side": "default_sand.png",
    "tnt_top": "default_sand.png",
    "tnt_bottom": "default_sand.png",
    "water": "default_water_source_animated.png",
    "lava": "default_lava_source_animated.png",
    "fire_0": "default_lava.png",
    "fire_1": "default_torch_on_floor.png",
    "quartz_side": "default_silver_sandstone.png",
    "quartz_top": "default_silver_sandstone_block.png",
    "quartz_bottom": "default_silver_sandstone.png",
    "quartzblock_side": "default_silver_sandstone.png",
    "quartzblock_top": "default_silver_sandstone_block.png",
    "quartzblock_bottom": "default_silver_sandstone.png",
    "quartzblock_chiseled": "default_silver_sandstone_brick.png",
    "quartzblock_chiseled_top": "default_silver_sandstone_brick.png",
    "quartzblock_lines": "default_silver_sandstone.png",
    "quartzblock_lines_top": "default_silver_sandstone_block.png",
    "quartz_chiseled_top": "default_silver_sandstone_brick.png",
    "quartz_pillar_top": "default_silver_sandstone_block.png",
    "quartz_pillar_side": "default_silver_sandstone.png",
    "piston_side": "default_pine_wood.png",
    "piston_top": "default_pine_tree_top.png",
    "piston_bottom": "default_pine_tree_top.png",
    "piston_inner": "default_stone.png",
    "hopper": "default_chest_side.png",
    "hopper_top": "default_chest_top.png",
    "hopper_inside": "default_chest_inside.png",
    "anvil_base": "default_steel_block.png",
    "anvil_top": "default_steel_block.png",
    "cauldron_top": "default_steel_block.png",
    "cauldron_side": "default_steel_block.png",
    "cauldron_bottom": "default_steel_block.png",
    "cake_top": "default_clay.png",
    "cake_side": "default_clay.png",
    "cake_bottom": "default_clay.png",
    "enchantment_side": "default_obsidian.png",
    "enchantment_top": "default_obsidian_brick.png",
    "enchantment_bottom": "default_obsidian.png",
    "jukebox_top": "default_wood.png",
    "pumpkin_jack": "default_cactus_top.png",
    "daylightDetector_side": "default_wood.png",
    "daylightDetector_top": "default_sand.png",
    "endframe_side": "default_obsidian_brick.png",
    "endframe_top": "default_obsidian.png",
    "endframe_eye": "default_meselamp.png",
    "workbench_side": "default_wood.png",
    "workbench_front": "default_tree.png",
    "dispenser_front": "default_furnace_front.png",
    "dispenser_side": "default_furnace_side.png",
    "dropper_front": "default_furnace_front.png",
    "dropper_side": "default_furnace_side.png",
    "endframe_top": "default_obsidian.png",
    "endframe_side": "default_obsidian_brick.png",
    "endframe_eye": "default_meselamp.png",
    "daylight_sensor_top": "default_sand.png",
    "daylight_sensor_side": "default_wood.png",
    "stem_straight": "default_papyrus.png",
    "stem_bent": "default_papyrus.png",
    "redtorch": "default_mese_crystal.png",
    "redtorch_lit": "default_meselamp.png",
    "lever": "default_stick.png",
    "trapdoor": "default_wood.png",
    "fenceIron": "default_ladder_steel.png",
    "mushroom_brown": "default_dry_grass_4.png",
    "mushroom_red": "default_dry_grass_5.png",
    "mushroom_skin_brown": "default_dirt.png",
    "mushroom_skin_red": "default_dirt.png",
    "mushroom_skin_stem": "default_wood.png",
    "brewingStand": "default_cobble.png",
    "brewingStand_base": "default_stone.png",
    "jukebox_top": "default_wood.png",
    "itemframe_back": "default_wood.png",
    "wheat": "default_wheat.png" if False else "default_grass_2.png",
    "crops_7": "default_grass_3.png",
    "carrots_3": "default_grass_4.png",
    "potatoes_3": "default_grass_5.png",
    "netherStalk_2": "default_papyrus.png",
    "cocoa_2": "default_jungleleaves.png",
}

# Fix entries that used False hack - check files exist at build time
MINETEST_STEM_MAP["waterlily"] = "default_dry_grass_3.png"
MINETEST_STEM_MAP["rail"] = "default_fence_rail_wood.png"
MINETEST_STEM_MAP["wheat"] = "default_grass_2.png"

# Wool: MTG default has no wool — map to clay palette substitutes for visibility
_WOOL_MTG = [
    "default_sand.png",
    "default_stone.png",
    "default_cobble.png",
    "default_desert_sand.png",
    "default_gravel.png",
    "default_dirt.png",
    "default_clay.png",
    "default_silver_sand.png",
    "default_stone_brick.png",
    "default_sandstone.png",
    "default_obsidian.png",
    "default_brick.png",
    "default_mossycobble.png",
    "default_silver_sandstone.png",
    "default_desert_sandstone.png",
    "default_permafrost.png",
]
for i in range(16):
    MINETEST_STEM_MAP[f"cloth_{i}"] = _WOOL_MTG[i]

# Unmappable MC-only stems (explicit null)
for _stem in (
    "web",
    "mobSpawner",
    "commandBlock",
    "beacon",
    "dragonEgg",
    "musicBlock",
    "netherquartz",
    "enchanting_table_top",
    "enchanting_table_side",
    "enchanting_table_bottom",
    "repeater",
    "repeater_lit",
    "comparator",
    "comparator_lit",
    "tripWire",
    "tripWireSource",
    "doorIron_lower",
    "doorIron_upper",
    "goldenRail",
    "goldenRail_powered",
    "detectorRail",
    "detectorRail_on",
    "activatorRail",
    "activatorRail_powered",
    "rail_turn",
    "piston_top_sticky",
):
    MINETEST_STEM_MAP.setdefault(_stem, None)


def write_minetest_stem_map_yaml(path: Path) -> None:
    stems = {k: v for k, v in sorted(MINETEST_STEM_MAP.items()) if v is not None}
    doc = {
        "description": "Cubatarium texture stem -> minetest-game/default PNG (or composite)",
        "stems": stems,
    }
    path.write_text(
        yaml.dump(doc, allow_unicode=True, sort_keys=False, default_flow_style=False),
        encoding="utf-8",
    )


def resolve_stem_ref(
    stem: str,
    index: dict[str, list[Path]],
    stem_map: dict[str, Any],
    tex_root: Path,
) -> Any | None:
    """Resolve stem to texture ref (relative path str or composite dict)."""
    if stem in stem_map:
        mapped = stem_map[stem]
        if mapped is None:
            return None
        if isinstance(mapped, dict):
            return mapped
        if isinstance(mapped, str):
            return mapped

    from analyze_texture_packs import resolve_stem  # noqa: WPS433

    hit = resolve_stem(stem, index)
    if hit and index.get(hit):
        rel = index[hit][0].relative_to(tex_root).as_posix()
        return rel
    return None


def texture_ref_exists(tex_root: Path, ref: Any) -> bool:
    if ref is None:
        return False
    if isinstance(ref, str):
        return (tex_root / ref).is_file() or (tex_root / Path(ref).name).is_file()
    if isinstance(ref, dict) and "composite" in ref:
        c = ref["composite"]
        return texture_ref_exists(tex_root, c["base"]) and texture_ref_exists(tex_root, c["overlay"])
    return False
