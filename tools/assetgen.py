#!/usr/bin/env python3
"""Build and validate Cubatarium's original voxel asset library.

The default backend is deterministic and offline. Optional Diffusers and
TripoSR integrations are imported only when explicitly requested.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path
from typing import Any

try:
    import yaml
except ImportError as exc:  # pragma: no cover - covered by CLI error path
    raise SystemExit("assetgen needs PyYAML; install requirements-assetgen.txt") from exc

try:
    from PIL import Image, ImageDraw, ImageFont, ImageOps
except ImportError as exc:  # pragma: no cover - covered by CLI error path
    raise SystemExit("assetgen needs Pillow; install requirements-assetgen.txt") from exc


REPO = Path(__file__).resolve().parents[1]
PACK_ID = "cubatarium_worldgen_cc0"
PACK_DIR = REPO / "resource_packs" / PACK_ID
CATALOG_PATH = REPO / "tools" / "asset_catalog.yaml"
DEFAULT_PACKS = ("minetest_default_16", "cubatarium_geology_cc0", PACK_ID)
MINECRAFT_REFERENCE = {
    "version": "26.3",
    "block_definitions": 1286,
    "texture_files": 1405,
    "block_source": "https://gofedako.com/mc/java/26.3/",
    "texture_source": "https://mcasset.cloud/26.3/assets/minecraft/textures/block/",
}
WORLDGEN_RULES = {
    "vegetation": [
        ("asset_tree_birch", ["forest", "plains"], 28, 2),
        ("asset_tree_cedar", ["mountains", "foothills"], 34, 2),
        ("asset_tree_maple", ["forest", "hills"], 30, 2),
        ("asset_tree_poplar", ["cold_steppe", "tundra"], 36, 1),
        ("asset_tree_willow", ["forest", "beach"], 38, 1),
    ],
    "ground_cover": [
        ("asset_shrub_moss", ["forest", "lush_caves"], 18, 2),
        ("asset_shrub_reed", ["beach", "ocean", "plains"], 20, 2),
        ("asset_mushroom_cluster_01", ["forest", "lush_caves", "deep_dark"], 24, 1),
        ("asset_boulder_granite", ["hills", "mountains", "foothills"], 28, 2),
        ("asset_boulder_basalt", ["jagged_peaks", "mountains", "dripstone_caves"], 30, 2),
        ("asset_flower_patch_01", ["plains", "forest", "savanna"], 22, 1),
    ],
    "decoration": [
        ("asset_coral_fan", ["beach"], 72, 1),
        ("asset_stalagmite_cluster", ["dripstone_caves", "deep_dark"], 84, 1),
        ("asset_crystal_cluster", ["deep_dark", "mountains"], 96, 1),
        ("asset_mossy_ruin_piece", ["forest", "hills", "scrubland"], 110, 1),
    ],
    "structures": [
        ("asset_cairn", ["hills", "mountains", "tundra"], 6200, 1),
        ("asset_woodland_shrine", ["forest", "plains"], 7600, 1),
    ],
}

FAMILY_COLORS: dict[str, list[tuple[int, int, int]]] = {
    "rock": [(105, 108, 111), (134, 130, 122), (85, 96, 106), (145, 125, 107), (123, 109, 123), (102, 119, 99), (164, 157, 143)],
    "soil": [(132, 91, 62), (111, 74, 52), (154, 116, 76), (116, 86, 68), (83, 72, 58), (166, 131, 90), (126, 111, 83)],
    "wood": [(126, 82, 48), (151, 105, 63), (104, 68, 45), (173, 132, 82), (95, 79, 58), (137, 100, 74), (166, 120, 77)],
    "masonry": [(126, 125, 119), (153, 139, 119), (101, 108, 115), (156, 116, 96), (119, 104, 91), (143, 145, 133), (91, 95, 92)],
    "ceramic": [(224, 209, 178), (184, 103, 77), (187, 147, 75), (179, 84, 86), (116, 100, 151), (72, 119, 160), (75, 145, 139), (91, 142, 100), (144, 153, 81), (81, 84, 86), (191, 128, 154), (115, 83, 67)],
    "fabric": [(232, 224, 207), (204, 183, 151), (180, 127, 72), (186, 83, 75), (204, 117, 124), (142, 101, 158), (68, 97, 157), (71, 143, 161), (75, 150, 129), (135, 158, 79), (90, 99, 110), (119, 79, 61), (199, 199, 191), (157, 106, 78), (112, 122, 79), (159, 136, 98)],
    "metal": [(125, 132, 135), (160, 166, 165), (159, 112, 69), (165, 123, 76), (190, 161, 92), (150, 157, 163), (198, 179, 135), (185, 157, 69), (120, 132, 140), (91, 113, 133), (153, 153, 146), (130, 143, 137), (108, 112, 111), (122, 111, 92), (91, 96, 102), (145, 112, 87)],
    "glass": [(193, 208, 210), (224, 219, 194), (196, 157, 105), (186, 94, 91), (203, 132, 151), (137, 111, 163), (91, 120, 177), (88, 167, 185), (92, 158, 136), (126, 171, 109), (139, 154, 94), (89, 96, 107), (192, 208, 192), (115, 115, 118), (67, 74, 83)],
    "crystal": [(190, 193, 190), (151, 109, 193), (222, 172, 85), (167, 89, 104), (90, 156, 126), (193, 179, 201), (123, 147, 181), (74, 138, 156), (83, 154, 137)],
    "organic": [(95, 125, 68), (112, 143, 73), (77, 111, 64), (121, 134, 83), (151, 112, 111), (68, 137, 125), (84, 124, 137), (167, 127, 93), (102, 148, 85)],
}

MATERIAL_TINTS = {
    # Geological materials
    ("rock", "basalt"): (83, 87, 92), ("rock", "granite"): (132, 130, 126),
    ("rock", "diorite"): (176, 173, 165), ("rock", "andesite"): (124, 126, 122),
    ("rock", "limestone"): (175, 163, 134), ("rock", "marble"): (206, 202, 194),
    ("rock", "slate"): (87, 99, 110), ("rock", "schist"): (121, 116, 111),
    ("rock", "gneiss"): (137, 136, 130), ("rock", "quartzite"): (194, 183, 174),
    ("rock", "sandstone"): (183, 139, 90), ("rock", "shale"): (88, 91, 95),
    ("rock", "chalk"): (213, 209, 194), ("rock", "travertine"): (193, 174, 132),
    ("rock", "porphyry"): (125, 99, 97), ("rock", "serpentine"): (86, 130, 107),
    ("rock", "dolomite"): (172, 168, 150), ("rock", "conglomerate"): (136, 119, 98),
    ("rock", "obsidian"): (43, 41, 51), ("rock", "pumice"): (176, 163, 147),
    ("rock", "tuff"): (121, 121, 113), ("rock", "greystone"): (98, 105, 109),
    # Earth and sand
    ("soil", "loam"): (124, 83, 52), ("soil", "clay"): (156, 86, 64),
    ("soil", "peat"): (79, 57, 40), ("soil", "podzol"): (95, 71, 46),
    ("soil", "humus"): (88, 60, 40), ("soil", "silt"): (154, 131, 99),
    ("soil", "wet_mud"): (87, 70, 59), ("soil", "dry_mud"): (160, 122, 83),
    ("soil", "red_earth"): (146, 75, 55), ("soil", "pale_sand"): (219, 202, 151),
    ("soil", "volcanic_ash"): (74, 77, 79),
    # Timber species
    ("wood", "oak"): (146, 99, 54), ("wood", "pine"): (151, 110, 70),
    ("wood", "birch"): (201, 170, 111), ("wood", "cedar"): (136, 84, 58),
    ("wood", "maple"): (178, 113, 57), ("wood", "ash"): (172, 149, 112),
    ("wood", "walnut"): (97, 61, 42), ("wood", "beech"): (181, 134, 82),
    ("wood", "willow"): (150, 116, 71), ("wood", "poplar"): (177, 135, 88),
    ("wood", "acacia"): (165, 92, 54), ("wood", "jungle"): (118, 79, 48),
    ("wood", "mangrove"): (125, 73, 47), ("wood", "larch"): (160, 105, 59),
    ("wood", "redwood"): (122, 64, 45),
    # Shared dye / ceramic / glass colors
    ("ceramic", "ivory"): (220, 214, 190), ("fabric", "ivory"): (220, 214, 190),
    ("glass", "ivory"): (220, 226, 218), ("ceramic", "cream"): (218, 202, 168),
    ("fabric", "cream"): (218, 202, 168), ("glass", "cream"): (226, 225, 205),
    ("ceramic", "ochre"): (187, 143, 65), ("fabric", "ochre"): (187, 143, 65),
    ("glass", "amber"): (202, 153, 79), ("ceramic", "amber"): (202, 153, 79),
    ("fabric", "amber"): (202, 153, 79), ("ceramic", "scarlet"): (180, 64, 62),
    ("fabric", "scarlet"): (180, 64, 62), ("glass", "scarlet"): (191, 78, 75),
    ("ceramic", "rose"): (199, 120, 137), ("fabric", "rose"): (199, 120, 137),
    ("glass", "rose"): (210, 153, 163), ("ceramic", "violet"): (133, 100, 160),
    ("fabric", "violet"): (133, 100, 160), ("glass", "violet"): (159, 139, 180),
    ("ceramic", "cobalt"): (67, 109, 166), ("fabric", "cobalt"): (67, 109, 166),
    ("glass", "cobalt"): (82, 138, 190), ("ceramic", "teal"): (65, 143, 137),
    ("fabric", "teal"): (65, 143, 137), ("glass", "teal"): (92, 174, 167),
    ("ceramic", "jade"): (78, 145, 105), ("fabric", "jade"): (78, 145, 105),
    ("glass", "jade"): (110, 177, 139), ("ceramic", "olive"): (119, 132, 69),
    ("fabric", "olive"): (119, 132, 69), ("glass", "olive"): (148, 164, 89),
    ("ceramic", "charcoal"): (67, 71, 75), ("fabric", "charcoal"): (67, 71, 75),
    ("glass", "charcoal"): (94, 101, 110), ("fabric", "tan"): (183, 145, 101),
    ("fabric", "coral"): (213, 118, 94), ("fabric", "indigo"): (71, 79, 144),
    ("fabric", "cyan"): (75, 164, 183), ("fabric", "lime"): (143, 177, 68),
    ("fabric", "umber"): (112, 77, 52),
    # Metals and gems
    ("metal", "iron"): (130, 137, 141), ("metal", "steel"): (156, 164, 168),
    ("metal", "copper"): (174, 105, 62), ("metal", "bronze"): (149, 104, 66),
    ("metal", "brass"): (185, 153, 75), ("metal", "tin"): (173, 179, 177),
    ("metal", "silver"): (196, 199, 194), ("metal", "gold"): (205, 169, 58),
    ("metal", "nickel"): (146, 151, 150), ("metal", "cobalt"): (75, 105, 147),
    ("metal", "zinc"): (153, 162, 160), ("metal", "titanium"): (143, 148, 153),
    ("metal", "pewter"): (127, 137, 143), ("metal", "verdigris"): (75, 137, 119),
    ("metal", "black_iron"): (62, 66, 70), ("metal", "meteoric_iron"): (100, 97, 94),
    ("crystal", "quartz"): (196, 196, 187), ("crystal", "amethyst"): (144, 94, 174),
    ("crystal", "citrine"): (218, 174, 68), ("crystal", "garnet"): (156, 62, 74),
    ("crystal", "jade"): (74, 150, 112), ("crystal", "opal"): (198, 183, 199),
    ("crystal", "fluorite"): (113, 151, 181), ("crystal", "azurite"): (65, 121, 168),
    ("crystal", "malachite"): (54, 135, 94),
    # Plants, flowers and fungi
    ("organic", "moss"): (81, 118, 54), ("organic", "lichen"): (142, 148, 83),
    ("organic", "fern"): (83, 133, 58), ("organic", "reed"): (127, 146, 72),
    ("organic", "mushroom"): (153, 114, 83), ("organic", "coral"): (184, 93, 91),
    ("organic", "kelp"): (53, 116, 93), ("organic", "blossom"): (188, 124, 144),
    ("organic", "leaf"): (78, 132, 56),
    ("organic", "flower_white"): (220, 218, 197), ("organic", "flower_yellow"): (218, 185, 59),
    ("organic", "flower_red"): (183, 64, 67), ("organic", "flower_pink"): (207, 117, 148),
    ("organic", "flower_blue"): (74, 118, 183), ("organic", "flower_purple"): (132, 89, 163),
    ("organic", "flower_orange"): (218, 117, 51), ("organic", "mushroom_red"): (169, 65, 65),
    ("organic", "mushroom_gold"): (203, 163, 64), ("organic", "mushroom_blue"): (73, 118, 149),
    ("organic", "mushroom_pale"): (192, 184, 163),
}

for _material in ("granite", "limestone", "slate", "sandstone", "basalt", "marble", "tuff", "laterite", "quartz", "darkstone"):
    MATERIAL_TINTS[("masonry", _material)] = MATERIAL_TINTS.get(("rock", _material), (130, 128, 120))
MATERIAL_TINTS.update({
    ("masonry", "brick"): (148, 75, 62), ("masonry", "adobe"): (175, 112, 73),
    ("masonry", "cobble"): (125, 123, 115), ("masonry", "concrete"): (137, 142, 143),
    ("masonry", "terrazzo"): (153, 148, 138), ("masonry", "flagstone"): (130, 126, 113),
    ("masonry", "laterite"): (158, 91, 65), ("masonry", "quartz"): (193, 187, 176),
    ("masonry", "darkstone"): (67, 69, 72),
    ("glass", "clear"): (178, 207, 211), ("glass", "frosted"): (196, 214, 213),
    ("glass", "cyan"): (91, 173, 190), ("glass", "lime"): (157, 187, 106),
    ("glass", "smoke"): (112, 125, 132),
    ("ceramic", "teal"): (65, 143, 137), ("ceramic", "olive"): (119, 132, 69),
})

PATTERN_BY_FAMILY = {
    "rock": "grain",
    "soil": "mottle",
    "wood": "wood",
    "masonry": "brick",
    "ceramic": "tile",
    "fabric": "weave",
    "metal": "metal",
    "glass": "glass",
    "crystal": "crystal",
    "organic": "organic",
}


def stable_seed(value: str) -> int:
    return int.from_bytes(hashlib.sha256(value.encode("utf-8")).digest()[:8], "big")


def safe_name(value: str) -> str:
    result = re.sub(r"[^a-z0-9]+", "_", value.lower()).strip("_")
    return result or "asset"


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def load_catalog(path: Path = CATALOG_PATH) -> dict[str, Any]:
    doc = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    if doc.get("schema_version") != 1:
        raise ValueError(f"unsupported asset catalog schema in {path}")
    return doc


def family_records(catalog: dict[str, Any]) -> list[dict[str, str]]:
    records: list[dict[str, str]] = []
    for family, config in catalog["families"].items():
        materials = config["materials"]
        variants = config["variants"]
        combos = [(material, variant) for material in materials for variant in variants]
        if len(combos) != config["count"]:
            raise ValueError(f"{family}: catalog count {config['count']} does not match {len(combos)} combinations")
        for material, variant in combos:
            name = f"cu_{safe_name(family)}_{safe_name(material)}_{safe_name(variant)}"
            records.append({"name": name, "family": family, "material": material, "variant": variant})
    minimum = catalog["benchmark"]["block_definitions_minimum"]
    if len(records) < minimum:
        raise ValueError(f"catalog has {len(records)} block definitions; benchmark requires {minimum}")
    return records


def tile_noise(seed: int, width: int = 16, height: int = 16, cells: int = 4) -> list[float]:
    rng = random.Random(seed)
    grid = [[rng.random() * 2.0 - 1.0 for _ in range(cells)] for _ in range(cells)]
    out: list[float] = []
    for y in range(height):
        gy = y * cells / height
        y0 = int(gy) % cells
        fy = gy - int(gy)
        y1 = (y0 + 1) % cells
        for x in range(width):
            gx = x * cells / width
            x0 = int(gx) % cells
            fx = gx - int(gx)
            x1 = (x0 + 1) % cells
            a = grid[y0][x0] * (1 - fx) + grid[y0][x1] * fx
            b = grid[y1][x0] * (1 - fx) + grid[y1][x1] * fx
            out.append(a * (1 - fy) + b * fy)
    return out


def shade(color: tuple[int, int, int], amount: float) -> tuple[int, int, int]:
    return tuple(max(0, min(255, int(channel * amount))) for channel in color)


def generated_texture(family: str, material: str, variant: str, seed: int, size: int = 16) -> Image.Image:
    """Make a tileable, opaque pixel-art albedo texture from material parameters."""
    rng = random.Random(seed)
    colors = FAMILY_COLORS[family]
    base = MATERIAL_TINTS.get((family, material), colors[stable_seed(f"{family}:{material}") % len(colors)])
    colors4 = [shade(base, factor) for factor in (0.63, 0.82, 1.0, 1.19)]
    noise = tile_noise(seed ^ 0x5A17, size, size, 4)
    style = PATTERN_BY_FAMILY[family]
    pixels: list[tuple[int, int, int, int]] = []
    material_bias = (stable_seed(material) % 7) - 3
    variant_bias = stable_seed(variant) % 5
    for y in range(size):
        for x in range(size):
            n = noise[y * size + x]
            value = n * 0.75 + (rng.random() - 0.5) * 0.34
            if style == "wood":
                wave = int(1.2 * math.sin((y + seed % 7) * math.tau / 8))
                grain = (x + wave + material_bias) % 8
                value += 0.28 if grain in (0, 1) else (-0.12 if grain == 2 else 0.02)
                if variant in ("endgrain", "cut"):
                    ring = int(math.sqrt(((x - 7.5) ** 2) * 0.8 + ((y - 7.5) ** 2) * 1.1)) % 5
                    value += 0.17 if ring in (0, 1) else -0.03
            elif style == "brick":
                row = y // 4
                col = (x + (4 if row % 2 else 0)) % 8
                value += -0.58 if col == 0 or y % 4 == 0 else 0.08
            elif style == "tile":
                seam = x % 8 == 0 or y % 8 == 0
                value += -0.53 if seam else 0.06
                if variant in ("mosaic", "patterned", "checked"):
                    value += 0.2 if ((x // 4 + y // 4 + variant_bias) % 2) else -0.04
            elif style == "weave":
                thread = (x + y + variant_bias) % 4
                value += 0.2 if thread in (0, 1) else -0.12
                if x % 4 == 0 or y % 4 == 0:
                    value -= 0.06
            elif style == "metal":
                value += 0.16 if (y + seed % 4) % 5 == 0 else 0.0
                if (x in (1, 14) and y in (1, 14)):
                    value -= 0.34
            elif style == "glass":
                value += 0.23 if (x - y - variant_bias) % 11 in (0, 1) else -0.02
                if variant in ("frosted", "bubbled", "rippled"):
                    value += n * 0.28
            elif style == "crystal":
                facet = (x + y + (x // 4) * 2 + variant_bias) % 9
                value += 0.34 if facet in (0, 1, 2) else -0.11
            elif style == "organic":
                patch = math.sin((x + seed % 9) * math.tau / 8) * math.cos((y + seed % 5) * math.tau / 8)
                value += patch * 0.22
            elif style == "mottle":
                value += n * 0.24
            index = max(0, min(3, int(round(1.5 + value + (variant_bias - 2) * 0.04))))
            rgb = colors4[index]
            if style in ("rock", "grain", "mottle", "organic", "crystal") and rng.random() < 0.055:
                rgb = colors4[max(0, min(3, index + (1 if rng.random() > 0.5 else -1)))]
            pixels.append((*rgb, 176 if style == "glass" else 255))
    image = Image.new("RGBA", (size, size))
    image.putdata(pixels)
    return image


def periodize(image: Image.Image, size: int = 16, seam: int = 2) -> Image.Image:
    """Resize a texture and make opposite border pixels meet without a seam."""
    image = ImageOps.fit(image.convert("RGBA"), (size, size), method=Image.Resampling.LANCZOS)
    alpha = image.getchannel("A")
    rgb = image.convert("RGB").quantize(colors=16, method=Image.Quantize.MEDIANCUT).convert("RGB")
    rgb.putalpha(alpha)
    image = rgb
    width, height = image.size
    px = image.load()
    for distance in range(seam):
        left, right = distance, width - 1 - distance
        for y in range(height):
            a, b = px[left, y], px[right, y]
            avg = tuple(round((a[c] + b[c]) / 2) for c in range(4))
            px[left, y] = px[right, y] = avg
        top, bottom = distance, height - 1 - distance
        for x in range(width):
            a, b = px[x, top], px[x, bottom]
            avg = tuple(round((a[c] + b[c]) / 2) for c in range(4))
            px[x, top] = px[x, bottom] = avg
    return image


def image_pixel_hash(image: Image.Image) -> str:
    return hashlib.sha256(image.convert("RGBA").tobytes()).hexdigest()


def texture_path(pack_dir: Path, stem: str) -> Path:
    return pack_dir / "textures" / "blocks" / f"{stem}.png"


def save_texture(image: Image.Image, pack_dir: Path, stem: str) -> str:
    path = texture_path(pack_dir, stem)
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format="PNG", optimize=True)
    return image_pixel_hash(image)


def load_block_files(pack_id: str) -> list[dict[str, Any]]:
    blocks_dir = REPO / "resource_packs" / pack_id / "blocks"
    result = []
    for path in sorted(blocks_dir.glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8-sig"))
        if data.get("name"):
            result.append(data)
    return result


def minetest_replacement_stems() -> dict[str, str]:
    source_dir = REPO / "resource_packs" / "minetest_default_16" / "textures" / "blocks"
    result: dict[str, str] = {}
    used: set[str] = set()
    for path in sorted(source_dir.glob("*.png")):
        stem = path.stem
        target = f"mt_{safe_name(stem)}"
        if target in used:
            target += "_" + hashlib.sha256(stem.encode()).hexdigest()[:6]
        used.add(target)
        result[stem] = target
    return result


def infer_family(stem: str) -> str:
    value = stem.lower()
    if "default_grass" in value:
        return "organic"
    if any(token in value for token in ("grass_side", "dirt_with_grass", "grass_with_dirt")):
        return "soil"
    if any(token in value for token in ("lava", "fire", "torch", "glowstone", "glow_")):
        return "ceramic"
    if any(token in value for token in ("water", "ice", "glass", "pane")):
        return "glass"
    if any(token in value for token in ("cloth", "wool", "carpet")):
        return "fabric"
    if any(token in value for token in ("diamond", "emerald", "lapis", "redstone", "crystal", "ruby", "amethyst", "mese", "lightgem", "enchant")):
        return "crystal"
    if any(token in value for token in ("gold", "iron", "metal", "anvil", "copper", "bronze", "rail", "cauldron", "sensor", "detector", "brewing", "hopper", "lever")):
        return "metal"
    if any(token in value for token in ("wood", "tree", "log", "plank", "bark", "door", "fence", "bookshelf", "ladder", "boat", "workbench", "crafting_table", "jukebox")):
        return "wood"
    if any(token in value for token in ("brick", "stone", "basalt", "granite", "slate", "rock", "pavement", "furnace", "dispenser", "dropper")):
        return "masonry" if "brick" in value else "rock"
    if any(token in value for token in ("grass", "fern", "leaf", "leaves", "flower", "sapling", "mushroom", "cactus", "vine", "crop", "cocoa", "coral", "reed", "moss", "lichen", "bush", "plant", "needle", "apple", "berry", "melon", "pumpkin", "potato", "carrot", "wheat", "lily", "papyrus", "deadbush", "mycel", "sponge", "stem")):
        return "organic"
    if any(token in value for token in ("dirt", "sand", "clay", "mud", "gravel", "farmland", "snow")):
        return "soil"
    if any(token in value for token in ("brick", "tile", "terracotta", "cake")):
        return "ceramic"
    return "rock"


def material_variant(stem: str, family: str) -> tuple[str, str]:
    clean = safe_name(re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", stem))
    config = load_catalog()["families"][family]
    materials = config["materials"]
    variants = config["variants"]
    words = set(clean.split("_"))
    color_map = {
        "white": "ivory", "light_gray": "cream", "light_grey": "cream", "gray": "charcoal",
        "grey": "charcoal", "black": "charcoal", "yellow": "ochre", "orange": "amber",
        "red": "scarlet", "pink": "rose", "magenta": "rose", "purple": "violet",
        "blue": "cobalt", "light_blue": "cyan", "cyan": "teal", "green": "jade",
        "lime": "lime", "brown": "umber",
    }
    selected: str | None = None
    # Source names provide semantic labels only; the generator never reads
    # reference pixels when building procedural replacement textures.
    if family == "ceramic" and any(t in words for t in ("fire", "lava", "torch", "glowstone", "lit")):
        selected = "scarlet" if "fire" in words or "torch" in words else "amber"
    if family == "glass" and any(t in words for t in ("water", "river", "flowing")):
        selected = "cyan" if "cyan" in materials else "clear"
    if family == "glass" and any(t in words for t in ("ice", "frost", "snow")):
        selected = "clear"
    if family == "fabric":
        cloth_index = re.search(r"(?:cloth|wool|carpet)[_-]?(\d+)", clean)
        cloth_palette = ("ivory", "amber", "rose", "cyan", "ochre", "lime", "rose", "charcoal", "cream", "teal", "violet", "cobalt", "umber", "jade", "scarlet", "charcoal")
        if cloth_index and int(cloth_index.group(1)) < len(cloth_palette):
            selected = cloth_palette[int(cloth_index.group(1))]
    if family in ("fabric", "ceramic", "glass") and selected is None:
        for color_tokens, preferred in (("light_gray", "cream"), ("light_grey", "cream"), ("light_blue", "cyan")):
            if color_tokens in clean:
                selected = preferred
                break
        if selected is None:
            for token in ("white", "gray", "grey", "black", "yellow", "orange", "red", "pink", "magenta", "purple", "blue", "cyan", "green", "lime", "brown"):
                if token in words:
                    selected = color_map[token]
                    break
    if family == "wood":
        for species in ("redwood", "mangrove", "jungle", "birch", "cedar", "maple", "walnut", "beech", "willow", "poplar", "acacia", "larch", "pine", "ash", "oak"):
            if species in words:
                selected = species
                break
        selected = selected or "oak"
    elif family == "soil":
        selected = "pale_sand" if "sand" in words or "snow" in words else "red_earth" if "red" in words else "clay" if "clay" in words else "peat" if "peat" in words else "loam"
    elif family == "rock":
        selected = next((m for m in materials if m in words), "obsidian" if "bedrock" in words else "basalt" if "stone" in words else "granite")
    elif family == "masonry":
        selected = next((m for m in materials if m in words), "brick" if "brick" in words else "cobble" if "cobble" in words else "granite")
    elif family == "metal":
        selected = next((m for m in materials if m in words), "gold" if "gold" in words else "copper" if "copper" in words else "iron")
    elif family == "crystal":
        selected = "jade" if "emerald" in words else "azurite" if "lapis" in words else "amethyst" if "enchant" in words else "garnet" if "ruby" in words or "redstone" in words else "citrine" if "gold" in words or "mese" in words or "lightgem" in words else "fluorite" if "diamond" in words else next((m for m in materials if m in words), "quartz")
    elif family == "organic":
        if "flower" in words or "rose" in words or "dandelion" in words:
            selected = next((f"flower_{color}" for color in ("red", "yellow", "blue", "purple", "pink", "white", "orange") if color in words and f"flower_{color}" in materials), "flower_yellow" if "dandelion" in words or "flower" in words else "flower_red" if "rose" in words else "flower_white")
        elif "mushroom" in words:
            selected = "mushroom_red" if "red" in words else "mushroom_gold" if "gold" in words or "yellow" in words else "mushroom_pale"
        else:
            selected = (
                "flower_red" if "apple" in words or "strawberry" in words else
                "flower_blue" if "blueberry" in words else
                "flower_orange" if any(t in words for t in ("pumpkin", "carrot", "carrots", "cocoa")) else
                "flower_yellow" if any(t in words for t in ("wheat", "crop", "sponge")) else
                "mushroom_pale" if "mycel" in words else
                "lichen" if "deadbush" in words else
                "moss" if any(t in words for t in ("spruce", "pine", "conifer", "needle")) else
                "leaf" if "leaves" in words else
                next((m for m in ("coral", "kelp", "reed", "fern", "moss", "lichen", "leaf") if m in words), "leaf")
            )
    elif family in ("fabric", "ceramic", "glass") and selected is None:
        selected = {"fabric": "tan", "ceramic": "cream", "glass": "clear"}[family]
    if selected not in materials:
        selected = materials[stable_seed(clean + family) % len(materials)]
    variant = "natural"
    if family == "wood":
        variant = "endgrain" if any(t in words for t in ("top", "end")) else "bark" if any(t in words for t in ("side", "bark", "log")) else "plank" if any(t in words for t in ("wood", "plank", "door", "trapdoor", "bookshelf", "fence")) else "stripped"
    elif family == "masonry" and "brick" in words:
        variant = "brick"
    elif family == "masonry" and "cobble" in words:
        variant = "cobble"
    elif family in ("rock", "masonry") and any(t in words for t in ("smooth", "polished")):
        variant = "polished" if "polished" in variants else "cut"
    elif family == "glass":
        variant = "frosted" if any(t in words for t in ("ice", "frost", "snow")) else "plain"
    elif family == "metal":
        variant = "polished" if "gold" in words or "iron" in words else "raw"
    elif family == "organic":
        variant = "bright" if any(t in words for t in ("flower", "torch", "glowstone")) else "fresh"
    if variant not in variants:
        variant = variants[stable_seed(family + clean) % len(variants)]
    return selected, variant


def make_replacement_texture(source_stem: str, target_stem: str, family: str, kind: str, output_dir: Path, hashes: set[str]) -> dict[str, Any]:
    material, variant = material_variant(source_stem, family)
    base_seed = stable_seed(f"cubatarium:{kind}:{source_stem}:{family}")
    image = periodize(generated_texture(family, material, variant, base_seed), 16, 2)
    digest = image_pixel_hash(image)
    salt = 0
    while digest in hashes:
        salt += 1
        image = periodize(generated_texture(family, material, f"{variant}_{salt}", base_seed + salt), 16, 2)
        digest = image_pixel_hash(image)
    hashes.add(digest)
    image.save(texture_path(output_dir, target_stem), format="PNG", optimize=True)
    return {
        "stem": target_stem,
        "family": family,
        "material": material,
        "variant": variant,
        "seed": base_seed,
        "pixel_sha256": digest,
        "source": {
            "kind": kind,
            "reference_only": source_stem,
            "reference_file_sha256": hashlib.sha256(
                (REPO / "resource_packs" / ("minetest_default_16" if kind == "minetest_reference_only" else "cubatarium_geology_cc0") / "textures" / "blocks" / f"{source_stem}.png").read_bytes()
            ).hexdigest(),
        },
        "license": "CC0-1.0",
    }


def block_definition(record: dict[str, str], texture_stem: str, block_id: int) -> dict[str, Any]:
    family = record["family"]
    hardness = {"soil": 0.65, "wood": 1.4, "fabric": 0.35, "glass": 0.45, "organic": 0.45, "ceramic": 1.1, "metal": 3.0, "crystal": 2.8}.get(family, 1.8)
    types = ["building", "natural"] if family in ("rock", "soil", "wood", "masonry") else ["building", "decoration"]
    render: dict[str, Any] = {}
    if family == "glass":
        render["transparent"] = True
    if family == "organic" and (record["material"] in ("blossom", "fern", "reed", "mushroom") or record["material"].startswith(("flower_", "mushroom_"))):
        render.update({"style": "cross", "double_sided": True})
    dig_group = "cracky" if family in ("rock", "masonry", "metal", "crystal") else "crumbly"
    return {
        "name": record["name"],
        "id": block_id,
        "displayName": " ".join(part.capitalize() for part in record["name"][3:].split("_")),
        "hardness": hardness,
        "dig": {"level": 0, "groups": {dig_group: 2}},
        "types": types,
        "textures": [texture_stem] * 6,
        "render": render,
        "assetgen": {"family": family, "material": record["material"], "variant": record["variant"]},
    }


def block_name(records: list[dict[str, str]], family: str, material: str, variant: str) -> str:
    wanted = f"cu_{family}_{material}_{variant}"
    for entry in records:
        if entry["name"] == wanted:
            return wanted
    candidates = [r for r in records if r["family"] == family]
    if not candidates:
        raise ValueError(f"no generated block for family {family}")
    return candidates[stable_seed(wanted) % len(candidates)]["name"]


def add_object_block(blocks: list[dict[str, Any]], x: int, y: int, z: int, kind: str) -> None:
    blocks.append({"dx": x, "dy": y, "dz": z, "type": kind})


def object_document(name: str, voxels: list[dict[str, Any]], tags: list[str], seed: int, material: str | None = None) -> dict[str, Any]:
    source: dict[str, Any] = {"mod": "cubatarium/assetgen", "seed": seed, "license": "CC0-1.0"}
    if material:
        source["material_parameter"] = material
    return {"name": name, "version": 2, "displayName": name.replace("asset_", "").replace("_", " ").title(), "source": source, "tags": tags, "anchor": [0, 0, 0], "blocks": voxels}


def make_tree(name: str, wood: str, records: list[dict[str, str]], size: int = 5) -> dict[str, Any]:
    trunk = block_name(records, "wood", wood, "bark")
    leaves = block_name(records, "organic", "leaf", "fresh")
    voxels: list[dict[str, Any]] = []
    height = size
    for y in range(height):
        add_object_block(voxels, 0, y, 0, trunk)
    canopy_y = height - 1
    radius = 2 if size >= 6 else 1
    for y in range(canopy_y - 1, canopy_y + 2):
        r = radius if y == canopy_y else max(1, radius - 1)
        for x in range(-r, r + 1):
            for z in range(-r, r + 1):
                if abs(x) + abs(z) + abs(y - canopy_y) <= radius + 1 and not (x == 0 and z == 0 and y < height):
                    add_object_block(voxels, x, y, z, leaves)
    return object_document(name, voxels, ["worldgen", "vegetation"], stable_seed(f"tree:{wood}:{size}"), wood)


def make_boulder(name: str, family: str, material: str, records: list[dict[str, str]], seed: int) -> dict[str, Any]:
    rng = random.Random(seed)
    block = block_name(records, family, material, "natural")
    coords: set[tuple[int, int, int]] = set()
    rx, ry, rz = rng.choice((1, 2)), rng.choice((1, 2)), rng.choice((1, 2))
    for x in range(-rx, rx + 1):
        for y in range(0, ry + 1):
            for z in range(-rz, rz + 1):
                norm = (x / max(1, rx)) ** 2 + ((y - ry / 2) / max(1, ry)) ** 2 + (z / max(1, rz)) ** 2
                if norm <= 1.35 and rng.random() > 0.08:
                    coords.add((x, y, z))
    return object_document(name, [{"dx": x, "dy": y, "dz": z, "type": block} for x, y, z in sorted(coords)], ["worldgen", "ground_cover"], seed, material)


def make_shrub(name: str, material: str, records: list[dict[str, str]], seed: int) -> dict[str, Any]:
    rng = random.Random(seed)
    variant = rng.choice(("fresh", "dense", "mature", "spotted"))
    leaf = block_name(records, "organic", material, variant)
    voxel_map: dict[tuple[int, int, int], str] = {}
    radius = rng.choice((1, 2))
    for y in range(2):
        r = radius if y == 0 else max(1, radius - 1)
        for x in range(-r, r + 1):
            for z in range(-r, r + 1):
                if rng.random() > (0.2 if y == 0 else 0.34):
                    voxel_map[(x, y, z)] = leaf
    if material in ("fern", "reed"):
        for stalk in range(rng.choice((2, 3, 4))):
            x, z = rng.choice(((-1, 0), (1, 0), (0, -1), (0, 1)))
            stalk_block = block_name(records, "organic", material, "fresh")
            for y in range(1, rng.choice((2, 3, 4))):
                voxel_map[(x, y, z)] = stalk_block
    voxels = [{"dx": x, "dy": y, "dz": z, "type": block} for (x, y, z), block in sorted(voxel_map.items())]
    return object_document(name, voxels, ["worldgen", "ground_cover", "vegetation"], seed, material)


def make_flower_patch(name: str, records: list[dict[str, str]], seed: int, flower_material: str | None = None) -> dict[str, Any]:
    rng = random.Random(seed)
    palette = ["flower_white", "flower_yellow", "flower_red", "flower_pink", "flower_blue", "flower_purple", "flower_orange"]
    if flower_material:
        palette = [flower_material]
    soil = block_name(records, "soil", "loam", "natural")
    flowers = [block_name(records, "organic", material, rng.choice(("fresh", "mature", "young", "bright", "spotted"))) for material in palette]
    voxels = [{"dx": x, "dy": 0, "dz": z, "type": soil} for x, z in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1))]
    for x, z in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1)):
        if rng.random() > 0.23:
            voxels.append({"dx": x, "dy": 1, "dz": z, "type": rng.choice(flowers)})
    return object_document(name, voxels, ["worldgen", "ground_cover", "plants"], seed, ",".join(palette))


def make_ruin(name: str, records: list[dict[str, str]], seed: int) -> dict[str, Any]:
    rng = random.Random(seed)
    stone = block_name(records, "masonry", "limestone", "block")
    moss = block_name(records, "organic", "moss", "mature")
    voxels: list[dict[str, Any]] = []
    height = rng.choice((3, 4, 5))
    for x, z in ((-2, -2), (2, -2), (-2, 2), (2, 2)):
        for y in range(height - rng.randrange(0, 2)):
            voxels.append({"dx": x, "dy": y, "dz": z, "type": moss if rng.random() < 0.12 else stone})
    for x in range(-2, 3):
        voxels.append({"dx": x, "dy": 0, "dz": 0, "type": stone})
    return object_document(name, voxels, ["worldgen", "structure", "decoration"], seed, "limestone")


def make_mushroom_cluster(name: str, records: list[dict[str, str]], seed: int) -> dict[str, Any]:
    rng = random.Random(seed)
    cap_material = rng.choice(("mushroom_red", "mushroom_gold", "mushroom_blue", "mushroom_pale"))
    cap_variant = rng.choice(("fresh", "mature", "spotted", "striped", "bright"))
    cap = block_name(records, "organic", cap_material, cap_variant)
    stem = block_name(records, "soil", "clay", "pale")
    voxels: list[dict[str, Any]] = []
    positions = rng.sample(((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1)), rng.choice((2, 3, 4)))
    for x, z in positions:
        height = rng.choice((2, 2, 3))
        for y in range(height):
            voxels.append({"dx": x, "dy": y, "dz": z, "type": stem})
        voxels.append({"dx": x, "dy": height, "dz": z, "type": cap})
    return object_document(name, voxels, ["worldgen", "ground_cover", "fungi"], seed, cap_material)


def make_coral_fan(name: str, records: list[dict[str, str]], seed: int) -> dict[str, Any]:
    rng = random.Random(seed)
    coral = block_name(records, "organic", "coral", rng.choice(("fresh", "dense", "bright", "mature")))
    sand = block_name(records, "soil", "pale_sand", "natural")
    voxels = [{"dx": x, "dy": 0, "dz": 0, "type": sand} for x in range(-2, 3)]
    for x in range(-2, 3):
        height = max(1, 4 - abs(x) + rng.choice((0, 0, 1)))
        for y in range(1, height + 1):
            voxels.append({"dx": x, "dy": y, "dz": 0, "type": coral})
    return object_document(name, voxels, ["decoration", "aquatic"], seed, "coral")


def make_stalagmites(name: str, records: list[dict[str, str]], seed: int) -> dict[str, Any]:
    rng = random.Random(seed)
    stone = block_name(records, "rock", rng.choice(("limestone", "chalk", "tuff")), rng.choice(("natural", "layered", "rough")))
    voxels: list[dict[str, Any]] = []
    for x, z in ((-2, 0), (0, 0), (2, 0), (-1, 1), (1, -1)):
        height = rng.choice((2, 3, 4, 5))
        for y in range(height):
            if y >= height - 1 and abs(x) + abs(z) > 1:
                continue
            voxels.append({"dx": x, "dy": y, "dz": z, "type": stone})
    return object_document(name, voxels, ["decoration", "cave"], seed, "limestone")


def make_crystal_cluster(name: str, records: list[dict[str, str]], seed: int) -> dict[str, Any]:
    rng = random.Random(seed)
    base = block_name(records, "rock", "basalt", "natural")
    crystal_materials = ["quartz", "amethyst", "citrine", "garnet", "jade", "opal", "fluorite", "azurite", "malachite"]
    voxels = [{"dx": x, "dy": 0, "dz": z, "type": base} for x, z in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1))]
    for index, (x, z) in enumerate(((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1))):
        crystal = block_name(records, "crystal", crystal_materials[(seed + index) % len(crystal_materials)], "cluster")
        for y in range(rng.choice((2, 3, 4))):
            voxels.append({"dx": x, "dy": y + 1, "dz": z, "type": crystal})
    return object_document(name, voxels, ["decoration", "cave", "crystal"], seed, "crystal")


def make_cairn(name: str, records: list[dict[str, str]], seed: int) -> dict[str, Any]:
    rng = random.Random(seed)
    materials = ["granite", "basalt", "slate", "sandstone", "schist", "gneiss"]
    voxels: list[dict[str, Any]] = []
    height = rng.choice((3, 4, 5))
    for y in range(height):
        material = materials[(seed + y) % len(materials)]
        stone = block_name(records, "rock", material, rng.choice(("natural", "rough", "weathered")))
        radius = max(0, (height - y - 1) // 2)
        for x in range(-radius, radius + 1):
            for z in range(-radius, radius + 1):
                if abs(x) + abs(z) <= radius + 1 and rng.random() > 0.15:
                    voxels.append({"dx": x, "dy": y, "dz": z, "type": stone})
    return object_document(name, voxels, ["decoration", "structure"], seed, "stone")


def make_woodland_shrine(name: str, records: list[dict[str, str]], seed: int) -> dict[str, Any]:
    rng = random.Random(seed)
    stone = block_name(records, "masonry", "limestone", "weathered")
    wood = block_name(records, "wood", rng.choice(("oak", "cedar", "willow")), "carved")
    moss = block_name(records, "organic", "moss", "mature")
    voxels: list[dict[str, Any]] = []
    for x in range(-2, 3):
        for z in range(-2, 3):
            voxels.append({"dx": x, "dy": 0, "dz": z, "type": stone})
    for x, z in ((-2, -2), (2, -2), (-2, 2), (2, 2)):
        for y in range(1, 4):
            voxels.append({"dx": x, "dy": y, "dz": z, "type": wood if y < 3 else moss})
    for x in range(-1, 2):
        voxels.append({"dx": x, "dy": 3, "dz": -2, "type": wood})
        voxels.append({"dx": x, "dy": 3, "dz": 2, "type": wood})
    return object_document(name, voxels, ["decoration", "structure", "ruin"], seed, "woodland shrine")


def generated_objects(records: list[dict[str, str]]) -> list[dict[str, Any]]:
    objects: list[dict[str, Any]] = []
    for wood in ("oak", "pine", "birch", "cedar", "maple", "ash", "walnut", "beech", "willow", "poplar", "acacia", "jungle", "mangrove", "larch", "redwood"):
        objects.append(make_tree(f"asset_tree_{wood}", wood, records, 6 if wood in ("redwood", "jungle") else 5))
    for material in ("moss", "fern", "reed", "leaf", "lichen", "blossom", "coral", "kelp"):
        name = f"asset_shrub_{material}"
        objects.append(make_shrub(name, material, records, stable_seed(name)))
    for material in ("granite", "basalt", "limestone", "marble", "slate", "schist", "gneiss", "sandstone", "chalk", "obsidian", "pumice", "tuff"):
        name = f"asset_boulder_{material}"
        objects.append(make_boulder(name, "rock", material, records, stable_seed(name)))
    for index in range(16):
        name = f"asset_flower_patch_{index + 1:02d}"
        objects.append(make_flower_patch(name, records, stable_seed(name)))
    for index in range(8):
        name = f"asset_mushroom_cluster_{index + 1:02d}"
        objects.append(make_mushroom_cluster(name, records, stable_seed(name)))
    objects.append(make_coral_fan("asset_coral_fan", records, stable_seed("asset_coral_fan")))
    objects.append(make_stalagmites("asset_stalagmite_cluster", records, stable_seed("asset_stalagmite_cluster")))
    objects.append(make_crystal_cluster("asset_crystal_cluster", records, stable_seed("asset_crystal_cluster")))
    objects.append(make_ruin("asset_mossy_ruin_piece", records, stable_seed("asset_mossy_ruin_piece")))
    objects.append(make_cairn("asset_cairn", records, stable_seed("asset_cairn")))
    objects.append(make_woodland_shrine("asset_woodland_shrine", records, stable_seed("asset_woodland_shrine")))
    for index in range(12):
        name = f"asset_standing_stone_{index + 1:02d}"
        seed = stable_seed(name)
        rng = random.Random(seed)
        material = ("slate", "granite", "basalt", "sandstone", "schist", "gneiss", "limestone", "marble", "chalk", "obsidian", "pumice", "tuff")[index]
        stone = block_name(records, "rock", material, rng.choice(("natural", "weathered", "rough", "layered")))
        height = rng.choice((2, 3, 4, 5))
        voxels = [{"dx": 0, "dy": y, "dz": 0, "type": stone} for y in range(height)]
        if rng.random() < 0.5:
            voxels.extend([{"dx": -1, "dy": 0, "dz": 0, "type": stone}, {"dx": 1, "dy": 0, "dz": 0, "type": stone}])
        else:
            voxels.extend([{"dx": 0, "dy": 0, "dz": -1, "type": stone}, {"dx": 0, "dy": 0, "dz": 1, "type": stone}])
        objects.append(object_document(name, voxels, ["decoration", "structure"], seed, material))
    return objects


def ensure_worldgen_rules(objects: list[dict[str, Any]]) -> int:
    path = REPO / "content" / "object_features.json"
    doc = json.loads(path.read_text(encoding="utf-8-sig"))
    names = {obj["name"] for obj in objects}
    additions = 0
    for pool, rules in WORLDGEN_RULES.items():
        existing = doc.setdefault(pool, [])
        next_seed = max([int(rule.get("seed_offset", 0)) for rule in existing if isinstance(rule, dict)] + [10000]) + 1
        for name, biomes, spacing_or_chance, weight in rules:
            if name not in names:
                continue
            qualified = f"{PACK_ID}::{name}"
            if any(rule.get("object") == qualified for rule in existing if isinstance(rule, dict)):
                continue
            rule: dict[str, Any] = {"object": qualified, "biomes": biomes, "weight": weight, "seed_offset": next_seed, "calibrated": True}
            if pool == "structures":
                rule["chance_per_column"] = spacing_or_chance
            else:
                rule["spacing"] = spacing_or_chance
            if pool == "decoration":
                rule["surface_constraint"] = "any_land"
            existing.append(rule)
            next_seed += 1
            additions += 1
    if additions:
        path.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return additions


def create_previews(pack_dir: Path, entries: list[tuple[str, Path]], no_preview: bool) -> None:
    if no_preview:
        return
    create_texture_sheets(entries, pack_dir / "previews")


def create_texture_sheets(entries: list[tuple[str, Path]], preview_dir: Path) -> None:
    preview_dir.mkdir(parents=True, exist_ok=True)
    per_page = 100
    font = ImageFont.load_default()
    for page_start in range(0, len(entries), per_page):
        page = entries[page_start : page_start + per_page]
        cols, cell_w, cell_h = 10, 72, 60
        rows = math.ceil(len(page) / cols)
        canvas = Image.new("RGB", (cols * cell_w, rows * cell_h), (28, 31, 34))
        draw = ImageDraw.Draw(canvas)
        for index, (label, path) in enumerate(page):
            x, y = (index % cols) * cell_w, (index // cols) * cell_h
            tile = Image.open(path).convert("RGBA").resize((32, 32), Image.Resampling.NEAREST)
            canvas.paste(tile, (x + 20, y + 2), tile)
            draw.text((x + 2, y + 37), label[:11], font=font, fill=(230, 230, 230))
        canvas.save(preview_dir / f"texture_catalog_{page_start // per_page + 1:02d}.png", optimize=True)


def create_reference_comparison(pack_dir: Path, reference_pack_id: str, output_dir: Path) -> int:
    manifest_path = pack_dir / "asset_manifest.json"
    if not manifest_path.is_file():
        raise ValueError(f"missing asset manifest in {pack_dir}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    kind = "minetest_reference_only" if reference_pack_id == "minetest_default_16" else "geology_reference_only"
    reference_dir = REPO / "resource_packs" / reference_pack_id / "textures" / "blocks"
    entries = []
    for item in manifest.get("textures", []):
        source = item.get("source", {})
        if source.get("kind") != kind:
            continue
        source_path = reference_dir / f"{source.get('reference_only')}.png"
        output_path = texture_path(pack_dir, item["stem"])
        if source_path.is_file() and output_path.is_file():
            entries.append((source_path.stem, item["stem"], source_path, output_path))
    if not entries:
        raise ValueError(f"no {reference_pack_id} reference textures found in the pack manifest")
    output_dir.mkdir(parents=True, exist_ok=True)
    per_page, cols, cell_w, cell_h = 80, 8, 112, 64
    font = ImageFont.load_default()
    for page_start in range(0, len(entries), per_page):
        page = entries[page_start : page_start + per_page]
        rows = math.ceil(len(page) / cols)
        canvas = Image.new("RGB", (cols * cell_w, 20 + rows * cell_h), (28, 31, 34))
        draw = ImageDraw.Draw(canvas)
        draw.text((4, 4), f"{reference_pack_id} reference → Cubatarium replacement", font=font, fill=(245, 245, 245))
        for index, (source_stem, target_stem, source_path, output_path) in enumerate(page):
            x, y = (index % cols) * cell_w, 20 + (index // cols) * cell_h
            for offset, path in ((8, source_path), (52, output_path)):
                tile = Image.open(path).convert("RGBA").resize((36, 36), Image.Resampling.NEAREST)
                canvas.paste(tile, (x + offset, y + 1), tile)
            draw.text((x + 2, y + 39), f"{source_stem[:7]} > {target_stem[:7]}", font=font, fill=(230, 230, 230))
        canvas.save(output_dir / f"comparison_{page_start // per_page + 1:02d}.png", optimize=True)
    return len(entries)


def command_preview(args: argparse.Namespace) -> int:
    if args.compare_reference:
        output_dir = args.output or (REPO / "assetgen_output" / "reference_qa" / args.compare_reference)
        resolved_pack = args.pack.resolve()
        resolved_output = output_dir.resolve()
        if resolved_output.is_relative_to(PACK_DIR.resolve()):
            raise ValueError("reference comparison sheets must be stored outside the distributable CC0 pack")
        count = create_reference_comparison(resolved_pack, args.compare_reference, resolved_output)
        print(json.dumps({"reference_pack": args.compare_reference, "count": count, "preview_directory": str(resolved_output)}, ensure_ascii=False))
        return 0
    if args.object:
        object_doc = json.loads(args.object.read_text(encoding="utf-8-sig"))
        output = args.output or args.object.with_name(args.object.stem + "_preview.png")
        preview = preview_object(object_doc, output)
        print(str(preview.resolve()))
        return 0
    if not args.textures or not args.textures.is_dir():
        raise ValueError("preview --textures requires an existing texture directory")
    entries = [(path.stem, path) for path in sorted(args.textures.glob("*.png"))]
    if not entries:
        raise ValueError(f"no PNG textures in {args.textures}")
    output_dir = args.output or (args.textures.parent / "previews")
    create_texture_sheets(entries, output_dir)
    print(json.dumps({"count": len(entries), "preview_directory": str(output_dir.resolve())}, ensure_ascii=False))
    return 0


def build_pack(no_preview: bool = False, add_worldgen: bool = True) -> dict[str, int]:
    catalog = load_catalog()
    records = family_records(catalog)
    PACK_DIR.mkdir(parents=True, exist_ok=True)
    (PACK_DIR / "blocks").mkdir(parents=True, exist_ok=True)
    (PACK_DIR / "objects").mkdir(parents=True, exist_ok=True)
    (PACK_DIR / "textures" / "blocks").mkdir(parents=True, exist_ok=True)
    write_json(PACK_DIR / "pack.json", {
        "id": PACK_ID,
        "name": "Cubatarium Original Worldgen CC0",
        "version": 1,
        "license": "CC0-1.0",
        "resolution": 16,
        "priority": 21,
        "pack_format": 1,
        "worldgen_role": "secondary",
        "merge_mode": "skip_existing",
        "depends": [],
        "conflicts": [],
        "min_game_version": "0.1.0",
    })
    minetest_stems = minetest_replacement_stems()
    geology_stems = {p.stem: f"geo_{safe_name(p.stem)}" for p in (REPO / "resource_packs" / "cubatarium_geology_cc0" / "textures" / "blocks").glob("*.png")}
    hashes: set[str] = set()
    manifest_textures: list[dict[str, Any]] = []
    preview_entries: list[tuple[str, Path]] = []
    for stem, target in minetest_stems.items():
        entry = make_replacement_texture(stem, target, infer_family(stem), "minetest_reference_only", PACK_DIR, hashes)
        manifest_textures.append(entry)
        preview_entries.append((target, texture_path(PACK_DIR, target)))
    for stem, target in geology_stems.items():
        entry = make_replacement_texture(stem, target, infer_family(stem), "geology_reference_only", PACK_DIR, hashes)
        manifest_textures.append(entry)
        preview_entries.append((target, texture_path(PACK_DIR, target)))
    first_generated_block_id = 1025
    if first_generated_block_id + len(records) - 1 > 4095:
        raise ValueError("generated block catalog exceeds the stable pack id range")
    for block_index, record in enumerate(records):
        target = record["name"]
        seed = stable_seed(f"cubatarium:{record['name']}")
        image = periodize(generated_texture(record["family"], record["material"], record["variant"], seed), 16, 2)
        digest = image_pixel_hash(image)
        salt = 0
        while digest in hashes:
            salt += 1
            image = periodize(generated_texture(record["family"], record["material"], f"{record['variant']}_{salt}", seed + salt), 16, 2)
            digest = image_pixel_hash(image)
        hashes.add(digest)
        image.save(texture_path(PACK_DIR, target), format="PNG", optimize=True)
        write_json(PACK_DIR / "blocks" / f"{target}.json", block_definition(record, target, first_generated_block_id + block_index))
        manifest_textures.append({"stem": target, "family": record["family"], "material": record["material"], "variant": record["variant"], "seed": seed, "pixel_sha256": digest, "source": {"kind": "procedural_parameters"}, "license": "CC0-1.0"})
        preview_entries.append((target, texture_path(PACK_DIR, target)))
    minetest_blocks = load_block_files("minetest_default_16")
    geology_blocks = load_block_files("cubatarium_geology_cc0")
    override_map: dict[str, list[dict[str, Any]]] = {}
    for pack_id, blocks, replacements in (("minetest_default_16", minetest_blocks, minetest_stems), ("cubatarium_geology_cc0", geology_blocks, geology_stems)):
        for block in blocks:
            grouped: dict[str, list[str]] = defaultdict(list)
            for face_index, stem in enumerate((block.get("textures") or [])[:6]):
                if stem in replacements:
                    grouped[replacements[stem]].append(("top", "bottom", "north", "south", "west", "east")[face_index])
            if grouped:
                override_map.setdefault(block["name"], []).extend({"stem": stem, "faces": faces} for stem, faces in sorted(grouped.items()))
    write_json(PACK_DIR / "texture_overrides.json", override_map)
    (PACK_DIR / "texture_overrides.yaml").write_text(
        yaml.safe_dump(override_map, sort_keys=False, allow_unicode=True), encoding="utf-8"
    )
    objects = generated_objects(records)
    for obj in objects:
        write_json(PACK_DIR / "objects" / f"{obj['name']}.json", obj)
    worldgen_rules_added = ensure_worldgen_rules(objects) if add_worldgen else 0
    feature_doc = json.loads((REPO / "content" / "object_features.json").read_text(encoding="utf-8-sig"))
    qualified_prefix = f"{PACK_ID}::"
    worldgen_rule_count = sum(
        1
        for pool in WORLDGEN_RULES
        for rule in feature_doc.get(pool, [])
        if isinstance(rule, dict)
        and isinstance(rule.get("object"), str)
        and rule["object"].startswith(qualified_prefix)
    )
    original_minetest_count = len(list((REPO / "resource_packs" / "minetest_default_16" / "textures" / "blocks").glob("*.png")))
    original_geology_count = len(list((REPO / "resource_packs" / "cubatarium_geology_cc0" / "textures" / "blocks").glob("*.png")))
    texture_manifest = {
        "schema_version": 1,
        "benchmark": MINECRAFT_REFERENCE,
        "generator": {"name": "Cubatarium AssetGen", "version": 1, "backend": "procedural", "resolution": 16},
        "counts": {
            "block_definitions": len(records),
            "textures": len(manifest_textures),
            "minetest_textures_replaced": len(minetest_stems),
            "geology_textures_replaced": len(geology_stems),
            "minetest_block_definitions_overridden": sum(1 for block in minetest_blocks if block["name"] in override_map),
            "geology_block_definitions_overridden": sum(1 for block in geology_blocks if block["name"] in override_map),
            "objects": len(objects),
            "worldgen_rules": worldgen_rule_count,
            "worldgen_rules_added_this_build": worldgen_rules_added,
        },
        "reference_inventory": {"minetest_default_16_pngs": original_minetest_count, "cubatarium_geology_cc0_pngs": original_geology_count},
        "textures": sorted(manifest_textures, key=lambda item: item["stem"]),
    }
    write_json(PACK_DIR / "asset_manifest.json", texture_manifest)
    (PACK_DIR / "LICENSE.txt").write_text(
        "Original Cubatarium procedural block textures, block definitions, and voxel objects in this pack are dedicated to the public domain under CC0 1.0.\n"
        "License: https://creativecommons.org/publicdomain/zero/1.0/\n"
        "Reference texture file names were inspected to classify broad material families. Source texture pixels were not used by the procedural generator.\n"
        "Minecraft Java Edition 26.3 counts are used only as a catalog-size benchmark; no Minecraft assets are included.\n",
        encoding="utf-8",
    )
    create_previews(PACK_DIR, preview_entries, no_preview)
    return {"blocks": len(records), "textures": len(manifest_textures), "minetest_replacements": len(minetest_stems), "geology_replacements": len(geology_stems), "objects": len(objects), "worldgen_rules": worldgen_rule_count, "worldgen_rules_added_this_build": worldgen_rules_added}


def memory_available_gb() -> float | None:
    try:
        import psutil  # type: ignore
        return psutil.virtual_memory().available / (1024**3)
    except ImportError:
        pass
    if os.name == "nt":
        try:
            import ctypes
            class MemoryStatus(ctypes.Structure):
                _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong), ("ullTotalPhys", ctypes.c_ulonglong), ("ullAvailPhys", ctypes.c_ulonglong), ("ullTotalPageFile", ctypes.c_ulonglong), ("ullAvailPageFile", ctypes.c_ulonglong), ("ullTotalVirtual", ctypes.c_ulonglong), ("ullAvailVirtual", ctypes.c_ulonglong), ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]
            status = MemoryStatus()
            status.dwLength = ctypes.sizeof(status)
            if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
                return status.ullAvailPhys / (1024**3)
        except Exception:
            return None
    try:
        return os.sysconf("SC_AVPHYS_PAGES") * os.sysconf("SC_PAGE_SIZE") / (1024**3)
    except (ValueError, OSError, AttributeError):
        return None


def cached_model_dir(model_id: str) -> Path:
    configured = os.environ.get("CUBATARIUM_ASSETGEN_HOME")
    if configured:
        root = Path(configured)
    elif os.name == "nt":
        root = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local")) / "Cubatarium" / "AssetGen"
    else:
        root = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "cubatarium" / "assetgen"
    return root / "models" / safe_name(model_id)


def cached_diffusers_model_id() -> str | None:
    model_root = cached_model_dir("__catalog__").parent
    if not model_root.is_dir():
        return None
    for model_dir in sorted(path for path in model_root.iterdir() if path.is_dir()):
        model_index = model_dir / "model_index.json"
        metadata_path = model_dir / "assetgen-model.json"
        if not model_index.is_file():
            continue
        if metadata_path.is_file():
            try:
                metadata = json.loads(metadata_path.read_text(encoding="utf-8-sig"))
                if isinstance(metadata.get("repo_id"), str):
                    if metadata["repo_id"].lower().startswith("hf-internal-testing/"):
                        continue
                    return metadata["repo_id"]
            except (OSError, json.JSONDecodeError):
                continue
        return str(model_dir)
    return None


def model_weight_bytes(model: Path) -> int:
    if model.is_file():
        return model.stat().st_size
    total = 0
    if model.is_dir():
        for path in model.rglob("*"):
            if path.is_file() and path.suffix.lower() in (".safetensors", ".bin", ".ckpt", ".pt"):
                total += path.stat().st_size
    return total


def resolve_device(torch: Any, requested: str, profile: str, model: Path | None) -> tuple[str, str]:
    if profile == "offline":
        raise RuntimeError("offline resource profile disables neural inference")
    if requested == "cpu":
        return "cpu", "none"
    cuda = bool(torch.cuda.is_available())
    if requested == "cuda" and not cuda:
        raise RuntimeError("CUDA requested but PyTorch reports no CUDA device")
    if not cuda:
        return "cpu", "none"
    if profile == "cpu":
        return "cpu", "none"
    if requested == "cuda":
        return "cuda", "sequential" if profile == "low-vram" else "none"
    free_vram = torch.cuda.mem_get_info()[0] / (1024**3)
    weights_gb = (model_weight_bytes(model) if model else 0) / (1024**3)
    estimated_vram = max(6.0, weights_gb * 1.6)
    available_ram = memory_available_gb()
    if requested == "cuda":
        return "cuda", "none"
    if free_vram >= estimated_vram:
        return "cuda", "none"
    if available_ram is None or available_ram < max(8.0, weights_gb * 1.8):
        return "cpu", "none"
    return "cuda", "sequential" if profile == "low-vram" else "model"


def run_diffusers(args: argparse.Namespace) -> Image.Image:
    try:
        import torch
        from diffusers import AutoPipelineForImage2Image, DiffusionPipeline
    except ImportError as exc:
        raise RuntimeError("Diffusers backend requires the optional ML dependencies; see requirements-assetgen-ml.txt") from exc
    if not args.model:
        raise RuntimeError("Diffusers backend requires --model (local path or Hugging Face model id)")
    if args.allow_download and not args.revision:
        raise RuntimeError("network model downloads require a pinned --revision; install the model first or supply --revision")
    model = Path(args.model)
    if not model.exists() and not args.allow_download:
        model = cached_model_dir(args.model)
        if not model.exists():
            raise RuntimeError("model is not cached; run `assetgen.py models install <model-id> --revision <revision>` first")
    device, offload = resolve_device(torch, args.device, args.resource_profile, model if model.exists() else None)
    dtype = torch.float16 if device == "cuda" else torch.float32
    from_pretrained_kwargs: dict[str, Any] = {"torch_dtype": dtype, "local_files_only": not args.allow_download}
    if args.revision:
        from_pretrained_kwargs["revision"] = args.revision
    if args.input:
        pipe = AutoPipelineForImage2Image.from_pretrained(str(model), **from_pretrained_kwargs)
    else:
        pipe = DiffusionPipeline.from_pretrained(str(model), **from_pretrained_kwargs)
    safety_checker = getattr(pipe, "safety_checker", None)
    feature_extractor = getattr(pipe, "feature_extractor", None)
    vision_model = getattr(safety_checker, "vision_model", None)
    safety_size = getattr(getattr(vision_model, "config", None), "image_size", None)
    if isinstance(safety_size, int) and feature_extractor is not None:
        # Some compact test checkpoints ship a CLIP processor at 224px while
        # their safety-checker vision model expects a smaller input (for
        # example, 30px). Keep the checker enabled and align its processor.
        feature_extractor.size = {"shortest_edge": safety_size}
        feature_extractor.crop_size = {"height": safety_size, "width": safety_size}
    if device == "cuda" and offload == "sequential":
        pipe.enable_sequential_cpu_offload()
    elif device == "cuda" and offload == "model":
        pipe.enable_model_cpu_offload()
    else:
        pipe = pipe.to(device)
    generator = torch.Generator(device=device).manual_seed(args.seed)
    prompt = args.prompt or f"seamless 16-bit voxel game texture, {args.material or 'natural material'}, hand painted pixel art, 16x16 game texture, no lighting, no shadows"
    call_args: dict[str, Any] = {"prompt": prompt, "generator": generator}
    if args.input:
        call_args.update({"image": Image.open(args.input).convert("RGB"), "strength": args.strength})
    result = pipe(**call_args).images[0]
    return result


def generate_texture_command(args: argparse.Namespace) -> int:
    backend = args.backend
    if args.resource_profile == "offline" and backend == "auto":
        backend = "procedural"
    elif args.resource_profile == "offline" and backend == "diffusers":
        raise RuntimeError("--backend diffusers requires a non-offline --resource-profile")
    if backend == "auto":
        if not args.model:
            args.model = cached_diffusers_model_id()
        backend = "diffusers" if args.model else "procedural"
    if backend == "diffusers":
        try:
            image = run_diffusers(args)
        except Exception as exc:
            if args.backend == "auto":
                print(f"assetgen: neural backend unavailable ({exc}); using procedural backend", file=sys.stderr)
                backend = "procedural"
            else:
                raise
    if backend == "procedural":
        family = args.family or infer_family(args.material or "rock")
        material = safe_name(args.material or "basalt")
        variant = safe_name(args.variant or "natural")
        image = generated_texture(family, material, variant, args.seed)
        if args.input:
            # Reference mode transfers broad colors only; original pixels are not copied.
            reference = Image.open(args.input).convert("RGB").resize((4, 4), Image.Resampling.BOX)
            avg = tuple(sum(reference.getpixel((x, y))[c] for x in range(4) for y in range(4)) // 16 for c in range(3))
            tint = Image.new("RGBA", image.size, (*avg, 255))
            image = Image.blend(image, tint, 0.12)
    output = periodize(image, 16, args.seam)
    output_path = args.output.resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output.save(output_path, format="PNG", optimize=True)
    model_path = Path(args.model) if args.model else None
    if model_path and not model_path.exists():
        model_path = cached_model_dir(args.model)
    model_metadata = {}
    if model_path and (model_path / "assetgen-model.json").is_file():
        model_metadata = json.loads((model_path / "assetgen-model.json").read_text(encoding="utf-8-sig"))
    provenance = {
        "output": str(output_path),
        "backend": backend,
        "seed": args.seed,
        "material": args.material,
        "family": args.family,
        "variant": args.variant,
        "model": args.model if backend == "diffusers" else None,
        "model_revision": (args.revision or model_metadata.get("revision")) if backend == "diffusers" else None,
        "input_sha256": hashlib.sha256(Path(args.input).read_bytes()).hexdigest() if args.input else None,
        "source_license": "REVIEW_REQUIRED" if args.input else None,
        "license": "REVIEW_REQUIRED" if args.input or backend == "diffusers" else "CC0-1.0",
        "license_review_required": bool(args.input or backend == "diffusers"),
        "resolution": [16, 16],
    }
    write_json(output_path.with_suffix(output_path.suffix + ".assetgen.json"), provenance)
    if not getattr(args, "quiet", False):
        print(json.dumps(provenance, ensure_ascii=False))
    return 0


def texture_batch_command(args: argparse.Namespace) -> int:
    catalog_path = args.catalog or CATALOG_PATH
    catalog = load_catalog(catalog_path)
    records = family_records(catalog)
    output_dir = (args.output_dir or (REPO / "assetgen_output" / "textures")).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    entries: list[dict[str, Any]] = []
    for record in records:
        item = copy.copy(args)
        item.action = None
        item.family = record["family"]
        item.material = record["material"]
        item.variant = record["variant"]
        item.seed = stable_seed(f"cubatarium:{record['name']}")
        item.output = output_dir / f"{record['name']}.png"
        item.prompt = args.prompt or f"seamless pixel-art voxel texture of {record['material']} {record['variant']}, no lighting, no shadows"
        item.quiet = True
        generate_texture_command(item)
        sidecar = item.output.with_suffix(item.output.suffix + ".assetgen.json")
        entries.append(json.loads(sidecar.read_text(encoding="utf-8-sig")))
    report = {
        "schema_version": 1,
        "catalog": str(catalog_path.resolve()),
        "backend_requested": args.backend,
        "count": len(entries),
        "output_directory": str(output_dir),
        "textures": entries,
    }
    write_json(output_dir / "assetgen_batch_manifest.json", report)
    create_texture_sheets(
        [(path.stem, path) for path in sorted(output_dir.glob("*.png"))],
        output_dir / "previews",
    )
    print(json.dumps({"status": "OK", "count": len(entries), "output_directory": str(output_dir)}, ensure_ascii=False))
    return 0


def write_object(path: Path, object_doc: dict[str, Any]) -> None:
    if not object_doc.get("blocks"):
        raise ValueError("decor object must contain at least one voxel")
    write_json(path, object_doc)


def voxel_palette_types(mesh: Any, points: Any, records: list[dict[str, str]]) -> list[str] | None:
    """Map reconstructed mesh colors to the nearest generated Cubatarium materials."""
    try:
        import numpy as np
        from scipy.spatial import cKDTree

        visual = mesh.visual
        if getattr(visual, "kind", None) == "texture" and hasattr(visual, "to_color"):
            visual = visual.to_color()
        vertex_colors = getattr(visual, "vertex_colors", None)
        if vertex_colors is None or len(vertex_colors) != len(mesh.vertices):
            face_colors = np.asarray(getattr(visual, "face_colors", []))
            if len(face_colors) != len(mesh.faces):
                return None
            accumulated = np.zeros((len(mesh.vertices), 3), dtype=np.float32)
            counts = np.zeros(len(mesh.vertices), dtype=np.float32)
            for corner in range(mesh.faces.shape[1]):
                vertices = mesh.faces[:, corner]
                np.add.at(accumulated, vertices, face_colors[:, :3])
                np.add.at(counts, vertices, 1)
            vertex_colors = accumulated / np.maximum(counts[:, None], 1)
        vertex_colors = np.asarray(vertex_colors, dtype=np.float32)[:, :3]
        if not len(vertex_colors) or float(np.ptp(vertex_colors, axis=0).max()) < 4.0:
            return None

        material_records: dict[tuple[str, str], dict[str, str]] = {}
        for record in records:
            key = (record["family"], record["material"])
            current = material_records.get(key)
            if current is None or record["variant"] == "natural":
                material_records[key] = record
        palette_names: list[str] = []
        palette_colors: list[tuple[int, int, int]] = []
        for (family, material), record in sorted(material_records.items()):
            base = MATERIAL_TINTS.get((family, material))
            if base is None:
                family_colors = FAMILY_COLORS[family]
                base = family_colors[stable_seed(f"{family}:{material}") % len(family_colors)]
            palette_names.append(record["name"])
            palette_colors.append(base)
        if not palette_names:
            return None

        nearest_vertex = cKDTree(np.asarray(mesh.vertices, dtype=np.float32)).query(np.asarray(points, dtype=np.float32))[1]
        voxel_colors = vertex_colors[nearest_vertex]
        palette = np.asarray(palette_colors, dtype=np.float32)
        nearest_material = ((voxel_colors[:, None, :] - palette[None, :, :]) ** 2).sum(axis=2).argmin(axis=1)
        return [palette_names[int(index)] for index in nearest_material]
    except Exception:
        # Geometry-only checkpoints can omit texture/vertex colors. Callers
        # then keep the explicitly selected single-block fallback.
        return None


def mesh_to_object(input_image: Path, output: Path, triposr_dir: Path, triposr_python: Path, model_path: str, model_revision: str | None, device: str, size: int, block_type: str, palette_mode: str, chunk_size: int, mc_resolution: int) -> None:
    input_image = input_image.expanduser().resolve()
    if not input_image.is_file():
        raise RuntimeError(f"TripoSR input image not found: {input_image}")
    triposr_dir = triposr_dir.expanduser().resolve()
    run_script = triposr_dir / "run.py"
    if not run_script.is_file():
        raise RuntimeError(f"TripoSR run.py not found in {triposr_dir}")
    try:
        import numpy as np
        import trimesh
    except ImportError as exc:
        raise RuntimeError("photo-to-voxel decor requires the optional trimesh dependency in the assetgen environment") from exc
    triposr_python = triposr_python.expanduser().resolve()
    if not triposr_python.is_file():
        raise RuntimeError(f"TripoSR Python interpreter not found: {triposr_python}")
    # A machine-level PYTHONHOME can override a virtual environment's Python
    # home and make its stdlib/site-packages unusable. Do not leak it into the
    # isolated TripoSR worker (or a subprocess run with the current Python).
    worker_env = os.environ.copy()
    for variable in ("PYTHONHOME", "PYTHON_HOME", "PYTHONPATH"):
        worker_env.pop(variable, None)
    try:
        probe = subprocess.run(
            [str(triposr_python), "-c", "import torch; print('cuda' if torch.cuda.is_available() else 'cpu')"],
            check=True,
            capture_output=True,
            text=True,
            env=worker_env,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        detail = (getattr(exc, "stderr", None) or getattr(exc, "stdout", None) or str(exc)).strip()
        raise RuntimeError(f"cannot initialize TripoSR Python environment {triposr_python}: {detail}") from exc
    worker_device = probe.stdout.strip().splitlines()[-1] if probe.stdout.strip() else ""
    if worker_device not in ("cpu", "cuda"):
        raise RuntimeError(f"TripoSR device probe returned an unexpected result: {probe.stdout!r}")
    if device == "cuda" and worker_device != "cuda":
        raise RuntimeError(f"CUDA requested for TripoSR, but {triposr_python} has CPU-only PyTorch")
    resolved_device = worker_device if device == "auto" else device
    with tempfile.TemporaryDirectory(prefix="cubatarium-triposr-") as tmp:
        outdir = Path(tmp) / "triposr"
        try:
            subprocess.run(
                [str(triposr_python), str(run_script), str(input_image), "--output-dir", str(outdir), "--device", resolved_device, "--pretrained-model-name-or-path", model_path, "--model-save-format", "obj", "--chunk-size", str(chunk_size), "--mc-resolution", str(mc_resolution)],
                check=True,
                env=worker_env,
            )
        except (OSError, subprocess.CalledProcessError) as exc:
            detail = (getattr(exc, "stderr", None) or getattr(exc, "stdout", None) or str(exc)).strip()
            raise RuntimeError(f"TripoSR inference failed using {triposr_python}: {detail}") from exc
        mesh_path = outdir / "0" / "mesh.obj"
        if not mesh_path.is_file():
            raise RuntimeError("TripoSR did not produce mesh.obj")
        mesh = trimesh.load(mesh_path, force="mesh")
        if isinstance(mesh, trimesh.Scene):
            mesh = trimesh.util.concatenate(tuple(mesh.geometry.values()))
        extent = max(float(v) for v in mesh.extents)
        if extent <= 0:
            raise RuntimeError("TripoSR mesh has empty bounds")
        pitch = extent / max(4, min(32, size))
        voxels = mesh.voxelized(pitch).fill()
        points = voxels.points
        if len(points) > 8192:
            stride = math.ceil(len(points) / 8192)
            points = points[::stride]
        selected_records = family_records(load_catalog())
        block_types = voxel_palette_types(mesh, points, selected_records) if palette_mode == "material" else None
        resolved_palette = "nearest_material" if block_types is not None else "single_block"
        if block_types is None:
            block_types = [block_type] * len(points)
        minimum = points.min(axis=0)
        normalized = (points - minimum) / pitch
        coords = np.rint(normalized).astype(int)
        coordinate_extent = int(coords.max()) if coords.size else 0
        if coordinate_extent >= size:
            if size == 1:
                coords.fill(0)
            else:
                coords = np.rint(coords * ((size - 1) / coordinate_extent)).astype(int)
        unique_voxels: dict[tuple[int, int, int], str] = {}
        for coordinate, kind in zip(coords, block_types):
            unique_voxels.setdefault(tuple(int(value) for value in coordinate), kind)
        try:
            code_revision = subprocess.run(["git", "-C", str(triposr_dir), "rev-parse", "HEAD"], check=True, capture_output=True, text=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            code_revision = None
        object_doc = {"name": output.stem, "version": 2, "displayName": output.stem.replace("_", " ").title(), "source": {"mod": "cubatarium/assetgen", "model": "TripoSR", "model_revision": model_revision, "code_revision": code_revision, "device": resolved_device, "palette_mode": resolved_palette, "palette_material_count": len(set(unique_voxels.values())), "input_sha256": hashlib.sha256(input_image.read_bytes()).hexdigest(), "source_license": "REVIEW_REQUIRED", "license": "REVIEW_REQUIRED"}, "anchor": [0, 0, 0], "blocks": [{"dx": x, "dy": y, "dz": z, "type": block_type} for (x, y, z), block_type in unique_voxels.items()]}
        write_object(output, object_doc)


def preview_object(object_doc: dict[str, Any], output: Path) -> Path:
    voxels = object_doc.get("blocks") or []
    coords = [(int(v["dx"]), int(v["dy"]), int(v["dz"]), str(v["type"])) for v in voxels]
    if not coords:
        raise ValueError("cannot preview an object without voxels")
    material_records = {record["name"]: record for record in family_records(load_catalog())}
    projections = (
        ("front", lambda x, y, z: (x, y)),
        ("side", lambda x, y, z: (z, y)),
        ("top", lambda x, y, z: (x, z)),
    )
    tile = 12
    pad = 12
    panel_width = 320
    panels: list[Image.Image] = []
    for title, project in projections:
        points = [(project(x, y, z)[0], project(x, y, z)[1], kind) for x, y, z, kind in coords]
        min_x, max_x = min(p[0] for p in points), max(p[0] for p in points)
        min_y, max_y = min(p[1] for p in points), max(p[1] for p in points)
        width = max(1, max_x - min_x + 1)
        height = max(1, max_y - min_y + 1)
        scale = min(tile, max(1, (panel_width - pad * 2) // max(width, height)))
        panel = Image.new("RGBA", (panel_width, panel_width), (34, 38, 43, 255))
        draw = ImageDraw.Draw(panel)
        draw.text((pad, 6), title, font=ImageFont.load_default(), fill=(225, 230, 235, 255))
        ox = (panel_width - width * scale) // 2
        oy = (panel_width - height * scale) // 2 + 8
        for px, py, kind in points:
            record = material_records.get(kind)
            if record:
                family, material = record["family"], record["material"]
                base = MATERIAL_TINTS.get((family, material), FAMILY_COLORS[family][stable_seed(material) % len(FAMILY_COLORS[family])])
                brightness = 0.91 + (stable_seed(kind) % 19) / 100.0
                color = (*shade(base, brightness), 255)
            else:
                color_seed = stable_seed(kind)
                color = (72 + color_seed % 128, 72 + (color_seed >> 8) % 128, 72 + (color_seed >> 16) % 128, 255)
            x0 = ox + (px - min_x) * scale
            y0 = oy + (max_y - py) * scale
            draw.rectangle((x0, y0, x0 + scale - 1, y0 + scale - 1), fill=color, outline=(12, 14, 16, 255))
        panels.append(panel)
    combined = Image.new("RGBA", (panel_width * len(panels), panel_width), (24, 27, 30, 255))
    for index, panel in enumerate(panels):
        combined.alpha_composite(panel, (panel_width * index, 0))
    preview_path = output.with_name(output.stem + "_preview.png")
    combined.save(preview_path, optimize=True)
    return preview_path


def generate_parametric_decor(preset: str, output: Path, seed: int, material: str) -> None:
    catalog = load_catalog()
    records = family_records(catalog)
    if preset.startswith("tree:"):
        wood = safe_name(preset.split(":", 1)[1])
        if wood not in catalog["families"]["wood"]["materials"]:
            raise ValueError(f"unknown wood species {wood!r}")
        obj = make_tree(output.stem, wood, records, 6 if seed % 2 else 5)
    elif preset.startswith("boulder:"):
        stone = safe_name(preset.split(":", 1)[1])
        if stone not in catalog["families"]["rock"]["materials"]:
            raise ValueError(f"unknown rock material {stone!r}")
        obj = make_boulder(output.stem, "rock", stone, records, seed)
    elif preset == "flower_patch":
        obj = make_flower_patch(output.stem, records, seed)
    elif preset == "ruin":
        obj = make_ruin(output.stem, records, seed)
    else:
        raise ValueError("preset must be tree:<species>, boulder:<material>, flower_patch, or ruin")
    obj["source"]["seed"] = seed
    obj["source"]["material_parameter"] = material
    write_object(output, obj)


def install_model_snapshot(model_id: str, revision: str) -> Path:
    try:
        from huggingface_hub import hf_hub_download, snapshot_download
    except ImportError as exc:
        raise RuntimeError("model downloads require huggingface_hub; install requirements-assetgen-ml.txt") from exc
    destination = cached_model_dir(model_id)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if model_id.lower() == "stabilityai/triposr":
        # TripoSR loads only these two files. Downloading them through the Hub
        # cache and hard-linking into AssetGen avoids Hugging Face's large
        # local_dir copy/symlink step, which can stall on Windows without
        # developer-mode symlink privileges.
        destination.mkdir(parents=True, exist_ok=True)
        metadata_path = destination / "assetgen-model.json"
        try:
            existing_metadata = json.loads(metadata_path.read_text(encoding="utf-8-sig")) if metadata_path.is_file() else {}
        except (OSError, json.JSONDecodeError):
            existing_metadata = {}
        same_revision = existing_metadata.get("repo_id", "").lower() == model_id.lower() and existing_metadata.get("revision") == revision
        for filename in ("config.yaml", "model.ckpt"):
            source = Path(hf_hub_download(repo_id=model_id, filename=filename, revision=revision))
            target = destination / filename
            if same_revision and target.is_file() and target.stat().st_size == source.stat().st_size:
                continue
            temporary = target.with_name(target.name + ".assetgen-tmp")
            if temporary.exists():
                temporary.unlink()
            try:
                os.link(source, temporary)
            except OSError:
                shutil.copy2(source, temporary)
            os.replace(temporary, target)
    else:
        snapshot_download(repo_id=model_id, revision=revision, local_dir=str(destination))
    write_json(destination / "assetgen-model.json", {"repo_id": model_id, "revision": revision, "path": str(destination), "license_review_required": True})
    return destination


def command_decor(args: argparse.Namespace) -> int:
    if args.size < 1 or args.size > 32:
        raise ValueError("--size must be in [1, 32]")
    if args.backend == "triposr":
        if not args.input or not args.triposr_dir:
            raise RuntimeError("TripoSR decor requires --input and --triposr-dir")
        model_path = Path(args.model)
        model_metadata: dict[str, Any] = {}
        if model_path.exists():
            resolved_model = str(model_path.resolve())
            metadata_path = model_path / "assetgen-model.json" if model_path.is_dir() else None
            if metadata_path and metadata_path.is_file():
                model_metadata = json.loads(metadata_path.read_text(encoding="utf-8-sig"))
        else:
            cached_model = cached_model_dir(args.model)
            if cached_model.is_dir():
                model_metadata_path = cached_model / "assetgen-model.json"
                if model_metadata_path.is_file():
                    model_metadata = json.loads(model_metadata_path.read_text(encoding="utf-8-sig"))
                if args.revision and model_metadata.get("revision") not in (None, args.revision):
                    if not args.allow_download:
                        raise RuntimeError(f"cached TripoSR revision is {model_metadata['revision']}; install {args.model}@{args.revision} first")
                    cached_model = install_model_snapshot(args.model, args.revision)
                    model_metadata = json.loads((cached_model / "assetgen-model.json").read_text(encoding="utf-8-sig"))
                resolved_model = str(cached_model)
            elif args.allow_download:
                if not args.revision:
                    raise RuntimeError("TripoSR downloads require a pinned --revision; install the model first or supply --revision")
                cached_model = install_model_snapshot(args.model, args.revision)
                model_metadata = json.loads((cached_model / "assetgen-model.json").read_text(encoding="utf-8-sig"))
                resolved_model = str(cached_model)
            else:
                raise RuntimeError(f"TripoSR model not cached; run `assetgen.py models install {args.model} --revision <revision>` or set --allow-download")
        triposr_python = args.triposr_python or Path(os.environ.get("CUBATARIUM_TRIPOSR_PYTHON", sys.executable))
        mesh_to_object(args.input, args.output, args.triposr_dir, triposr_python, resolved_model, model_metadata.get("revision") or args.revision, args.device, args.size, args.block_type, args.palette_mode, args.chunk_size, args.mc_resolution)
    else:
        if not args.preset:
            raise RuntimeError("parametric decor requires --preset")
        generate_parametric_decor(args.preset, args.output, args.seed, args.material or "natural")
    object_doc = json.loads(args.output.read_text(encoding="utf-8-sig"))
    preview_path = preview_object(object_doc, args.output)
    print(json.dumps({"object": str(args.output.resolve()), "preview": str(preview_path.resolve())}, ensure_ascii=False))
    return 0


def command_models(args: argparse.Namespace) -> int:
    if args.models_action == "list":
        configured = cached_model_dir("__catalog__").parent
        paths = sorted(path for path in configured.iterdir() if path.is_dir()) if configured.exists() else []
        print("Local AssetGen models:")
        for path in paths:
            print(f"  {path}")
        if not paths:
            print("  none installed")
        return 0
    if not args.model_id or not args.revision:
        raise RuntimeError("models install requires <model-id> --revision <revision>")
    destination = install_model_snapshot(args.model_id, args.revision)
    print(f"installed {args.model_id}@{args.revision} to {destination}")
    return 0


def audit_pack(pack_dir: Path) -> tuple[list[str], dict[str, int]]:
    errors: list[str] = []
    try:
        pack_manifest = json.loads((pack_dir / "pack.json").read_text(encoding="utf-8-sig"))
        geology_manifest = json.loads((REPO / "resource_packs" / "cubatarium_geology_cc0" / "pack.json").read_text(encoding="utf-8-sig"))
        if int(pack_manifest.get("priority", 0)) <= int(geology_manifest.get("priority", 0)) + 10:
            errors.append("pack priority must exceed geology priority by more than one secondary selection rank (10) so original textures win overrides")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        errors.append(f"cannot read pack priority metadata: {exc}")
    blocks_dir = pack_dir / "blocks"
    textures_dir = pack_dir / "textures" / "blocks"
    objects_dir = pack_dir / "objects"
    blocks = [json.loads(path.read_text(encoding="utf-8-sig")) for path in sorted(blocks_dir.glob("*.json"))] if blocks_dir.exists() else []
    textures = sorted(textures_dir.glob("*.png")) if textures_dir.exists() else []
    unique_pixels: set[str] = set()
    duplicate_pixels: list[str] = []
    alpha_extrema: dict[str, tuple[int, int]] = {}
    for path in textures:
        try:
            image = Image.open(path).convert("RGBA")
        except Exception as exc:
            errors.append(f"invalid image {path.name}: {exc}")
            continue
        if image.size != (16, 16):
            errors.append(f"{path.name}: expected 16x16, got {image.size}")
        colors = image.getcolors(maxcolors=257)
        if colors is None or len(colors) > 32:
            errors.append(f"{path.name}: palette exceeds 32 RGBA colors")
        alpha_extrema[path.stem] = image.getchannel("A").getextrema()
        pixels = image.load()
        if any(pixels[0, y] != pixels[15, y] for y in range(16)) or any(pixels[x, 0] != pixels[x, 15] for x in range(16)):
            errors.append(f"{path.name}: texture edges are not seamless")
        digest = image_pixel_hash(image)
        if digest in unique_pixels:
            duplicate_pixels.append(path.name)
        unique_pixels.add(digest)
    if duplicate_pixels:
        errors.append(f"pixel-identical texture duplicates: {', '.join(duplicate_pixels[:10])}")
    name_to_block: dict[str, dict[str, Any]] = {}
    for block in blocks:
        name = block.get("name")
        if not isinstance(name, str) or not name:
            errors.append("block definition missing name")
            continue
        if name in name_to_block:
            errors.append(f"duplicate block name {name}")
        name_to_block[name] = block
        if len(block.get("textures", [])) not in (6, 12):
            errors.append(f"{name}: texture list must have 6 or 12 faces")
        for stem in block.get("textures", [])[:6]:
            if not (textures_dir / f"{stem}.png").is_file():
                errors.append(f"{name}: missing texture {stem}.png")
    ids: dict[int, str] = {}
    for block in blocks:
        block_id = block.get("id")
        if not isinstance(block_id, int) or block_id < 1 or block_id > 4095:
            errors.append(f"{block.get('name', '<unknown>')}: stable block id missing or out of range")
            continue
        if block_id in ids:
            errors.append(f"duplicate block id {block_id}: {ids[block_id]} and {block.get('name')}")
        ids[block_id] = str(block.get("name", "<unknown>"))
    active_block_ids = {
        int(block["id"])
        for pack_id in ("minetest_default_16", "cubatarium_geology_cc0")
        for block in load_block_files(pack_id)
        if isinstance(block.get("id"), int)
    }
    if active_block_ids.intersection(ids):
        errors.append(f"new pack stable block ids collide with default packs: {sorted(active_block_ids.intersection(ids))[:10]}")
    manifest_path = pack_dir / "texture_overrides.json"
    overrides = json.loads(manifest_path.read_text(encoding="utf-8-sig")) if manifest_path.exists() else {}
    minetest_blocks = load_block_files("minetest_default_16")
    geology_blocks = load_block_files("cubatarium_geology_cc0")
    missing_overrides = sorted(block["name"] for block in minetest_blocks + geology_blocks if block["name"] not in overrides)
    if missing_overrides:
        errors.append(f"missing default-pack face overrides for {len(missing_overrides)} blocks: {', '.join(missing_overrides[:8])}")
    for name, entries in overrides.items():
        if not isinstance(entries, list):
            errors.append(f"{name}: texture overrides must be a list")
            continue
        for entry in entries:
            stem = entry.get("stem") if isinstance(entry, dict) else None
            if not isinstance(stem, str) or not (textures_dir / f"{stem}.png").is_file():
                errors.append(f"{name}: override texture {stem!r} does not resolve")
    face_names = ("top", "bottom", "north", "south", "west", "east")
    replacement_maps = {
        "minetest_default_16": minetest_replacement_stems(),
        "cubatarium_geology_cc0": {path.stem: f"geo_{safe_name(path.stem)}" for path in (REPO / "resource_packs" / "cubatarium_geology_cc0" / "textures" / "blocks").glob("*.png")},
    }
    for pack_id, source_blocks in (("minetest_default_16", minetest_blocks), ("cubatarium_geology_cc0", geology_blocks)):
        replacement_map = replacement_maps[pack_id]
        for block in source_blocks:
            actual_pairs = {
                (entry.get("stem"), face)
                for entry in overrides.get(block["name"], [])
                if isinstance(entry, dict)
                for face in entry.get("faces", [])
            }
            missing_faces = []
            for index, stem in enumerate((block.get("textures") or [])[:6]):
                target_stem = replacement_map.get(stem)
                if target_stem is None:
                    errors.append(f"{pack_id}/{block['name']}: source texture {stem!r} has no replacement")
                elif (target_stem, face_names[index]) not in actual_pairs:
                    missing_faces.append(face_names[index])
            if missing_faces:
                errors.append(f"{block['name']}: missing face override(s) {', '.join(missing_faces)}")
    benchmark = load_catalog()["benchmark"]
    if len(blocks) < benchmark["block_definitions_minimum"]:
        errors.append(f"block count {len(blocks)} is below {benchmark['block_definitions_minimum']}")
    if len(textures) < benchmark["texture_files_minimum"]:
        errors.append(f"texture count {len(textures)} is below {benchmark['texture_files_minimum']}")
    minetest_count = len(list((REPO / "resource_packs" / "minetest_default_16" / "textures" / "blocks").glob("*.png")))
    provenance = json.loads((pack_dir / "asset_manifest.json").read_text(encoding="utf-8-sig")) if (pack_dir / "asset_manifest.json").is_file() else {}
    counts = provenance.get("counts", {})
    replaced = counts.get("minetest_textures_replaced", 0)
    if replaced < minetest_count:
        errors.append(f"Minetest texture replacement count {replaced} below source file count {minetest_count}")
    manifest_textures = provenance.get("textures", [])
    manifest_by_stem = {item.get("stem"): item for item in manifest_textures if isinstance(item, dict)}
    if len(manifest_textures) != len(textures):
        errors.append(f"asset manifest texture entries {len(manifest_textures)} do not match PNG count {len(textures)}")
    if len({item.get("stem") for item in manifest_textures if isinstance(item, dict)}) != len(manifest_textures):
        errors.append("asset manifest has duplicate texture stems")
    minetest_manifest_sources = {item.get("source", {}).get("reference_only") for item in manifest_textures if item.get("source", {}).get("kind") == "minetest_reference_only"}
    expected_minetest_sources = {path.stem for path in (REPO / "resource_packs" / "minetest_default_16" / "textures" / "blocks").glob("*.png")}
    if minetest_manifest_sources != expected_minetest_sources:
        errors.append(f"Minetest replacement source coverage differs: missing={sorted(expected_minetest_sources - minetest_manifest_sources)[:8]}, extra={sorted(minetest_manifest_sources - expected_minetest_sources)[:8]}")
    for stem, extrema in alpha_extrema.items():
        family = manifest_by_stem.get(stem, {}).get("family")
        has_transparency = extrema[0] < 255
        if family == "glass" and not has_transparency:
            errors.append(f"{stem}.png: glass texture must include alpha transparency")
        elif family != "glass" and has_transparency:
            errors.append(f"{stem}.png: non-glass texture unexpectedly includes transparency")
    object_count = len(list(objects_dir.glob("*.json"))) if objects_dir.exists() else 0
    block_names = set(name_to_block)
    for object_path in sorted(objects_dir.glob("*.json")) if objects_dir.exists() else []:
        try:
            obj = json.loads(object_path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"{object_path.name}: invalid object JSON ({exc})")
            continue
        voxels = obj.get("blocks")
        if not isinstance(voxels, list) or not voxels:
            errors.append(f"{object_path.name}: object has no voxel blocks")
            continue
        positions: set[tuple[int, int, int]] = set()
        for voxel in voxels:
            if not isinstance(voxel, dict) or voxel.get("type") not in block_names:
                errors.append(f"{object_path.name}: unresolved voxel block {voxel.get('type') if isinstance(voxel, dict) else None!r}")
                break
            try:
                position = (int(voxel["dx"]), int(voxel["dy"]), int(voxel["dz"]))
            except (KeyError, TypeError, ValueError):
                errors.append(f"{object_path.name}: voxel coordinate is missing or invalid")
                break
            if position in positions:
                errors.append(f"{object_path.name}: duplicate voxel coordinate {position}")
                break
            positions.add(position)
    return errors, {"blocks": len(blocks), "textures": len(textures), "unique_pixel_textures": len(unique_pixels), "minetest_textures": minetest_count, "minetest_overrides": len(overrides), "objects": object_count}


def command_audit(args: argparse.Namespace) -> int:
    if args.minecraft_version != MINECRAFT_REFERENCE["version"]:
        raise ValueError(f"no pinned benchmark is recorded for Minecraft {args.minecraft_version}; expected {MINECRAFT_REFERENCE['version']}")
    pack_dir = args.pack or PACK_DIR
    errors, counts = audit_pack(pack_dir)
    repo_minetest_textures = len(list((REPO / "resource_packs" / "minetest_default_16" / "textures" / "blocks").glob("*.png")))
    repo_minetest_blocks = len(load_block_files("minetest_default_16"))
    report = {
        "minecraft_reference": MINECRAFT_REFERENCE,
        "cubatarium": counts,
        "repo_minetest_reference": {"block_definitions": repo_minetest_blocks, "texture_pngs": repo_minetest_textures},
        "errors": errors,
    }
    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 1 if errors else 0


def command_benchmark(args: argparse.Namespace) -> int:
    if args.version != MINECRAFT_REFERENCE["version"]:
        raise ValueError(f"no pinned benchmark is recorded for Minecraft {args.version}; expected {MINECRAFT_REFERENCE['version']}")
    root = args.minecraft_root.resolve()
    textures = root / "assets" / "minecraft" / "textures" / "block"
    if not textures.is_dir():
        textures = root / "textures" / "block"
    if not textures.is_dir():
        raise RuntimeError(f"cannot find textures/block under {root}")
    texture_files = len(list(textures.rglob("*.png")))
    block_count: int | None = None
    for candidate in (root / "reports" / "blocks.json", root / "blocks.json", root / "data" / "minecraft" / "registries" / "block.json"):
        if candidate.is_file():
            parsed = json.loads(candidate.read_text(encoding="utf-8"))
            block_count = len(parsed) if isinstance(parsed, dict) else len(parsed) if isinstance(parsed, list) else None
            break
    print(json.dumps({"version": args.version, "block_definitions": block_count, "texture_files": texture_files, "texture_directory": str(textures), "block_reference_count": MINECRAFT_REFERENCE["block_definitions"], "texture_reference_count": MINECRAFT_REFERENCE["texture_files"]}, indent=2))
    return 0


def command_validate(args: argparse.Namespace) -> int:
    pack_dir = args.pack or PACK_DIR
    errors, counts = audit_pack(pack_dir)
    resource_pack_result = subprocess.run([sys.executable, "tools/validate_resource_pack.py", str(pack_dir.relative_to(REPO) if pack_dir.is_relative_to(REPO) else pack_dir)], cwd=REPO, check=False)
    if errors or resource_pack_result.returncode != 0:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(json.dumps({"status": "OK", **counts}, ensure_ascii=False))
    return 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Generate, audit, and validate Cubatarium voxel assets")
    sub = parser.add_subparsers(dest="command", required=True)
    build = sub.add_parser("build-pack", help="generate the full original CC0 block, texture, and decor pack")
    build.add_argument("--no-preview", action="store_true")
    build.add_argument("--no-worldgen", action="store_true")
    audit = sub.add_parser("audit", help="check generated pack against counts and current Minetest references")
    audit.add_argument("--minecraft-version", default="26.3")
    audit.add_argument("--pack", type=Path, default=PACK_DIR)
    benchmark = sub.add_parser("benchmark-minecraft", help="count extracted Minecraft assets without copying them")
    benchmark.add_argument("--minecraft-root", type=Path, required=True)
    benchmark.add_argument("--version", default="26.3")
    texture = sub.add_parser("texture", help="generate a single 16x16 texture")
    texture.add_argument("action", nargs="?", choices=("batch",), help="use 'batch' to build the texture catalog")
    texture.add_argument("--material")
    texture.add_argument("--family", choices=sorted(FAMILY_COLORS))
    texture.add_argument("--variant", default="natural")
    texture.add_argument("--prompt")
    texture.add_argument("--input", type=Path)
    texture.add_argument("--output", type=Path)
    texture.add_argument("--output-dir", type=Path)
    texture.add_argument("--catalog", type=Path)
    texture.add_argument("--backend", choices=("auto", "procedural", "diffusers"), default="auto")
    texture.add_argument("--model")
    texture.add_argument("--revision", help="pin a model revision when allowing runtime downloads")
    texture.add_argument("--allow-download", action="store_true", help="allow the model backend to access the network")
    texture.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    texture.add_argument("--resource-profile", choices=("offline", "cpu", "low-vram", "balanced", "quality"), default="balanced")
    texture.add_argument("--seed", type=int, default=0)
    texture.add_argument("--strength", type=float, default=0.55)
    texture.add_argument("--seam", type=int, default=2)
    texture.add_argument("--quiet", action="store_true", help="suppress the generation manifest on stdout")
    decor = sub.add_parser("decor", help="generate voxel decor as Cubatarium object JSON")
    decor.add_argument("--backend", choices=("parametric", "triposr"), default="parametric")
    decor.add_argument("--preset", help="tree:<species>, boulder:<material>, flower_patch, or ruin")
    decor.add_argument("--input", type=Path)
    decor.add_argument("--output", type=Path, required=True)
    decor.add_argument("--material")
    decor.add_argument("--seed", type=int, default=0)
    decor.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    decor.add_argument("--triposr-dir", type=Path)
    decor.add_argument("--triposr-python", type=Path, help="Python interpreter for the isolated TripoSR environment; defaults to CUBATARIUM_TRIPOSR_PYTHON or the current Python")
    decor.add_argument("--model", default="stabilityai/TripoSR")
    decor.add_argument("--allow-download", action="store_true", help="download the pinned TripoSR revision to the local cache if needed")
    decor.add_argument("--revision", help="pin a TripoSR model revision when downloading")
    decor.add_argument("--chunk-size", type=int, default=8192, help="TripoSR renderer chunk size; lower values reduce peak memory")
    decor.add_argument("--mc-resolution", type=int, default=256, help="TripoSR mesh extraction grid resolution")
    decor.add_argument("--size", type=int, default=16)
    decor.add_argument("--block-type", default="cu_masonry_limestone_block")
    decor.add_argument("--palette-mode", choices=("material", "single"), default="material", help="map reconstructed colors to catalog materials, or use only --block-type")
    models = sub.add_parser("models", help="list or explicitly download optional local models")
    models_sub = models.add_subparsers(dest="models_action", required=True)
    models_sub.add_parser("list")
    install = models_sub.add_parser("install")
    install.add_argument("model_id")
    install.add_argument("--revision", required=True)
    validate = sub.add_parser("validate", help="validate the asset pack and 16x16 texture invariants")
    validate.add_argument("--pack", type=Path, default=PACK_DIR)
    preview = sub.add_parser("preview", help="create contact sheets or a voxel object preview")
    preview_target = preview.add_mutually_exclusive_group(required=True)
    preview_target.add_argument("--textures", type=Path)
    preview_target.add_argument("--object", type=Path)
    preview_target.add_argument("--compare-reference", choices=("minetest_default_16", "cubatarium_geology_cc0"))
    preview.add_argument("--output", type=Path)
    preview.add_argument("--pack", type=Path, default=PACK_DIR)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "build-pack":
            print(json.dumps(build_pack(args.no_preview, not args.no_worldgen), ensure_ascii=False))
            return 0
        if args.command == "audit":
            return command_audit(args)
        if args.command == "benchmark-minecraft":
            return command_benchmark(args)
        if args.command == "texture":
            if not 0 <= args.seam <= 8:
                raise ValueError("--seam must be in [0, 8]")
            if not 0.0 <= args.strength <= 1.0:
                raise ValueError("--strength must be in [0, 1]")
            if args.action == "batch":
                return texture_batch_command(args)
            if args.output is None:
                raise ValueError("single texture generation requires --output")
            return generate_texture_command(args)
        if args.command == "decor":
            return command_decor(args)
        if args.command == "models":
            return command_models(args)
        if args.command == "validate":
            return command_validate(args)
        if args.command == "preview":
            return command_preview(args)
        parser.error("unknown command")
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"assetgen: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
