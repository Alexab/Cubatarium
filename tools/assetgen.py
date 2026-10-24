#!/usr/bin/env python3
"""Build and validate Cubatarium's original voxel asset library.

The default backend is deterministic and offline. Optional Diffusers and
TripoSR integrations are imported only when explicitly requested.
"""

from __future__ import annotations

import argparse
import copy
from functools import lru_cache
import hashlib
import json
import math
import os
import random
import re
import shutil
import statistics
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
    from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageOps, ImageStat
except ImportError as exc:  # pragma: no cover - covered by CLI error path
    raise SystemExit("assetgen needs Pillow; install requirements-assetgen.txt") from exc


REPO = Path(__file__).resolve().parents[1]
PACK_ID = "cubatarium_worldgen_cc0"
PACK_DIR = REPO / "resource_packs" / PACK_ID
DEFAULT_TEXTURE_SIZE = 16
MIN_REPLACEMENT_LEAF_TRANSPARENCY = 0.40
MIN_CATALOG_LEAF_TRANSPARENCY = 0.38
PHOTO64_PACK_ID = "cubatarium_worldgen_photo64_cc0"
PHOTO64_PACK_DIR = REPO / "resource_packs" / PHOTO64_PACK_ID
STAGING_DIR = REPO / "assetgen_output" / "staging"
CATALOG_PATH = REPO / "tools" / "asset_catalog.yaml"
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
        ("asset_tree_aspen", ["forest", "plains", "cold_steppe"], 32, 1),
        ("asset_tree_cedar", ["mountains", "foothills"], 34, 2),
        ("asset_tree_maple", ["forest", "hills"], 30, 2),
        ("asset_tree_poplar", ["cold_steppe", "tundra"], 36, 1),
        ("asset_tree_spruce", ["cold_steppe", "tundra", "mountains"], 30, 2),
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
        ("asset_coral_fan", ["ocean"], 72, 1),
        ("asset_stalagmite_cluster", ["dripstone_caves", "deep_dark"], 84, 1),
        ("asset_crystal_cluster", ["deep_dark", "mountains"], 96, 1),
        ("asset_mossy_ruin_piece", ["forest", "hills", "scrubland"], 110, 1),
    ],
    "structures": [
        ("asset_cairn", ["hills", "mountains", "tundra"], 6200, 1),
        ("asset_woodland_shrine", ["forest", "plains"], 7600, 1),
    ],
}
FACE_NAMES = ("front", "right", "back", "left", "top", "bottom")

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
    ("soil", "red_earth"): (146, 75, 55), ("soil", "pale_sand"): (214, 207, 159),
    ("soil", "desert_sand"): (211, 169, 103),
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
    ("wood", "aspen"): (202, 188, 158),
    ("wood", "spruce"): (121, 88, 54),
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
    ("fabric", "umber"): (112, 77, 52), ("fabric", "slate"): (131, 131, 132),
    ("fabric", "black"): (24, 26, 30),
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
    ("crystal", "emerald"): (122, 190, 190),
    ("crystal", "malachite"): (54, 135, 94),
    # Plants, flowers and fungi
    ("organic", "moss"): (81, 118, 54), ("organic", "lichen"): (142, 148, 83),
    ("organic", "fern"): (83, 133, 58), ("organic", "reed"): (127, 146, 72),
    ("organic", "mushroom"): (153, 114, 83), ("organic", "coral"): (184, 93, 91),
    ("organic", "kelp"): (53, 116, 93), ("organic", "blossom"): (188, 124, 144),
    ("organic", "leaf"): (56, 87, 40),
    ("organic", "flower_white"): (220, 218, 197), ("organic", "flower_yellow"): (218, 185, 59),
    ("organic", "flower_red"): (183, 64, 67), ("organic", "flower_pink"): (207, 117, 148),
    ("organic", "flower_blue"): (74, 118, 183), ("organic", "flower_purple"): (132, 89, 163),
    ("organic", "flower_orange"): (218, 117, 51), ("organic", "mushroom_red"): (169, 65, 65),
    ("organic", "mushroom_gold"): (203, 163, 64), ("organic", "mushroom_blue"): (73, 118, 149),
    ("organic", "mushroom_pale"): (192, 184, 163), ("organic", "needle"): (53, 91, 46),
    ("organic", "mushroom_stem"): (204, 194, 171),
}

# Bark colors describe the finished bark surface rather than darkening the
# timber species color with one shared multiplier. This keeps pale, smooth
# species readable beside the rougher brown species at the 16px world scale.
BARK_BASE_TINTS = {
    "acacia": (137, 118, 88), "ash": (143, 137, 122),
    "beech": (183, 177, 166), "cedar": (133, 83, 62),
    "jungle": (130, 92, 60), "larch": (152, 112, 76),
    "mangrove": (115, 83, 66), "maple": (144, 108, 78),
    "oak": (139, 108, 78), "pine": (86, 76, 66),
    "poplar": (178, 171, 157), "redwood": (158, 81, 56),
    "spruce": (80, 81, 78),
    "walnut": (105, 79, 60), "willow": (147, 132, 102),
}

# Face-scale crack plates vary by species. Most bark keeps compact flakes;
# conifers use short, vertically elongated plates so the texture reads as
# bark furrows without becoming a continuous sinusoidal wood grain.
BARK_CELL_COUNTS = {
    "acacia": (5, 5), "ash": (5, 5), "aspen": (6, 5), "beech": (6, 6),
    "cedar": (7, 4), "jungle": (5, 5), "larch": (7, 4),
    "mangrove": (4, 5), "maple": (5, 5), "oak": (4, 4),
    "pine": (8, 4), "poplar": (6, 5), "redwood": (6, 4), "spruce": (7, 4),
    "walnut": (5, 5), "willow": (5, 4),
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

SAND_MATERIALS = frozenset(("pale_sand", "desert_sand", "silt"))

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


@lru_cache(maxsize=8)
def load_catalog(path: Path = CATALOG_PATH) -> dict[str, Any]:
    doc = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    if doc.get("schema_version") != 2:
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


def _wrapped_noise(seed: int, size: int) -> tuple[list[float], list[float], list[float]]:
    """Multi-scale periodic noise; coordinates wrap at the tile boundary."""
    return (
        tile_noise(seed ^ 0x5A17, size, size, max(2, size // 8)),
        tile_noise(seed ^ 0x1B83, size, size, max(2, size // 4)),
        tile_noise(seed ^ 0xC2B2, size, size, max(2, size // 2)),
    )


def _wood_endgrain_texture(
    base: tuple[int, int, int], variant: str, seed: int, size: int,
) -> Image.Image:
    """Draw a tileable cut-wood face with growth rings, pith, and fine rays."""
    coarse, medium, fine = _wrapped_noise(seed, size)
    grain = tile_noise(seed ^ 0x454E4447, size, size, size)
    phase_offset = (stable_seed(variant) % 997) / 997.0 * math.tau
    dark_variant = any(token in variant for token in ("dark", "aged", "weathered"))
    pale_variant = "pale" in variant
    tint = 0.84 if dark_variant else 1.08 if pale_variant else 1.0
    wood = shade(base, tint)
    palette = (0.72, 0.83, 0.92, 1.0, 1.08, 1.16)
    ray_angles = tuple(
        (phase_offset + index * math.tau / 5.0) % math.tau
        for index in range(5)
    )
    pixels: list[tuple[int, int, int, int]] = []
    ring_spacing = max(2.2, size * 0.17)
    for y in range(size):
        for x in range(size):
            i = y * size + x
            dx = x + 0.5 - size / 2.0
            dy = y + 0.5 - size / 2.0
            # Distances wrap around the tile. Matching distances at opposite
            # edges keeps the annual rings continuous when adjacent blocks meet.
            dx -= round(dx / size) * size
            dy -= round(dy / size) * size
            radius = math.hypot(dx, dy)
            angle = math.atan2(dy, dx)
            # Let growth rings wander with the wrapped material fields. The
            # old low-amplitude distortion kept every ring almost perfectly
            # circular, which made log ends look stamped and mechanical.
            distortion = (
                coarse[i] * 0.52 + medium[i] * 0.28
                + fine[i] * 0.12 + grain[i] * 0.08
            )
            local_spacing = ring_spacing * (1.0 + medium[i] * 0.045)
            ring = math.cos(
                (radius + distortion) * math.tau / local_spacing
                + phase_offset * 0.08
            )
            tone = 0.5 + 0.37 * ring + 0.08 * fine[i] + 0.05 * grain[i]
            palette_index = max(0, min(len(palette) - 1, round(tone * (len(palette) - 1))))
            factor = palette[palette_index]

            # A handful of subdued radial rays makes the face read as cut
            # timber instead of stone; they taper out before reaching the edge.
            angular_distance = min(
                abs((angle - ray + math.pi) % math.tau - math.pi)
                for ray in ray_angles
            )
            if 1.5 < radius < size * 0.48 and angular_distance < 0.055:
                factor *= 0.86
            if radius < 0.9:
                factor *= 0.72

            rgb = shade(wood, factor)
            pixels.append((*rgb, 255))
    image = Image.new("RGBA", (size, size))
    image.putdata(pixels)
    return image


def _wood_surface_texture(
    base: tuple[int, int, int], variant: str, seed: int, size: int, role: str,
) -> Image.Image:
    """Draw long-grain wood faces with irregular fibers and an occasional knot."""
    coarse, medium, fine = _wrapped_noise(seed, size)
    grain = tile_noise(seed ^ 0x574F4F44, size, size, size)
    knot_seed = stable_seed(f"wood-knot:{seed}")
    knot_x = (knot_seed % size) + 0.5
    knot_y = ((knot_seed // max(1, size)) % size) + 0.5
    phase_offset = (stable_seed(variant) % 997) / 997.0 * math.tau
    tint = (
        0.78 if any(token in variant for token in ("dark", "aged", "weathered"))
        else 1.10 if "pale" in variant else 1.0
    )
    if role == "bottom":
        tint *= 0.92
    wood = shade(base, tint)
    pixels: list[tuple[int, int, int, int]] = []
    for y in range(size):
        for x in range(size):
            i = y * size + x
            flow = coarse[i] * 0.48 + medium[i] * 0.36 + fine[i] * 0.20
            phase = (
                (y + flow * max(1, size // 3)) * math.tau * 2.35 / size
                + math.sin((x + phase_offset) * math.tau / size) * 0.34
                + phase_offset
            )
            fibers = math.cos(phase) * 0.24 + math.cos(phase * 1.9 + fine[i]) * 0.09
            tone = (
                1.0 + fibers + coarse[i] * 0.10 + medium[i] * 0.12
                + fine[i] * 0.11 + grain[i] * 0.08
            )

            dx = abs((x + 0.5) - knot_x)
            dy = abs((y + 0.5) - knot_y)
            dx = min(dx, size - dx)
            dy = min(dy, size - dy)
            knot_radius = math.sqrt(dx * dx + dy * dy * 0.52)
            if 0.55 < knot_radius < 1.55:
                tone *= 0.76
            elif knot_radius <= 0.55:
                tone *= 0.94

            pixel_tone = tone + fine[i] * 0.025 + grain[i] * 0.025
            rgb = shade(wood, max(0.64, min(1.32, pixel_tone)))
            pixels.append((*rgb, 255))
    image = Image.new("RGBA", (size, size))
    image.putdata(pixels)
    return image


def _wrapped_cellular_plates(
    seed: int, size: int, species: str = "",
) -> tuple[list[float], list[float]]:
    """Return seamless irregular plate tones and crack strengths for bark.

    A jittered feature grid keeps the small 16px tile covered evenly. Distances
    wrap around the tile in both axes, so plate shapes remain continuous when
    tiled. Cellular regions avoid long, parallel waves while retaining readable
    bark flakes and fissures at voxel scale.
    """
    # Species profiles change flake scale while keeping the footprint
    # irregular and short enough to avoid stripe-like trunk grain.
    cells_x, cells_y = BARK_CELL_COUNTS.get(species, (6, 4))
    cells_x = max(3, round(cells_x * size / 16))
    cells_y = max(3, round(cells_y * size / 16))
    spacing_x = size / cells_x
    spacing_y = size / cells_y
    rng = random.Random(seed ^ 0xB4A2C311)
    sites: list[tuple[float, float, float]] = []
    for gy in range(cells_y):
        for gx in range(cells_x):
            jitter_x = rng.uniform(-0.38, 0.38)
            jitter_y = rng.uniform(-0.38, 0.38)
            site_x = ((gx + 0.5 + jitter_x) * spacing_x) % size
            site_y = ((gy + 0.5 + jitter_y) * spacing_y) % size
            tone = rng.uniform(-1.0, 1.0)
            sites.append((site_x, site_y, tone))

    plate_tones: list[float] = []
    crack_strengths: list[float] = []
    for y in range(size):
        for x in range(size):
            px, py = x + 0.5, y + 0.5
            nearest = float("inf")
            second_nearest = float("inf")
            nearest_tone = 0.0
            for site_x, site_y, tone in sites:
                dx = abs(px - site_x)
                dy = abs(py - site_y)
                dx = min(dx, size - dx)
                dy = min(dy, size - dy)
                normalized_x = dx / spacing_x
                normalized_y = dy / spacing_y
                distance_squared = normalized_x * normalized_x + normalized_y * normalized_y
                if distance_squared < nearest:
                    second_nearest = nearest
                    nearest = distance_squared
                    nearest_tone = tone
                elif distance_squared < second_nearest:
                    second_nearest = distance_squared
            edge_gap = math.sqrt(second_nearest) - math.sqrt(nearest)
            crack_t = max(0.0, min(1.0, (edge_gap - 0.025) / 0.20))
            crack_t = crack_t * crack_t * (3.0 - 2.0 * crack_t)
            plate_tones.append(nearest_tone)
            crack_strengths.append(1.0 - crack_t)
    return plate_tones, crack_strengths


def _apply_birch_bark_marks(
    image: Image.Image, seed: int, species: str = "birch",
) -> Image.Image:
    """Add crisp, wrapped lenticels to pale birch or aspen bark."""
    result = image.convert("RGBA")
    size = result.width
    species_salt = 0xA5E3 if species == "aspen" else 0xB17C4A2D
    rng = random.Random(seed ^ species_salt)
    pixels = result.load()
    colors = (
        ((54, 53, 48, 255), (77, 75, 68, 255), (108, 103, 91, 255))
        if species == "aspen" else
        ((42, 43, 41, 255), (66, 67, 63, 255), (89, 89, 83, 255))
    )
    # Aspen has fewer, softer scars than birch. Both patterns stay mostly
    # horizontal so a single texel reads as bark instead of a dark wood grain.
    band_count = max(5, round(size * (0.56 if species == "aspen" else 0.75)))
    for band in range(band_count):
        y = (round((band + 0.5) * size / band_count)
             + rng.choice((-1, 0, 0, 1))) % size
        marks_in_band = 1 if rng.random() < (0.76 if species == "aspen" else 0.62) else 2
        for _ in range(marks_in_band):
            start_x = rng.randrange(size)
            length = rng.randint(
                max(2, size // 9),
                max(3, size // (5 if species == "aspen" else 4)),
            )
            color = rng.choice(colors)
            slope = rng.choice((-1, 0, 0, 0, 1))
            for offset in range(length):
                x = (start_x + offset) % size
                yy = (y + round(offset * slope * 0.18)) % size
                pixels[x, yy] = color
            if rng.random() < 0.28:
                split_x = (start_x + rng.randrange(length)) % size
                pixels[split_x, (y + 1) % size] = colors[2]
    return result


def _apply_conifer_bark_furrows(
    image: Image.Image, seed: int, species: str,
) -> Image.Image:
    """Add short, broken bark fissures without wavy grain bands."""
    result = image.convert("RGBA")
    size = result.width
    rng = random.Random(seed ^ stable_seed(f"conifer-furrows:{species}"))
    pixels = result.load()
    original = [pixels[x, y] for y in range(size) for x in range(size)]
    weights = (1, 2, 3, 2, 1)
    for y in range(size):
        for x in range(size):
            i = y * size + x
            blurred = [
                sum(original[((y + offset) % size) * size + x][channel] * weights[offset + 2]
                    for offset in range(-2, 3)) / sum(weights)
                for channel in range(3)
            ]
            color = original[i]
            pixels[x, y] = (
                *(round(color[channel] * 0.42 + blurred[channel] * 0.58)
                  for channel in range(3)),
                color[3],
            )

    fissure_count = max(6, round(size * 0.42))
    for _ in range(fissure_count):
        x = rng.randrange(size)
        y = rng.randrange(size)
        length = rng.randint(max(3, size // 4), max(4, size * 3 // 8))
        drift = rng.choice((-1, 0, 0, 0, 1))
        darken = rng.uniform(0.50, 0.70)
        highlight = rng.random() < 0.55
        for step in range(length):
            # Short runs keep the bark direction readable, while gaps and
            # slight offsets prevent continuous vertical or sinusoidal bands.
            if step > 0 and rng.random() < 0.08:
                continue
            px = (x + round(step * drift * 0.18)) % size
            py = (y + step) % size
            red, green, blue, alpha = pixels[px, py]
            factor = 1.06 if highlight else darken
            pixels[px, py] = (
                max(0, min(255, round(red * factor))),
                max(0, min(255, round(green * factor))),
                max(0, min(255, round(blue * factor))),
                alpha,
            )
    return result


def _apply_soft_bark_marks(
    image: Image.Image, seed: int, species: str,
) -> Image.Image:
    """Add sparse horizontal lenticels to smooth beech and poplar bark."""
    result = image.convert("RGBA")
    size = result.width
    beech = species == "beech"
    rng = random.Random(seed ^ (0xBE3C if beech else 0xA510))
    pixels = result.load()
    dark = (91, 91, 85, 255) if beech else (79, 78, 72, 255)
    mid = (119, 116, 106, 255) if beech else (103, 101, 92, 255)
    mark_count = max(5, round(size * (0.40 if beech else 0.52)))
    for _ in range(mark_count):
        x, y = rng.randrange(size), rng.randrange(size)
        length = rng.randint(1, max(2, size // (6 if beech else 5)))
        color = dark if rng.random() < 0.68 else mid
        for offset in range(length):
            xx = (x + offset) % size
            yy = (y + (1 if offset == length - 1 and rng.random() < 0.18 else 0)) % size
            pixels[xx, yy] = color
    return result


def _apply_needle_foliage(
    image: Image.Image, seed: int, variant: str = "fresh"
) -> Image.Image:
    """Build short, directional conifer needles over a wrapped green base."""
    result = image.convert("RGBA")
    size = result.width
    unique_size = max(2, size - 1)
    rng = random.Random(seed ^ 0x4E454544)
    pixels = result.load()
    variant = variant.lower()
    shades = (
        ((57, 77, 39, 255), (80, 101, 48, 255), (111, 126, 60, 255), (39, 62, 36, 255))
        if variant in ("dry", "weathered") else
        ((30, 55, 32, 255), (43, 77, 39, 255), (66, 99, 48, 255), (87, 116, 57, 255))
        if variant in ("dark", "dense") else
        ((38, 72, 36, 255), (54, 92, 43, 255), (77, 112, 51, 255), (103, 132, 62, 255))
        if variant in ("young", "fresh", "bright", "mature") else
        ((34, 65, 34, 255), (48, 84, 41, 255), (72, 106, 49, 255), (94, 124, 59, 255))
    )

    # Keep subtle tile value variation, but remove the broad, leaf-like color
    # patches that made spruce crowns read as generic deciduous foliage.
    for y in range(size):
        for x in range(size):
            red, green, blue, _ = pixels[x % unique_size, y % unique_size]
            value = (red * 0.25 + green * 0.62 + blue * 0.13) / 255.0
            palette_index = max(0, min(2, round(value * 2.0)))
            pixels[x, y] = (*shades[palette_index][:3], 0)

    def put_wrapped(x: int, y: int, color: tuple[int, int, int, int]) -> None:
        pixels[x % unique_size, y % unique_size] = color

    # Pine and spruce needles grow in short fans off a twig. Several offset
    # fans make a useful tile for the six faces of voxel leaf blocks: the
    # direction stays legible at 16 px, while the cutout mask supplies canopy
    # gaps between adjacent blocks. Sparse and dense variants change the
    # number of fans as well as their alpha coverage later in the pipeline.
    density = 0.70 if variant == "sparse" else 1.35 if variant == "dense" else 1.0
    sprig_count = max(3, round((size / 2.5) * density))
    directions = (
        (-1, -1), (0, -1), (1, -1), (-1, 0),
        (1, 0), (-1, 1), (0, 1), (1, 1),
    )
    for _ in range(sprig_count):
        x0, y0 = rng.randrange(unique_size), rng.randrange(unique_size)
        dx, dy = rng.choice(directions)
        length = rng.randint(max(3, round(size * 0.24)),
                             max(4, round(size * 0.52)))
        twig = shades[rng.choice((0, 0, 1))]
        for step in range(length):
            x, y = x0 + dx * step, y0 + dy * step
            put_wrapped(x, y, twig)
            if step % 2 != 0:
                continue
            for side in (-1, 1):
                needle_length = rng.choice((1, 1, 2))
                needle = shades[rng.choice((1, 2, 2, 3))]
                for reach in range(1, needle_length + 1):
                    # A slight forward lean prevents the fans from forming
                    # identical perpendicular rungs along every twig.
                    along = step + (side if reach > 1 else 0)
                    across = side * reach
                    put_wrapped(x0 + dx * along - dy * across,
                                y0 + dy * along + dx * across, needle)
            if rng.random() < 0.20:
                put_wrapped(x + dx, y + dy,
                            shades[rng.choice((2, 3))])
    return result


def fit_alpha_coverage(image: Image.Image, coverage: float, seed: int) -> Image.Image:
    """Fit a new sprite to a reference alpha percentage without collapsing it.

    Only the scalar coverage is taken from the reference. Thinning preserves
    the generated silhouette row by row and follows adjacent rows to retain
    stems and blades. Expansion grows from that silhouette's edge.
    """
    rgba = image.convert("RGBA")
    width, height = rgba.size
    total = width * height
    target = max(1, min(total, round(total * max(0.0, min(1.0, coverage)))))
    pixels = list(rgba.getdata())
    foreground = [pixel[3] >= 128 for pixel in pixels]
    current = sum(foreground)
    if current == target or current == 0 or current == total:
        return rgba

    rng = random.Random(seed ^ 0x414C5048)
    keep = {index for index, selected in enumerate(foreground) if selected}
    nearest_foreground = list(range(total))

    if target > current:
        # Grow the silhouette inward from its edge. Earlier farthest-first
        # growth created detached pixels on large fire and vine masks.
        distance = [-1] * total
        queue: list[int] = []
        for index, selected in enumerate(foreground):
            if selected:
                distance[index] = 0
                queue.append(index)
        cursor = 0
        while cursor < len(queue):
            index = queue[cursor]
            cursor += 1
            x, y = index % width, index // width
            for dy in (-1, 0, 1):
                ny = y + dy
                if not 0 <= ny < height:
                    continue
                for dx in (-1, 0, 1):
                    if dx == 0 and dy == 0:
                        continue
                    nx = x + dx
                    if not 0 <= nx < width:
                        continue
                    neighbor = ny * width + nx
                    if distance[neighbor] < 0:
                        distance[neighbor] = distance[index] + 1
                        nearest_foreground[neighbor] = nearest_foreground[index]
                        queue.append(neighbor)
        candidates = [index for index, selected in enumerate(foreground) if not selected]
        candidates.sort(key=lambda index: (distance[index], rng.random()))
        keep.update(candidates[:target - current])
    else:
        rows = [
            [x for x in range(width) if foreground[y * width + x]]
            for y in range(height)
        ]
        active_rows = [y for y, row in enumerate(rows) if row]
        quotas = [0] * height

        if target < len(active_rows):
            ranked_rows = sorted(
                active_rows, key=lambda y: (len(rows[y]), rng.random()), reverse=True
            )
            for y in ranked_rows[:target]:
                quotas[y] = 1
        else:
            for y in active_rows:
                quotas[y] = 1
            remaining = target - len(active_rows)
            capacities = [max(0, len(row) - 1) for row in rows]
            total_capacity = sum(capacities)
            if remaining and total_capacity:
                exact = [remaining * capacity / total_capacity for capacity in capacities]
                for y, amount in enumerate(exact):
                    quotas[y] += int(amount)
                remaining = target - sum(quotas)
                remainder_order = sorted(
                    range(height),
                    key=lambda y: (exact[y] - int(exact[y]), rng.random()),
                    reverse=True,
                )
                for y in remainder_order:
                    if remaining <= 0:
                        break
                    if quotas[y] < len(rows[y]):
                        quotas[y] += 1
                        remaining -= 1

        keep.clear()
        previous_x: list[int] = []
        for y in reversed(range(height)):
            row = rows[y]
            count = quotas[y]
            if count <= 0:
                previous_x = []
                continue
            available = set(row)
            selected_x: list[int] = []
            references = previous_x
            if len(references) > count:
                if count == 1:
                    references = [references[len(references) // 2]]
                else:
                    references = [
                        references[round(i * (len(references) - 1) / (count - 1))]
                        for i in range(count)
                    ]
            for reference_x in references:
                if not available or len(selected_x) >= count:
                    break
                chosen_x = min(available, key=lambda x: (abs(x - reference_x), rng.random()))
                selected_x.append(chosen_x)
                available.remove(chosen_x)
            missing = count - len(selected_x)
            if missing:
                remaining_x = sorted(available)
                available_ranks = set(range(len(remaining_x)))
                for sample in range(missing):
                    ideal = (sample + 0.5) * len(remaining_x) / missing - 0.5
                    rank = min(
                        available_ranks,
                        key=lambda candidate: (abs(candidate - ideal), rng.random()),
                    )
                    available_ranks.remove(rank)
                    selected_x.append(remaining_x[rank])
            for x in selected_x:
                keep.add(y * width + x)
            previous_x = sorted(selected_x)

    adjusted: list[tuple[int, int, int, int]] = []
    for index, pixel in enumerate(pixels):
        if index in keep:
            color_index = nearest_foreground[index] if not foreground[index] else index
            source_pixel = pixels[color_index]
            adjusted.append((source_pixel[0], source_pixel[1], source_pixel[2], 255))
        else:
            adjusted.append((pixel[0], pixel[1], pixel[2], 0))
    result = Image.new("RGBA", (width, height))
    result.putdata(adjusted)
    return result


def apply_needle_cutout(image: Image.Image, seed: int,
                        transparent_ratio: float,
                        variant: str = "fresh") -> Image.Image:
    """Fit alpha to the authored sprig silhouette, keeping gaps between fans."""
    result = _apply_needle_foliage(image.convert("RGBA"), seed, variant)
    size = result.width
    unique_size = max(2, size - 1)
    alpha = result.getchannel("A")
    source = alpha.load()
    transparent_ratio = max(0.05, min(0.75, transparent_ratio))
    target_holes = round(size * size * transparent_ratio)

    # Pick holes on the unique (size - 1)² tile and weight duplicated border
    # texels. This keeps both the alpha silhouette and its exact coverage
    # seamless. Distances preserve the largest natural gaps and grow new gaps
    # from existing ones when a denser profile needs more transparency.
    base_holes = {
        (x, y)
        for y in range(unique_size)
        for x in range(unique_size)
        if source[x, y] < 128
    }
    base_opaque = {
        (x, y)
        for y in range(unique_size)
        for x in range(unique_size)
        if (x, y) not in base_holes
    }
    rng = random.Random(seed ^ 0x4E454544)
    ranked_cells: list[tuple[float, int, int, int]] = []
    for y in range(unique_size):
        for x in range(unique_size):
            is_hole = (x, y) in base_holes
            opposite = list(base_opaque if is_hole else base_holes)
            if opposite:
                distances = []
                for ox, oy in opposite:
                    dx = abs(x - ox)
                    dy = abs(y - oy)
                    dx = min(dx, unique_size - dx)
                    dy = min(dy, unique_size - dy)
                    distances.append(math.sqrt(dx * dx + dy * dy))
                distance = min(distances)
            else:
                distance = float(unique_size)
            # Existing holes rank ahead of foliage. If more holes are needed,
            # expand from the nearest opening instead of punching new islands.
            score = -distance if is_hole else distance
            score += rng.random() * 0.001
            weight = (2 if x == 0 else 1) * (2 if y == 0 else 1)
            ranked_cells.append((score, y, x, weight))
    ranked_cells.sort(key=lambda item: (item[0], item[1], item[2]))

    best: list[tuple[int, tuple[tuple[int, int], ...]] | None] = [
        None
    ] * (target_holes + 1)
    best[0] = (0, ())
    for rank, (_, y, x, weight) in enumerate(ranked_cells):
        for count in range(target_holes, weight - 1, -1):
            previous = best[count - weight]
            if previous is None:
                continue
            candidate = (previous[0] + rank, previous[1] + ((x, y),))
            if best[count] is None or candidate[0] < best[count][0]:
                best[count] = candidate
    selected_holes = set(best[target_holes][1]) if best[target_holes] else set()
    pixels = result.load()
    for y in range(size):
        for x in range(size):
            r, g, b, _ = pixels[x, y]
            hole = (x % unique_size, y % unique_size) in selected_holes
            pixels[x, y] = (r, g, b, 0 if hole else 255)

    alpha = result.getchannel("A")
    for palette_size in range(32, 15, -1):
        rgb = result.convert("RGB").quantize(
            colors=palette_size, method=Image.Quantize.MEDIANCUT
        ).convert("RGB")
        rgb.putalpha(alpha)
        palette = rgb.getcolors(maxcolors=49)
        if palette is not None and len(palette) <= 48:
            return rgb
    rgb = result.convert("RGB").quantize(
        colors=16, method=Image.Quantize.MEDIANCUT
    ).convert("RGB")
    rgb.putalpha(alpha)
    return rgb

def generated_texture(
    family: str,
    material: str,
    variant: str,
    seed: int,
    size: int = DEFAULT_TEXTURE_SIZE,
    role: str = "side",
    calibration: dict[str, float] | None = None,
) -> Image.Image:
    """Generate a semantic, seamless albedo texture from material parameters."""
    colors = FAMILY_COLORS[family]
    base = MATERIAL_TINTS.get((family, material), colors[stable_seed(f"{family}:{material}") % len(colors)])
    pale_bark = (
        family == "wood" and material in ("birch", "aspen")
        and variant == "bark" and role != "endgrain"
    )
    birch_bark = pale_bark and material == "birch"
    soft_bark = (
        family == "wood" and material in ("beech", "poplar")
        and variant == "bark" and role != "endgrain"
    )
    bark_surface = family == "wood" and variant == "bark" and role != "endgrain"
    if family == "organic" and material == "leaf":
        # Leaf variants carry seasonal and canopy-density color roles instead
        # of changing only the noise seed.
        base = {
            "fresh": (56, 91, 40), "mature": (48, 79, 36),
            "dense": (42, 73, 33), "sparse": (71, 103, 49),
            "spotted": (60, 88, 40), "striped": (49, 82, 37),
            "pale": (91, 113, 52), "dark": (35, 61, 30),
            "young": (70, 111, 51), "dry": (117, 105, 49),
        }.get(variant, base)
    if role == "water_surface":
        # Keep the tile itself quiet so it does not stamp the same 16px motif
        # over a whole lake. World-space shading supplies the broad, changing
        # ripples; the base tile contributes only fine blue-water variation.
        base = (62, 151, 218)
    if calibration and all(key in calibration for key in ("mean_rgb_r", "mean_rgb_g", "mean_rgb_b")):
        # Only the family's aggregate color centroid is transferred; no source
        # pixels or spatial samples participate in texture synthesis.
        weight = max(0.0, min(0.75, calibration.get("color_weight", 0.12)))
        base = tuple(round(base[i] * (1.0 - weight) + calibration[key] * weight) for i, key in enumerate(("mean_rgb_r", "mean_rgb_g", "mean_rgb_b")))
    if family == "wood" and (role == "endgrain" or "endgrain" in variant):
        return _wood_endgrain_texture(base, variant, seed, size)
    if family == "wood" and role in ("top", "bottom"):
        return _wood_surface_texture(base, variant, seed, size, role)
    if bark_surface:
        if pale_bark:
            # Birch is nearly white; aspen bark is a warmer gray with fewer
            # dark lenticels. Their species marks are drawn after shading.
            base = (246, 246, 241) if birch_bark else (210, 207, 192)
        elif material in BARK_BASE_TINTS:
            base = BARK_BASE_TINTS[material]
        else:
            # Bark is darker and more chromatically varied than finished timber.
            base = tuple(round(channel * factor) for channel, factor in zip(base, (0.441, 0.512, 0.699)))
    if role == "water_surface":
        palette_factors = (0.96, 0.985, 1.015, 1.04)
    elif family == "soil" and material in SAND_MATERIALS:
        # Fine sand uses restrained luminance steps; a wide palette creates
        # high-contrast diagonal patches when the 16px tile repeats at scale.
        palette_factors = (0.92, 0.96, 1.0, 1.04, 1.08)
    elif family == "soil" and material == "clay":
        # Sand and fine earth need grain detail without the dark, high-contrast
        # blotches that make a 16px tile read like stone or mud.
        palette_factors = (0.84, 0.93, 1.0, 1.08)
    elif family == "organic" and material in ("leaf", "moss", "lichen", "fern"):
        palette_factors = (0.70, 0.86, 1.0, 1.16)
    elif soft_bark:
        palette_factors = (0.91, 0.96, 1.0, 1.04, 1.08)
    elif pale_bark:
        palette_factors = (0.84, 0.91, 0.96, 1.0, 1.04, 1.08)
    elif bark_surface:
        palette_factors = (0.45, 0.57, 0.69, 0.81, 0.93, 1.05, 1.17, 1.29, 1.41)
    elif family == "wood":
        palette_factors = (0.70, 0.86, 1.0, 1.15)
    else:
        palette_factors = (0.62, 0.81, 1.0, 1.20)
    colors4 = [shade(base, factor) for factor in palette_factors]
    coarse, medium, fine = _wrapped_noise(seed, size)
    micro = tile_noise(seed ^ 0x6D494352, size, size, max(2, size * 3 // 4))
    pixel_grain = tile_noise(seed ^ 0x504958454C, size, size, size)
    soil_pixel_grain = (
        tile_noise(seed ^ 0x534F494C, size, size, size)
        if family == "soil" else None
    )
    sand_grain = (
        tile_noise(seed ^ 0x53414E44, size, size, size)
        if family == "soil" and material in SAND_MATERIALS else None
    )
    bark_plate_tones, bark_crack_strengths = (
        _wrapped_cellular_plates(seed, size, material)
        if bark_surface
        else (None, None)
    )
    style = PATTERN_BY_FAMILY[family]
    pixels: list[tuple[int, int, int, int]] = []
    material_bias = (stable_seed(material) % 7) - 3
    variant_bias = stable_seed(variant) % 5
    sand_material = family == "soil" and material in SAND_MATERIALS
    detail_scale = max(
        0.55,
        min(1.7, (calibration or {}).get("detail_scale", 0.72 if sand_material else 1.0)),
    )
    contrast_scale = max(0.65, min(1.5, (calibration or {}).get("contrast_scale", 1.0)))
    coarse_scale = max(0.6, min(1.5, (calibration or {}).get("coarse_scale", 1.0)))
    micro_scale = max(0.6, min(1.5, (calibration or {}).get("micro_scale", 1.0)))
    if any(word in variant for word in ("polished", "smooth", "glazed")):
        detail_scale *= 0.68
    elif any(word in variant for word in ("rough", "fractured", "cracked", "speckled", "weathered")):
        detail_scale *= 1.28

    for y in range(size):
        for x in range(size):
            i = y * size + x
            if role == "water_surface":
                # Water needs small, multidirectional ripples. The previous
                # glass-family mix was dominated by 2x2 and 4x4 cells, making
                # broad stepped patches repeat visibly across every fluid
                # block. Shift energy to the fine wrapped fields and texels.
                value = (
                    coarse[i] * 0.02
                    + medium[i] * 0.06
                    + fine[i] * 0.12 * micro_scale
                    + micro[i] * 0.27 * micro_scale
                    + pixel_grain[i] * 0.53
                ) * detail_scale
            elif family == "soil" and material in SAND_MATERIALS:
                # Keep sand grains isotropic: broad correlated fields leave
                # diagonal smears across a beach after the 16px tile repeats.
                # A little 12-cell clustering groups grains without drawing
                # long bands; independent wrapped texels preserve a crisp,
                # seamless granular surface.
                value = (
                    fine[i] * 0.06 * micro_scale
                    + micro[i] * 0.08 * micro_scale
                    + sand_grain[i] * 0.30
                    + pixel_grain[i] * 0.08
                ) * detail_scale
            elif family == "soil":
                # Shift detail from broad, soft blobs into individual wrapped
                # grains. The earlier coarse-heavy mix had the right overall
                # luminance spread but half the local contrast of the dirt
                # references, so 16px soil read as smudged rather than granular.
                value = (
                    coarse[i] * 0.20 * coarse_scale
                    + medium[i] * 0.18
                    + fine[i] * 0.18 * micro_scale
                    + micro[i] * 0.24 * micro_scale
                    + soil_pixel_grain[i] * 0.20 * micro_scale
                ) * detail_scale
            elif family == "rock" and style == "grain":
                value = (
                    coarse[i] * 0.38 * coarse_scale
                    + medium[i] * 0.25
                    + fine[i] * 0.17 * micro_scale
                    + micro[i] * 0.20 * micro_scale
                ) * detail_scale
            else:
                # Keep large forms, but reserve more of the signal for texel
                # scale structure. The old coarse-heavy mix made ceramics,
                # metals, glass and several organic surfaces look airbrushed
                # beside the already detailed soil and rock families.
                value = (
                    coarse[i] * 0.38 * coarse_scale
                    + medium[i] * 0.30
                    + fine[i] * 0.20 * micro_scale
                    + micro[i] * 0.12 * micro_scale
                ) * detail_scale
            # Family patterns describe recognizable structure, while noise only
            # perturbs that structure instead of becoming the whole texture.
            if style == "wood":
                if bark_surface:
                    # Bark uses short, species-scaled flakes rather than the
                    # vertical sinusoidal grain that made trunks look wavy.
                    if birch_bark:
                        value += coarse[i] * 0.10 + medium[i] * 0.08 + fine[i] * 0.07
                    elif material == "beech":
                        # Beech bark is smooth and pale, with subdued tonal
                        # variation before its small lenticel marks.
                        value += coarse[i] * 0.06 + medium[i] * 0.05 + fine[i] * 0.04
                    else:
                        conifer_bark = material in ("cedar", "larch", "pine", "redwood", "spruce")
                        plate_strength = 0.24 if material == "poplar" else 0.30 if conifer_bark else 0.44
                        crack_strength = 0.17 if material == "poplar" else 0.26 if conifer_bark else 0.31
                        value += bark_plate_tones[i] * plate_strength
                        value -= bark_crack_strengths[i] * crack_strength
                        if fine[i] > 0.67 and medium[i] < 0.18:
                            value += 0.14
                        elif fine[i] < -0.72:
                            value -= 0.12
                else:
                    wave = 1.65 * coarse[i] + 0.75 * medium[i]
                    grain_phase = (
                        (x + wave * 0.38 + material_bias * 0.25)
                        * math.tau * 3.0 / size
                        + medium[i] * 0.22
                    )
                    grain = math.cos(grain_phase)
                    value += grain * 0.15 + fine[i] * 0.08
                    if variant in ("aged", "weathered"):
                        value += -0.18 if fine[i] > 0.78 and medium[i] < 0.0 else 0.0
                    elif variant == "carved":
                        carve = math.cos(grain_phase * 1.5 + coarse[i] * 0.4)
                        value += 0.25 if carve > 0.76 else -0.12 if carve < -0.82 else 0.0
            elif style == "brick":
                if variant == "brick" or material == "brick":
                    unit = max(3, size // 4)
                    row = y // unit
                    col = (x + (unit if row % 2 else 0)) % (unit * 2)
                    value += -0.62 if col == 0 or y % unit == 0 else 0.04
                elif variant == "tiled":
                    unit = max(3, size // 4)
                    seam = x % unit == 0 or y % unit == 0
                    value += -0.52 if seam else 0.06
                elif variant == "cobble" or material == "cobble":
                    unit = max(5, size // 2)
                    row = y // unit
                    local_x = (x + (unit // 2 if row % 2 else 0)) % unit
                    seam = local_x == 0 or y % unit == 0 or coarse[i] < -0.72
                    value += -0.53 if seam else 0.09
                else:
                    value += coarse[i] * 0.20 + medium[i] * 0.12
                    if variant == "chiseled":
                        radius = size * 0.23
                        diamond = abs(abs(x - (size - 1) / 2) + abs(y - (size - 1) / 2) - radius)
                        value += 0.42 if diamond < 1.15 else -0.12 if diamond < 2.0 else 0.0
                    elif variant == "cracked":
                        crack = (x + y + seed % max(3, size // 2)) % max(5, size // 2)
                        value += -0.54 if crack == 0 and medium[i] > -0.45 else 0.0
                    elif variant == "mossy" and coarse[i] > 0.48:
                        value += 0.32
                    elif variant == "weathered":
                        value += -0.24 if fine[i] > 0.64 and coarse[i] < 0.0 else 0.0
                if role == "top":
                    value += 0.08
            elif style == "tile":
                unit = max(3, size // 2)
                seam = x % unit == 0 or y % unit == 0
                value += -0.48 if seam else 0.05
                if variant in ("mosaic", "patterned", "checked"):
                    value += 0.18 if ((x // max(1, size // 4) + y // max(1, size // 4) + variant_bias) % 2) else -0.03
            elif style == "weave":
                thread = (x + y + variant_bias) % max(3, size // 4)
                value += 0.19 if thread < max(1, size // 8) else -0.09
            elif style == "metal":
                # A brushed surface has directional variation, but avoid a
                # bright stripe at the same row interval on every metal.
                value += medium[i] * 0.12 + fine[i] * 0.09
                if variant == "brushed":
                    value += coarse[i] * 0.12 + fine[i] * 0.10
                elif variant in ("raw", "hammered", "weathered", "oxidized"):
                    value += pixel_grain[i] * 0.10
                elif variant == "polished":
                    value *= 0.78
            elif style == "glass":
                if role == "water_surface":
                    value += fine[i] * 0.11 + micro[i] * 0.12 + pixel_grain[i] * 0.16
                elif variant in ("lined", "rippled"):
                    line = math.cos((x - y + coarse[i] * 1.8 + variant_bias) * math.tau / max(5, size // 2))
                    value += line * 0.12
                elif variant == "bubbled":
                    value += 0.17 if fine[i] > 0.46 and medium[i] > 0.02 else -0.025
                elif variant == "frosted":
                    value += fine[i] * 0.12 + pixel_grain[i] * 0.08
                else:
                    value += medium[i] * 0.05 + fine[i] * 0.04
            elif style == "crystal":
                facet = (x + y + (x // max(1, size // 4)) * 2 + variant_bias) % max(5, size // 2)
                value += 0.3 if facet < max(2, size // 8) else -0.08
            elif style in ("organic", "mottle"):
                if not (style == "mottle" and material in SAND_MATERIALS):
                    # Wrapped noise gives foliage, moss and soft stone
                    # mottling irregular clumps instead of a repeated sine grid.
                    patch = coarse[i] * 0.58 + medium[i] * 0.42
                    value += patch * (0.24 if style == "organic" else 0.14)
                if style == "mottle":
                    if variant == "layered":
                        value += coarse[i] * 0.18 + medium[i] * 0.12
                    elif variant == "cracked":
                        crack = (x * 3 + y * 5 + seed) % max(7, size)
                        value += -0.48 if crack in (0, 1) and coarse[i] > -0.25 else 0.0
                    elif variant in ("coarse", "pebbled"):
                        value += 0.30 if fine[i] > 0.64 and medium[i] > 0.05 else 0.0
                    elif variant == "rooted":
                        root = abs(0.68 * medium[i] + 0.32 * fine[i])
                        value += -0.36 if root < 0.11 and coarse[i] > -0.2 else 0.0
                    if material == "wet_mud":
                        value += 0.24 if medium[i] > 0.42 else -0.06
            if style == "grain":
                if material in ("granite", "diorite", "porphyry", "conglomerate", "pumice", "tuff"):
                    fleck = fine[i] > (0.48 if material in ("granite", "diorite", "conglomerate") else 0.72)
                    value += 0.34 if fleck else -0.08 if fine[i] < -0.78 else 0.0
                elif material in ("sandstone", "slate", "schist", "gneiss", "shale", "travertine"):
                    band = coarse[i] * 0.62 + medium[i] * 0.38
                    value += band * (0.30 if material in ("gneiss", "schist", "travertine") else 0.20)
                elif material == "marble":
                    vein = abs(math.sin((x + y + seed % size + coarse[i] * 2.5) * math.tau / size))
                    value += 0.48 if vein < 0.16 else -0.12
                elif material == "obsidian":
                    glint = fine[i] > 0.88 and medium[i] > 0.12
                    value += 0.56 if glint else -0.18
                elif material == "basalt":
                    value += -0.13 if fine[i] > 0.70 else 0.0
                if variant in ("layered", "banded"):
                    value += 0.16 * coarse[i] + 0.12 * medium[i]
                elif variant in ("fractured", "cracked"):
                    fracture = (x * 5 + y * 3 + seed) % max(7, size)
                    value += -0.48 if fracture == 0 and medium[i] > -0.25 else 0.0
                elif variant == "speckled":
                    value += 0.40 if fine[i] > 0.52 else 0.0
                elif variant == "polished":
                    value *= 0.70

            if role == "bottom":
                value -= 0.13
            elif role == "top":
                value += 0.04
            value *= contrast_scale
            if bark_surface:
                index = max(0, min(len(colors4) - 1, int(round((len(colors4) - 1) * 0.5 + value * 5.0 + (variant_bias - 2) * 0.035))))
            elif family == "soil" and material in SAND_MATERIALS:
                index = max(0, min(len(colors4) - 1, int(round((len(colors4) - 1) * 0.5 + value * 5.0 + (variant_bias - 2) * 0.035))))
            else:
                index = max(0, min(3, int(round(1.5 + value + (variant_bias - 2) * 0.035))))
            # Use a periodic field for micro accents. Independent per-pixel
            # random draws create discontinuities at the wrap boundary.
            if abs(fine[i]) > 0.68 and style in ("grain", "mottle", "organic", "crystal") and not (family == "soil" and material in SAND_MATERIALS):
                index = max(0, min(3, index + (1 if fine[i] > 0 else -1)))
            if size >= 32:
                # 64px material maps retain continuous color variation instead
                # of the restricted palette used by the pixel-art profile.
                factor = max(0.58, min(1.42, 1.0 + value * 0.22 + fine[i] * 0.03))
                rgb = tuple(max(0, min(255, round(channel * factor))) for channel in base)
                if style in ("grain", "mottle", "organic", "crystal") and fine[i] > 0.94 * detail_scale:
                    rgb = shade(rgb, 1.12)
                elif style in ("grain", "mottle", "organic", "crystal") and fine[i] < -0.94 * detail_scale:
                    rgb = shade(rgb, 0.88)
            else:
                if bark_surface and not pale_bark:
                    bark_factor = palette_factors[index]
                    channel_contrast = ((0.62, 0.76, 0.88) if soft_bark
                                        else (1.08, 1.24, 1.34))
                    rgb = tuple(
                        max(0, min(255, round(base[channel] * (
                            1.0 + (bark_factor - 1.0) * channel_contrast[channel]
                        ))))
                        for channel in range(3)
                    )
                else:
                    rgb = colors4[index]
            # The broad fields define material structure. This wrapped texel
            # layer restores crisp 16px grain after palette quantization, which
            # otherwise turns dirt, stone and foliage into smooth bands.
            if size <= 16:
                if role == "water_surface":
                    pixel_detail = 1
                elif soft_bark:
                    pixel_detail = 2
                elif pale_bark:
                    pixel_detail = 2 if birch_bark else 3
                elif family == "soil" and material not in SAND_MATERIALS:
                    pixel_detail = 11
                elif sand_material:
                    pixel_detail = 2
                elif family == "rock" and style == "grain":
                    pixel_detail = 7
                elif family == "organic" and style in ("organic", "mottle"):
                    pixel_detail = 6
                elif family == "wood":
                    pixel_detail = 6 if variant == "bark" else 4
                elif family == "masonry":
                    pixel_detail = 4
                elif family == "ceramic":
                    pixel_detail = 3 if variant in ("glazed", "glossy", "polished") else 4
                elif family == "fabric":
                    pixel_detail = 3
                elif family == "metal":
                    pixel_detail = 3 if variant == "polished" else 4
                elif family == "glass":
                    pixel_detail = 1 if variant in ("plain", "clear") else 2
                elif family == "crystal":
                    pixel_detail = 5
                else:
                    pixel_detail = 0
            else:
                scale_detail = 16.0 / size
                if role == "water_surface":
                    pixel_detail = max(1, round(2 * scale_detail))
                elif soft_bark:
                    pixel_detail = max(1, round(2 * scale_detail))
                elif family == "soil" and material not in SAND_MATERIALS:
                    pixel_detail = max(1, round(11 * scale_detail))
                elif sand_material:
                    pixel_detail = max(1, round(2 * scale_detail))
                elif family == "rock" and style == "grain":
                    pixel_detail = max(1, round(7 * scale_detail))
                elif family == "organic" and style in ("organic", "mottle"):
                    pixel_detail = max(1, round(6 * scale_detail))
                elif family == "wood":
                    pixel_detail = max(1, round((6 if variant == "bark" else 4) * scale_detail))
                elif family == "masonry":
                    pixel_detail = max(1, round(4 * scale_detail))
                elif family == "ceramic":
                    pixel_detail = max(1, round(4 * scale_detail))
                elif family == "fabric":
                    pixel_detail = max(1, round(3 * scale_detail))
                elif family == "metal":
                    pixel_detail = max(1, round(4 * scale_detail))
                elif family == "glass":
                    pixel_detail = max(1, round(2 * scale_detail))
                elif family == "crystal":
                    pixel_detail = max(1, round(5 * scale_detail))
                else:
                    pixel_detail = 0
            if pixel_detail:
                grain_delta = round(pixel_grain[i] * pixel_detail)
                rgb = tuple(max(0, min(255, channel + grain_delta)) for channel in rgb)
            alpha = 204 if role == "water_surface" else 176 if style == "glass" else 255
            pixels.append((*rgb, alpha))

    image = Image.new("RGBA", (size, size))
    image.putdata(pixels)
    if pale_bark:
        image = _apply_birch_bark_marks(image, seed, material)
    elif soft_bark:
        image = _apply_soft_bark_marks(image, seed, material)
    elif bark_surface and material in ("cedar", "larch", "pine", "redwood", "spruce"):
        image = _apply_conifer_bark_furrows(image, seed, material)
    elif family == "organic" and material == "needle":
        original_alpha = image.getchannel("A")
        image = _apply_needle_foliage(image, seed, variant)
        if role != "leaf_cutout":
            image.putalpha(original_alpha)

    # Grass/dirt is a layered material: a ragged vegetation edge sits above
    # exposed soil on side faces. Its thickness is proportional to resolution.
    if role in ("grass_side", "dry_grass_side", "snow_side", "litter_side"):
        px = image.load()
        palettes = {
            "grass_side": ((47, 99, 27, 255), (72, 129, 40, 255)),
            "dry_grass_side": ((139, 132, 67, 255), (174, 159, 83, 255)),
            "snow_side": ((207, 218, 223, 255), (235, 240, 239, 255)),
            "litter_side": ((91, 76, 50, 255), (123, 99, 57, 255)),
        }
        lower, upper = palettes[role]
        edge_noise = tile_noise(seed ^ 0xA53C, size, 1, max(2, size // 4))
        if role == "grass_side":
            # The image's upper rows are a narrow grass cap, while most of
            # each side face stays exposed earth. Keep a ragged boundary so
            # the cap does not become a ruler-straight line at 16px.
            grass_height = max(3, size // 4)
            edge = [
                max(2, min(size - 1, grass_height + round(edge_noise[x] * max(1, size // 8))))
                for x in range(size)
            ]
        else:
            base_depth = max(3, size // 4)
            edge = [max(1, min(size // 2, base_depth + round(edge_noise[x] * max(1, size // 8)))) for x in range(size)]
        grass_shades = ((54, 94, 24, 255), (61, 104, 25, 255),
                        (66, 115, 27, 255), (72, 127, 30, 255))
        soil_coordinates: list[tuple[int, int]] = []
        for x, depth in enumerate(edge):
            for y in range(depth):
                grain = micro[y * size + x] * 0.78 + medium[y * size + x] * 0.22
                if role == "grass_side":
                    shade_position = max(0.0, min(3.0, (grain * 2.5 + 1.0) * 1.5))
                    shade_index = int(shade_position)
                    next_index = min(3, shade_index + 1)
                    shade_mix = shade_position - shade_index
                    color = tuple(
                        round(grass_shades[shade_index][channel] * (1.0 - shade_mix)
                              + grass_shades[next_index][channel] * shade_mix)
                        for channel in range(4)
                    )
                else:
                    color = upper if grain > 0.24 else lower
                if y == depth - 1 and (x * 11 + seed) % max(3, size // 4) == 0:
                    # Short hanging blades break the straight grass/dirt seam.
                    px[x, y] = upper
                else:
                    px[x, y] = color
            if role == "grass_side":
                soil_coordinates.extend((x, y) for y in range(depth, size))
        if role == "grass_side" and soil_coordinates:
            # Calibrate the soil band independently from the larger grass
            # region. Otherwise whole-image quantization spends almost every
            # color on green and leaves the soil nearly flat. These targets
            # reproduce the reference's aggregate brown centroid and spread,
            # while the actual pixel arrangement comes from wrapped noise.
            common_noise = [
                coarse[y * size + x] * 0.24
                + medium[y * size + x] * 0.25
                + fine[y * size + x] * 0.18
                + soil_pixel_grain[y * size + x] * 0.33
                for x, y in soil_coordinates
            ]
            independent_noise = [
                [channel[y * size + x] for x, y in soil_coordinates]
                for channel in (
                    tile_noise(seed ^ 0x534F494C52, size, size, size),
                    tile_noise(seed ^ 0x534F494C47, size, size, size),
                    tile_noise(seed ^ 0x534F494C42, size, size, size),
                )
            ]
            common_mean = statistics.fmean(common_noise)
            common_std = statistics.pstdev(common_noise) or 1.0
            common_normalized = [(value - common_mean) / common_std for value in common_noise]
            channel_weights = (0.96, 0.96, 0.92)
            soil_noise = []
            for channel, weight in enumerate(channel_weights):
                values = independent_noise[channel]
                noise_mean = statistics.fmean(values)
                noise_std = statistics.pstdev(values) or 1.0
                independent_normalized = [(value - noise_mean) / noise_std for value in values]
                mixed = [
                    common_normalized[i] * weight
                    + independent_normalized[i] * math.sqrt(1.0 - weight * weight)
                    for i in range(len(soil_coordinates))
                ]
                mixed_mean = statistics.fmean(mixed)
                mixed_std = statistics.pstdev(mixed) or 1.0
                soil_noise.append([(value - mixed_mean) / mixed_std for value in mixed])
            soil_targets = ((100.0, 15.0), (70.0, 16.0), (44.0, 17.0))
            for index, (x, y) in enumerate(soil_coordinates):
                px[x, y] = (
                    *(max(0, min(255, round(mean + soil_noise[channel][index] * deviation)))
                      for channel, (mean, deviation) in enumerate(soil_targets)),
                    255,
                )
    elif role in ("grass_top", "dry_grass_top", "snow_top", "litter_top"):
        px = image.load()
        for y in range(size):
            for x in range(size):
                i = y * size + x
                if role == "grass_top":
                    # Turf covers the full top face; never reduce it to an edge mask.
                    detail = (
                        coarse[i] * 0.02 + medium[i] * 0.05
                        + micro[i] * 0.23 + pixel_grain[i] * 0.70
                    )
                    green = max(80, min(164, 112 + round(detail * 24)))
                    red = max(35, min(96, 65 + round((coarse[i] * 0.15 + pixel_grain[i] * 0.85) * 10)))
                    blue = max(16, min(66, 26 + round((medium[i] * 0.15 - pixel_grain[i] * 0.85) * 9)))
                    px[x, y] = (red, green, blue, 255)
                elif role == "dry_grass_top":
                    grain = pixel_grain[i]
                    px[x, y] = (
                        max(109, min(188, 151 + round(coarse[i] * 5 + grain * 26))),
                        max(112, min(183, 147 + round(medium[i] * 5 + grain * 24))),
                        max(48, min(104, 69 + round(fine[i] * 3 + grain * 13))),
                        255,
                    )
                elif role == "snow_top":
                    shade_value = round(coarse[i] * 2 + fine[i] * 2 + pixel_grain[i] * 14)
                    px[x, y] = (max(196, 232 + shade_value), max(205, 239 + shade_value), max(207, 242 + shade_value), 255)
                else:
                    px[x, y] = (143, 108, 62, 255) if pixel_grain[i] > 0.57 or fine[i] > 0.57 else (96, 70, 45, 255)
    elif role == "ore":
        px = image.load()
        ore_colors = MATERIAL_TINTS.get(("crystal", material), (185, 143, 73))
        for y in range(size):
            for x in range(size):
                cluster = (x * 13 + y * 7 + seed) % max(5, size // 2)
                if cluster == 0 and coarse[y * size + x] > -0.2:
                    px[x, y] = (*ore_colors, 255)
    return image


def apply_leaf_cutout(image: Image.Image, seed: int,
                      transparent_ratio: float = 0.38) -> Image.Image:
    """Apply a deterministic, seamless 16 px leaf cluster mask."""
    image = image.convert("RGBA")
    size = image.width
    if image.height != size:
        raise ValueError("leaf cutout textures must be square")
    # Wrapped multi-scale noise gives the foliage a clustered silhouette while
    # keeping the mask periodic at the tile boundary.
    unique_size = max(2, size - 1)
    leaf_seed = seed ^ 0x4C454146
    coarse = tile_noise(leaf_seed ^ 0x5A17, unique_size, unique_size,
                        max(2, size // 8))
    medium = tile_noise(leaf_seed ^ 0x1B83, unique_size, unique_size,
                        max(2, size // 4))
    fine = tile_noise(leaf_seed ^ 0xC2B2, unique_size, unique_size,
                      max(2, size // 2))
    pixel_grain = tile_noise(leaf_seed ^ 0x47524149, unique_size,
                             unique_size, unique_size)
    pixels = image.load()
    field = [
        coarse[(y % unique_size) * unique_size + (x % unique_size)] * 0.08
        + medium[(y % unique_size) * unique_size + (x % unique_size)] * 0.20
        + fine[(y % unique_size) * unique_size + (x % unique_size)] * 0.62
        + pixel_grain[(y % unique_size) * unique_size + (x % unique_size)] * 0.35
        for y in range(size) for x in range(size)
    ]
    # Add restrained wrapped pixel grain to the color field. The earlier
    # broad-only field made foliage read as airbrushed at the 16px target.
    shade_field = [
        coarse[(y % unique_size) * unique_size + (x % unique_size)] * 0.35
        + medium[(y % unique_size) * unique_size + (x % unique_size)] * 0.24
        + fine[(y % unique_size) * unique_size + (x % unique_size)] * 0.15
        + pixel_grain[(y % unique_size) * unique_size + (x % unique_size)] * 0.72
        for y in range(size) for x in range(size)
    ]
    # Select a periodic mask with the requested coverage. Border samples are
    # duplicated by wraparound, so weight them by how often they occur in the
    # final tile. A weighted rank selection avoids the coverage drift caused
    # by choosing a quantile from the smaller (size - 1)² sample grid.
    transparent_ratio = max(0.05, min(0.75, transparent_ratio))
    target_holes = round(size * size * transparent_ratio)
    ranked_cells: list[tuple[float, int, int, int]] = []
    for y in range(unique_size):
        for x in range(unique_size):
            weight = (2 if x == 0 else 1) * (2 if y == 0 else 1)
            ranked_cells.append((field[y * size + x], y, x, weight))
    ranked_cells.sort(key=lambda item: (item[0], item[1], item[2]))

    # Minimize the sum of ranks for exactly target_holes pixels. This keeps
    # transparent gaps clustered in low-field regions while respecting the
    # duplicated edge weights required for a seamless mask.
    best: list[tuple[int, tuple[tuple[int, int], ...]] | None] = [None] * (target_holes + 1)
    best[0] = (0, ())
    for rank, (_, y, x, weight) in enumerate(ranked_cells):
        for count in range(target_holes, weight - 1, -1):
            previous = best[count - weight]
            if previous is None:
                continue
            candidate = (previous[0] + rank, previous[1] + ((x, y),))
            if best[count] is None or candidate[0] < best[count][0]:
                best[count] = candidate
    selected_holes = set(best[target_holes][1]) if best[target_holes] else set()
    for y in range(size):
        for x in range(size):
            i = y * size + x
            r, g, b, _ = pixels[x, y]
            tone = shade_field[i]
            tone_index = max(0, min(3, int((tone + 1.0) * 2.0)))
            shade_factor = (0.18, 0.58, 1.10, 1.85)[tone_index]
            if medium[(y % unique_size) * unique_size + (x % unique_size)] > 0.48:
                shade_factor = min(1.89, shade_factor + 0.04)
            if fine[(y % unique_size) * unique_size + (x % unique_size)] < -0.62:
                shade_factor = max(0.14, shade_factor - 0.04)
            channel_factors = (
                shade_factor,
                1.0 + (shade_factor - 1.0) * 1.08,
                1.0 + (shade_factor - 1.0) * 0.60,
            )
            r = max(0, min(255, round(r * channel_factors[0])))
            g = max(0, min(255, round(g * channel_factors[1])))
            b = max(0, min(255, round(b * channel_factors[2])))
            mask_cell = (x % unique_size, y % unique_size)
            pixels[x, y] = (r, g, b, 0 if mask_cell in selected_holes else 255)
    alpha = image.getchannel("A")
    # Leaf cutouts need more RGB shades than opaque tiles because the foliage
    # reference uses subtle hue changes as well as alpha silhouettes. Keep up
    # to 48 RGBA colors while retaining the exact binary alpha mask.
    for palette_size in range(32, 15, -1):
        rgb = image.convert("RGB").quantize(
            colors=palette_size, method=Image.Quantize.MEDIANCUT
        ).convert("RGB")
        rgb.putalpha(alpha)
        palette = rgb.getcolors(maxcolors=49)
        if palette is not None and len(palette) <= 48:
            image = rgb
            break
    else:
        rgb = image.convert("RGB").quantize(
            colors=16, method=Image.Quantize.MEDIANCUT
        ).convert("RGB")
        rgb.putalpha(alpha)
        image = rgb
    return image


@lru_cache(maxsize=512)
def reference_leaf_transparency(source_stem: str) -> float:
    """Read only the alpha coverage of a Minetest leaf reference for QA."""
    reference = (REPO / "resource_packs" / "minetest_default_16" /
                 "textures" / "blocks" / f"{source_stem}.png")
    try:
        with Image.open(reference) as source:
            alpha = source.convert("RGBA").getchannel("A")
            values = list(alpha.getdata())
    except (OSError, ValueError):
        return 0.38
    if not values:
        return 0.38
    return max(0.05, min(0.75, sum(value < 128 for value in values) / len(values)))


@lru_cache(maxsize=1024)
def reference_alpha_coverage(source_stem: str, source_pack: str) -> float | None:
    """Return only the fraction of reference texels with visible alpha."""
    if source_pack not in ("minetest_default_16", "cubatarium_geology_cc0"):
        return None
    reference = (REPO / "resource_packs" / source_pack / "textures" /
                 "blocks" / f"{source_stem}.png")
    try:
        with Image.open(reference) as source:
            alpha = source.convert("RGBA").getchannel("A")
            values = list(alpha.getdata())
    except (OSError, ValueError):
        return None
    if not values:
        return None
    return sum(value >= 128 for value in values) / len(values)


def semantic_sprite_alpha_coverage(
    source_stem: str, reference_coverage: float | None,
) -> float | None:
    """Keep authored silhouettes readable when references use dense masks."""
    if reference_coverage is None:
        return None
    name = source_stem.lower()
    if "junglegrass" in name:
        # The legacy mask is a dense pixel clump. Keeping that full opacity
        # made distinct 16px blades merge into one solid leaf shape; cap the
        # new silhouette while retaining a minimum visible footprint.
        return max(0.28, min(reference_coverage, 0.42))
    if any(token in name for token in ("tallgrass", "grass_1", "dry_grass")):
        # The source masks are tiny flattened tufts. Applying their 8–11%
        # coverage to a single authored blade left only a few stray pixels;
        # the 3D tuft mesh supplies the other viewing angles at runtime.
        return max(reference_coverage, 0.16)
    if any(token in name for token in ("reed", "papyrus", "kelp", "seagrass")):
        # Source textures contain whole stands. Our cards contain one named
        # stalk or blade, and the reed mesh supplies the other directions.
        # Matching a 50%+ source mask expands each single stalk into a blob.
        return min(reference_coverage, 0.24)
    if any(token in name for token in ("mushroom", "cocoa", "cactus_seedling")):
        # Preserve cap, pod and seedling silhouettes instead of expanding them
        # to the dense alpha masks used by the reference sprites.
        return min(reference_coverage, 0.32)
    if "vine" in name:
        return min(reference_coverage, 0.30)
    if any(token in name for token in (
        "netherstalk", "potatoes", "carrots", "wheat", "pumpkin_stem",
        "melon_stem",
    )):
        return min(reference_coverage, 0.32)
    if "fire" in name or "flame" in name:
        # Keep a broad flame silhouette while retaining its interior tongues.
        return min(reference_coverage, 0.48)
    return reference_coverage


def catalog_leaf_transparency(record: dict[str, str]) -> float:
    variant = record.get("variant", "").lower()
    if "sparse" in variant:
        return 0.64
    if "dense" in variant:
        return 0.40
    return 0.54


def is_leaf_material(family: str, material: str, stem: str = "") -> bool:
    if family != "organic":
        return False
    value = f"{material} {stem}".lower()
    if any(token in value for token in ("litter", "leaf_litter", "leaves_tile")):
        return False
    return any(token in value for token in ("leaf", "leaves", "foliage", "needle"))


def procedural_decor_sprite(source_stem: str, size: int, seed: int) -> Image.Image | None:
    """Draw original voxel foliage/flame silhouettes with transparent margins."""
    name = source_stem.lower()
    fire = "fire" in name or "flame" in name
    torch = "torch" in name or "redtorch" in name
    sapling = "sapling" in name
    crop = any(token in name for token in (
        "crops", "carrots", "potatoes", "wheat", "netherstalk", "pumpkin_stem", "melon_stem",
    ))
    cocoa = "cocoa" in name
    fruit = "apple" in name or "berry" in name
    cactus_seedling = "cactus_seedling" in name
    waterlily = "waterlily" in name
    vine = "vine" in name
    fern = "fern" in name
    junglegrass = "junglegrass" in name
    reed = any(token in name for token in ("reed", "papyrus", "kelp", "seagrass"))
    flower = any(token in name for token in ("flower", "blossom", "rose", "tulip", "dandelion"))
    coral = "coral" in name
    mushroom_stem = "mushroom_stem" in name
    bush_stem = "bush_stem" in name and not mushroom_stem
    mushroom = "mushroom" in name and "skin" not in name and not mushroom_stem
    deadbush = "deadbush" in name or "dry_shrub" in name
    grass = any(token in name for token in ("grass_1", "dry_grass", "tallgrass", "marram"))
    if not any((fire, torch, sapling, crop, cocoa, fruit, cactus_seedling, waterlily, vine,
                fern, junglegrass, reed, flower, coral, mushroom, mushroom_stem,
                bush_stem, deadbush, grass)):
        return None

    rng = random.Random(seed)
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    s = size / 64.0

    def xy(x: float, y: float) -> tuple[int, int]:
        return (round(x * s), round(y * s))

    def polygon(points: list[tuple[float, float]], color: tuple[int, int, int, int]) -> None:
        draw.polygon([xy(x, y) for x, y in points], fill=color)

    if fire:
        # One flame silhouette per card. Flame geometry supplies three
        # intersecting planes and adds depth without repeating three separate
        # tongues inside every plane.
        center = 32 + rng.randint(-3, 3)
        drift = rng.randint(-5, 5)
        polygon([(center - 15, 63), (center - 13, 42),
                 (center - 5 + drift, 31), (center + 1 + drift, 18),
                 (center + 7, 34), (center + 14, 63)],
                (188, 55, 23, 255))
        polygon([(center - 10, 63), (center - 8, 45),
                 (center + drift, 36), (center + 2 + drift, 28),
                 (center + 9, 63)], (241, 128, 34, 255))
        polygon([(center - 4, 63), (center - 4, 49),
                 (center + drift, 42), (center + 5, 63)],
                (255, 211, 85, 255))
    elif torch:
        red = "red" in name or "redtorch" in name
        flame_outer = (190, 48, 43, 255) if red else (224, 112, 37, 255)
        flame_inner = (248, 108, 53, 255) if red else (255, 205, 83, 255)
        draw.line([xy(32, 63), xy(31 + rng.randint(-2, 2), 29)],
                  fill=(92, 62, 39, 255), width=max(2, round(6*s)))
        polygon([(25, 39), (27, 29), (31, 22), (35, 29), (39, 39), (34, 44)], flame_outer)
        polygon([(28, 38), (30, 30), (32, 26), (36, 38), (33, 41)], flame_inner)
    elif sapling:
        conifer = any(token in name for token in ("pine", "spruce", "cedar", "larch"))
        birch = "birch" in name
        trunk = (211, 204, 176, 255) if birch else (105, 76, 45, 255)
        green = ((45, 83, 42, 255), (61, 103, 47, 255), (83, 119, 54, 255)) if conifer else (
            (76, 133, 52, 255), (96, 151, 66, 255), (57, 111, 48, 255)
        )
        center = 32 + rng.randint(-2, 2)
        top = rng.randint(17, 23)
        draw.line([xy(32, 63), xy(center, top + 15)],
                  fill=trunk, width=max(2, round(4*s)))
        if conifer:
            # A single young conifer leader with two short whorls. The tuft
            # mesh repeats this card around the plant to supply depth.
            for y, spread in ((top + 13, 13), (top + 22, 17), (top + 31, 12)):
                color = green[rng.randrange(len(green))]
                draw.line([xy(center, y), xy(center - spread, y - 6)],
                          fill=color, width=max(2, round(4*s)))
                draw.line([xy(center, y), xy(center + spread, y - 6)],
                          fill=color, width=max(2, round(4*s)))
        else:
            # A small broadleaf sapling is one stem with a few distinct buds,
            # not a complete shrub duplicated on every cluster card.
            for cx, cy, radius in ((center - 8, top + 7, 5),
                                   (center + 7, top + 3, 6),
                                   (center, top - 1, 6)):
                color = green[rng.randrange(len(green))]
                draw.ellipse((*xy(cx - radius, cy - radius),
                              *xy(cx + radius, cy + radius)), fill=color)
            draw.line([xy(center, top + 22), xy(center - 9, top + 16)],
                      fill=trunk, width=max(1, round(2*s)))
            draw.line([xy(center, top + 19), xy(center + 9, top + 12)],
                      fill=trunk, width=max(1, round(2*s)))
    elif bush_stem:
        # Minetest bush stems are open woody branches. Pine needles and bush
        # leaves are separate blocks, so this sprite must stay brown and cut
        # out instead of becoming a solid green foliage tile.
        center = 32 + rng.randint(-2, 2)
        lean = rng.randint(-2, 2)
        if "acacia" in name:
            bark = (112, 101, 86, 255)
            highlight = (151, 137, 119, 255)
            shadow = (76, 69, 61, 255)
        elif "pine" in name:
            bark = (91, 67, 45, 255)
            highlight = (128, 99, 68, 255)
            shadow = (60, 47, 35, 255)
        else:
            bark = (73, 53, 38, 255)
            highlight = (108, 81, 57, 255)
            shadow = (48, 37, 29, 255)

        fork_y = rng.randint(42, 47)
        leader_top = rng.randint(7, 12)
        left_tip = rng.randint(4, 9)
        right_tip = rng.randint(55, 60)
        broad = max(3, round(13*s))
        mid = max(2, round(8*s))
        fine = max(1, round(4*s))
        draw.line([xy(32, 63), xy(center + lean, leader_top)],
                  fill=shadow, width=broad)
        draw.line([xy(31, 59), xy(center + lean - 1, leader_top + 5)],
                  fill=bark, width=mid)
        draw.line([xy(center - 1, fork_y), xy(left_tip, 25 + rng.randint(-2, 3))],
                  fill=shadow, width=broad)
        draw.line([xy(center + 1, fork_y), xy(right_tip, 25 + rng.randint(-2, 3))],
                  fill=shadow, width=broad)
        draw.line([xy(center - 1, fork_y - 1), xy(left_tip + 2, 27)],
                  fill=bark, width=mid)
        draw.line([xy(center + 1, fork_y - 1), xy(right_tip - 2, 27)],
                  fill=bark, width=mid)
        for branch_x, branch_y, side in (
            (center - 8, 39, -1), (center + 7, 36, 1),
            (center - 3, 24, -1), (center + 5, 21, 1),
        ):
            end_x = max(3, min(60, branch_x + side * rng.randint(7, 12)))
            end_y = max(8, branch_y - rng.randint(8, 14))
            draw.line([xy(branch_x, branch_y), xy(end_x, end_y)],
                      fill=bark, width=fine)
        # Short bark flecks add texture without introducing regular vertical
        # grooves into this small branch silhouette.
        for x, y in ((center - 1, 54), (center + 1, 45),
                     (center, 34), (center + 1, 23)):
            draw.line([xy(x, y), xy(x + rng.choice((-1, 0, 1)), y - 3)],
                      fill=highlight, width=fine)
    elif crop:
        dry = "netherstalk" not in name and "wheat" in name
        stem = (141, 113, 55, 255) if dry else (56, 116, 54, 255)
        leaf = (193, 167, 82, 255) if dry else (91, 151, 61, 255)
        center = 32 + rng.randint(-3, 3)
        height = rng.randint(43, 57)
        lean = rng.randint(-5, 5)
        tip_x = center + lean
        draw.line([xy(center, 63), xy(tip_x, 63 - height)],
                  fill=stem, width=max(1, round(3*s)))
        for fraction, side in ((0.38, -1), (0.55, 1), (0.72, -1)):
            y = 63 - height * fraction
            x = center + lean * fraction
            polygon([(x, y + 2), (x + side * 12, y - 5),
                     (x + side * 9, y + 3)], leaf)
        if dry:
            # A compact grain head identifies ripe wheat without making each
            # card an entire sheaf of stems.
            for offset, y in ((-2, 0), (1, 4), (-1, 8), (2, 12)):
                draw.ellipse((*xy(tip_x + offset - 2, 63 - height + y - 2),
                              *xy(tip_x + offset + 2, 63 - height + y + 2)), fill=leaf)
    elif cocoa:
        cx, cy = 32 + rng.randint(-3, 3), 39 + rng.randint(-3, 3)
        pod = (163, 89, 39, 255) if rng.random() > 0.25 else (93, 124, 50, 255)
        draw.ellipse((*xy(cx - 6, cy - 8), *xy(cx + 6, cy + 8)), fill=pod)
        draw.line([xy(cx, cy - 9), xy(cx + rng.randint(-4, 4), cy - 14)],
                  fill=(70, 105, 44, 255), width=max(1, round(3*s)))
    elif fruit:
        fruit_color = (183, 53, 50, 255) if "apple" in name else (129, 55, 116, 255)
        draw.ellipse((*xy(20, 24), *xy(44, 50)), fill=fruit_color)
        draw.polygon([xy(31, 25), xy(32, 15), xy(35, 23)], fill=(88, 63, 39, 255))
        polygon([(32, 23), (42, 17), (40, 26)], (79, 137, 55, 255))
        draw.point(xy(26, 32), fill=(231, 113, 90, 255))
    elif cactus_seedling:
        green = (62, 133, 67, 255)
        draw.rounded_rectangle((*xy(27, 16), *xy(37, 63)), radius=max(1, round(4*s)), fill=green)
        draw.line([xy(31, 21), xy(31, 59)], fill=(121, 175, 83, 255), width=max(1, round(2*s)))
        for y in (25, 34, 43, 52):
            for side in (-1, 1):
                draw.line([xy(32, y), xy(32 + side * 7, y - 2)],
                          fill=(218, 207, 150, 255), width=max(1, round(s)))
    elif waterlily:
        center_x = 32 + rng.randint(-1, 1)
        center_y = 32 + rng.randint(-1, 1)
        radius_x, radius_y = 25, 23
        arc = [
            (center_x + math.cos(math.radians(angle)) * radius_x,
             center_y + math.sin(math.radians(angle)) * radius_y)
            for angle in range(45, 326, 8)
        ]
        # A waterlily is a nearly round floating leaf with a clear radial
        # notch. Its old eight-blade flower silhouette was rendered upright
        # and read as a tiny green star instead of a pond plant.
        leaf = (64, 126, 57, 255)
        polygon([(center_x, center_y), *arc], leaf)
        outline = [xy(x, y) for x, y in arc]
        draw.line(outline, fill=(45, 98, 46, 255), width=max(1, round(2*s)))
        for angle in (70, 112, 154, 196, 238, 280):
            theta = math.radians(angle)
            end_x = center_x + math.cos(theta) * (radius_x - 4)
            end_y = center_y + math.sin(theta) * (radius_y - 4)
            draw.line([xy(center_x, center_y), xy(end_x, end_y)],
                      fill=(92, 151, 72, 255), width=max(1, round(s)))
        # A small bud at the leaf root gives the top-down tile a focal point.
        draw.ellipse((*xy(center_x - 2, center_y - 2),
                      *xy(center_x + 2, center_y + 2)),
                     fill=(226, 190, 91, 255))
    elif vine:
        vine_color = (66, 121, 49, 255)
        leaf_colors = ((74, 133, 52, 255), (92, 151, 60, 255),
                       (57, 111, 45, 255))
        start_x = 31 + rng.randint(-4, 4)
        sway = rng.randint(-8, 8)
        strand = []
        for y in range(3, 64, 7):
            x = start_x + round(sway * (y / 63.0)) + rng.randint(-2, 2)
            strand.append(xy(x, y))
        draw.line(strand, fill=vine_color, width=max(1, round(2*s)))
        # A single climbing stem with alternating leaves; the reed mesh adds
        # four viewing angles around it.
        for index, y in enumerate((12, 24, 37, 50)):
            side = -1 if index % 2 == 0 else 1
            x = start_x + round(sway * (y / 63.0))
            length = rng.randint(8, 13)
            leaf_color = leaf_colors[rng.randrange(len(leaf_colors))]
            polygon([(x, y), (x + side * length, y - 5),
                     (x + side * (length - 2), y + 1),
                     (x + side * 2, y + 3)], leaf_color)
    elif mushroom_stem:
        # This is a separate, transparent stem sprite. It is kept distinct
        # from the complete mushroom silhouette used by mushroom cap blocks.
        center = 31 + rng.randint(-2, 2)
        top_half = rng.randint(4, 6)
        bottom_half = rng.randint(6, 9)
        base = (205, 194, 168) if "dark" not in name and "dry" not in name else (177, 161, 134)
        draw.polygon([xy(center - top_half, 4), xy(center + top_half, 4),
                      xy(center + bottom_half, 63), xy(center - bottom_half, 63)],
                     fill=(*base, 255))
        highlight = tuple(min(255, channel + 27) for channel in base)
        draw.polygon([xy(center - top_half + 2, 5), xy(center - 1, 5),
                      xy(center + 1, 62), xy(center - bottom_half + 2, 62)],
                     fill=(*highlight, 255))
        shadow = tuple(max(0, channel - 34) for channel in base)
        draw.line([xy(center + top_half - 1, 12), xy(center + bottom_half - 1, 57)],
                  fill=(*shadow, 255),
                  width=max(1, round(2*s)))
        for _ in range(3):
            mark_y = rng.randint(9, 58)
            mark_x = center + rng.randint(-bottom_half + 1, bottom_half - 1)
            draw.point(xy(mark_x, mark_y), fill=(*highlight, 255))
    elif mushroom:
        cap_color = (
            (177, 52, 48, 255) if "red" in name else
            (207, 155, 47, 255) if "gold" in name or "yellow" in name else
            (65, 119, 177, 255) if "blue" in name else
            (193, 179, 151, 255)
        )
        center = 32 + rng.randint(-2, 2)
        cap_y = 15 + rng.randint(-2, 2)
        cap_width = rng.randint(13, 16)
        stem_left = center - 5
        stem_right = center + 5
        draw.polygon([xy(stem_left + 2, cap_y + 12), xy(stem_right - 2, cap_y + 12),
                      xy(stem_right + 2, 63), xy(stem_left - 2, 63)],
                     fill=(210, 198, 171, 255))
        draw.polygon([xy(stem_left + 3, cap_y + 13), xy(center - 1, cap_y + 13),
                      xy(center, 62), xy(stem_left, 62)], fill=(236, 226, 205, 255))
        draw.line([xy(stem_right - 1, cap_y + 16), xy(stem_right + 1, 57)],
                  fill=(174, 157, 132, 255), width=max(1, round(2*s)))
        cap_box = (xy(center - cap_width, cap_y), xy(center + cap_width, cap_y + 17))
        draw.ellipse((*cap_box[0], *cap_box[1]), fill=cap_color)
        rim = (229, 218, 190, 255) if "pale" in name else tuple(min(255, c + 50) for c in cap_color[:3]) + (255,)
        draw.arc((*cap_box[0], *cap_box[1]), 180, 360, fill=rim,
                 width=max(1, round(2*s)))
        if "red" in name or "pale" in name:
            for _ in range(rng.randint(3, 5)):
                spot_x = center + rng.randint(-cap_width + 3, cap_width - 3)
                spot_y = cap_y + rng.randint(4, 11)
                draw.ellipse((*xy(spot_x - 1.8, spot_y - 1.5),
                              *xy(spot_x + 1.8, spot_y + 1.5)),
                             fill=(245, 235, 210, 255))
    elif flower:
        petal_color = (
            (226, 221, 207, 255) if "white" in name else
            (218, 185, 59, 255) if "yellow" in name or "dandelion" in name else
            (183, 53, 59, 255) if "red" in name or "rose" in name else
            (214, 113, 157, 255) if "pink" in name else
            (82, 139, 204, 255) if "blue" in name else
            (139, 91, 177, 255) if "purple" in name else
            (224, 111, 48, 255) if "orange" in name else
            (213, 121, 157, 255)
        )
        stem_color = (52, 113, 54, 255)
        center_color = (239, 194, 57, 255) if any(
            token in name for token in ("white", "red", "pink", "blue", "purple")
        ) else (251, 219, 91, 255)
        center = 32 + rng.randint(-3, 3)
        top = rng.randint(11, 20)
        lean = rng.randint(-5, 5)
        center += lean
        head_y = top + 7
        draw.line([xy(32, 63), xy(center, head_y + 4)],
                  fill=stem_color, width=max(1, round(3*s)))
        # A flower texture represents one plant. The cluster mesh places six
        # slender cards around the block; drawing a bouquet here multiplied
        # each card into several overlapping flowers and made meadows look
        # like tall, dense spikes.
        polygon([(center, 46),
                 (center - rng.randint(8, 13), 41),
                 (center - 5, 47),
                 (center, 50)], (61, 130, 57, 255))
        polygon([(center, 49),
                 (center + rng.randint(8, 13), 43),
                 (center + 6, 50),
                 (center, 52)], (72, 139, 59, 255))
        if "tulip" in name:
            polygon([(center - 6, head_y + 5), (center - 5, head_y),
                     (center - 3, head_y + 3), (center, head_y - 2),
                     (center + 3, head_y + 3), (center + 5, head_y),
                     (center + 6, head_y + 5), (center + 3, head_y + 9),
                     (center - 3, head_y + 9)], petal_color)
            draw.line([xy(center - 4, head_y + 4), xy(center, head_y + 7),
                       xy(center + 4, head_y + 4)],
                      fill=tuple(max(0, c - 35) for c in petal_color[:3]) + (255,),
                      width=max(1, round(s)))
        elif "dandelion" in name:
            draw.ellipse((*xy(center - 5, head_y - 5),
                          *xy(center + 5, head_y + 5)), fill=petal_color)
            for angle in range(0, 360, 45):
                theta = math.radians(angle)
                draw.point(xy(center + math.cos(theta) * 6,
                              head_y + math.sin(theta) * 6), fill=center_color)
        elif "rose" in name:
            draw.ellipse((*xy(center - 7, head_y - 6),
                          *xy(center + 7, head_y + 6)), fill=petal_color)
            draw.arc((*xy(center - 5, head_y - 4),
                      *xy(center + 5, head_y + 4)), 190, 345,
                     fill=tuple(max(0, c - 45) for c in petal_color[:3]) + (255,),
                     width=max(1, round(2*s)))
            draw.point(xy(center, head_y), fill=center_color)
        else:
            petal = rng.randint(5, 7)
            for angle in range(0, 360, 72):
                theta = math.radians(angle)
                petal_x = center + math.cos(theta) * petal * 0.52
                petal_y = head_y + math.sin(theta) * 3.6
                draw.ellipse((*xy(petal_x - 2.1, petal_y - 2.7),
                              *xy(petal_x + 2.1, petal_y + 2.7)), fill=petal_color)
            draw.ellipse((*xy(center - 2.5, head_y - 2.5),
                          *xy(center + 2.5, head_y + 2.5)), fill=center_color)
    elif coral:
        coral_color = (196, 83, 99, 255) if seed % 2 else (226, 142, 89, 255)
        colors = (coral_color, (232, 162, 111, 255), (157, 70, 92, 255))
        base_x = rng.randint(27, 37)
        lean = rng.randint(-5, 5)
        draw.line([xy(base_x, 63), xy(base_x + lean, 20)],
                  fill=colors[0], width=max(2, round(6*s)))
        for side, branch_y, end_y in ((-1, 48, 35), (1, 39, 26), (-1, 31, 16)):
            end_x = base_x + lean * (63 - branch_y) / 43 + side * rng.randint(9, 15)
            color = colors[rng.randrange(len(colors))]
            draw.line([xy(base_x + lean * (63 - branch_y) / 43, branch_y),
                       xy(end_x, end_y)], fill=color,
                      width=max(1, round(3*s)))
            draw.ellipse((*xy(end_x - 2, end_y - 2),
                          *xy(end_x + 2, end_y + 2)), fill=color)
    elif deadbush:
        branch_colors = ((128, 103, 57, 255), (154, 123, 68, 255), (102, 91, 56, 255))
        color = branch_colors[rng.randrange(len(branch_colors))]
        center = 31 + rng.randint(-3, 3)
        fork_y = rng.randint(31, 39)
        draw.line([xy(32, 63), xy(center, fork_y)], fill=color,
                  width=max(1, round(3*s)))
        for side in (-1, 1):
            end_x = center + side * rng.randint(12, 20)
            end_y = rng.randint(12, 25)
            draw.line([xy(center, fork_y), xy(end_x, end_y)], fill=color,
                      width=max(1, round(2*s)))
            for fraction in (0.35, 0.68):
                twig_x = center + (end_x - center) * fraction
                twig_y = fork_y + (end_y - fork_y) * fraction
                twig_side = -side
                draw.line([xy(twig_x, twig_y),
                           xy(twig_x + twig_side * rng.randint(5, 9),
                              twig_y - rng.randint(4, 8))],
                          fill=branch_colors[rng.randrange(len(branch_colors))],
                          width=max(1, round(s)))
    elif reed:
        if "kelp" in name:
            # One flexible kelp blade per texture plane; the reed mesh fans
            # four cards around the plant and provides the bundle silhouette.
            stem = (37, 111, 83, 255)
            base_x = 32 + rng.randint(-3, 3)
            height = rng.randint(45, 63)
            lean = rng.randint(-12, 12)
            tip_x = base_x + lean
            color = (54, 145, 103, 255) if rng.random() < 0.62 else (42, 127, 94, 255)
            polygon([(base_x - 2, 63), (base_x + 2, 63),
                     (tip_x + 3, 63 - height * 0.62),
                     (tip_x, 63 - height),
                     (tip_x - 3, 63 - height * 0.56)], color)
            draw.line([xy(base_x, 63), xy(tip_x, 63 - height)],
                      fill=stem, width=max(1, round(2*s)))
        elif "seagrass" in name:
            # Short seagrass uses one curved blade. Four cards make the low
            # underwater rosette without stacking several tufts per card.
            base_x = 32 + rng.randint(-2, 2)
            height = rng.randint(22, 36)
            lean = rng.randint(-13, 13)
            tip_x = base_x + lean
            blade_color = rng.choice(((47, 121, 83, 255),
                                      (67, 145, 94, 255),
                                      (90, 158, 99, 255)))
            polygon([(base_x - 2, 63), (base_x + 2, 63),
                     (tip_x + 2, 63 - height * 0.66),
                     (tip_x, 63 - height),
                     (tip_x - 2, 63 - height * 0.42)], blade_color)
        elif "papyrus" in name:
            # A single papyrus stem and its radial crown per card.
            stem = (111, 130, 57, 255)
            head = (147, 151, 75, 255)
            center = 32 + rng.randint(-2, 2)
            top = rng.randint(13, 20)
            draw.line([xy(32, 63), xy(center, top + 9)],
                      fill=stem, width=max(1, round(3*s)))
            crown_x, crown_y = center, top + 7
            for angle in (-160, -135, -110, -85, -60, -35, -10):
                theta = math.radians(angle)
                end_x = crown_x + math.cos(theta) * rng.randint(7, 11)
                end_y = crown_y + math.sin(theta) * rng.randint(5, 8)
                draw.line([xy(crown_x, crown_y), xy(end_x, end_y)],
                          fill=head, width=max(1, round(2*s)))
                draw.ellipse((*xy(end_x - 1.2, end_y - 1.5),
                              *xy(end_x + 1.2, end_y + 1.5)), fill=head)
        else:
            # One upright reed culm with only a few narrow leaves. The mesh
            # supplies four viewing angles; a full stand in this bitmap made
            # each voxel multiply into a dense screen of stalks.
            culm = (103, 125, 53, 255)
            blade = ((126, 150, 62, 255), (147, 163, 74, 255),
                     (93, 119, 51, 255))
            seed_head = (129, 112, 61, 255)
            center = 32 + rng.randint(-2, 2)
            height = rng.randint(47, 63)
            lean = rng.randint(-3, 3)
            tip_y = 63 - height
            tip_x = center + lean
            draw.line([xy(center, 63), xy(tip_x, tip_y + 4)],
                      fill=culm, width=max(1, round(3*s)))
            for fraction, side in ((0.48, -1), (0.68, 1)):
                y = 63 - height * fraction
                x = center + lean * fraction
                blade_length = rng.randint(7, 10)
                polygon([(x, y + 2), (x + side * blade_length, y - 5),
                         (x + side * (blade_length - 2), y + 1),
                         (x + side, y + 4)], blade[rng.randrange(len(blade))])
            # A slim, seeded head distinguishes common reed from leafy grass.
            draw.line([xy(tip_x, tip_y + 5), xy(tip_x + 1, tip_y - 5)],
                      fill=seed_head, width=max(1, round(2*s)))
            for spike in (-2, 0, 2):
                draw.point(xy(tip_x + spike, tip_y - 2), fill=seed_head)
    elif junglegrass:
        # Junglegrass is a coarse, low clump of strap-like leaves. Drawing
        # several broad, distinct blades keeps it readable at 16px and avoids
        # the tiny feathered silhouette used by true ferns.
        blade_colors = ((46, 113, 46, 255), (62, 137, 53, 255),
                        (79, 151, 59, 255), (96, 161, 68, 255))
        center = 32 + rng.randint(-2, 2)
        leaves = (
            (-13, rng.randint(27, 39), -7),
            (-7, rng.randint(37, 51), -5),
            (0, rng.randint(43, 57), rng.randint(-2, 2)),
            (7, rng.randint(34, 49), 5),
            (13, rng.randint(26, 39), 7),
        )
        for base_offset, height, lean in leaves:
            base_x = center + base_offset
            tip_x = base_x + lean
            half_width = rng.randint(6, 9)
            color = blade_colors[rng.randrange(len(blade_colors))]
            polygon([
                (base_x - half_width, 63),
                (base_x - 3 + lean * 0.30, 63 - height * 0.48),
                (tip_x, 63 - height),
                (base_x + 3 + lean * 0.30, 63 - height * 0.50),
                (base_x + half_width, 63),
            ], color)
            draw.line([xy(base_x, 63), xy(tip_x, 63 - height)],
                      fill=(111, 169, 76, 255), width=max(1, round(1.25*s)))
    elif fern:
        stem_color = (62, 108, 44, 255)
        leaflet_colors = ((81, 137, 52, 255), (96, 151, 59, 255),
                          (71, 123, 47, 255))
        base_x = 32 + rng.randint(-3, 3)
        tip_x = base_x + rng.randint(-8, 8)
        top_y = rng.randint(5, 12)
        draw.line([xy(base_x, 63), xy(tip_x, top_y)],
                  fill=stem_color, width=max(1, round(3*s)))
        for index in range(1, 8):
            fraction = index / 8.0
            y = 63 + (top_y - 63) * fraction
            x = base_x + (tip_x - base_x) * fraction
            leaflet_width = max(4, round((13 - index) * 0.82))
            color = leaflet_colors[index % len(leaflet_colors)]
            for side in (-1, 1):
                leaflet_tip = x + side * leaflet_width
                polygon([(x, y), (leaflet_tip, y - 5),
                         (leaflet_tip - side * 2, y + 1)], color)
    else:
        # One grass blade per card. The tuft mesh arranges six differently
        # angled blades in 3D, so a full five-blade bitmap made the tuft far
        # too dense when projected onto every card.
        dry = "dry" in name or "wheat" in name
        if dry:
            colors = ((150, 133, 66, 255), (178, 158, 78, 255),
                      (195, 174, 95, 255), (125, 116, 61, 255))
        elif "marram" in name:
            colors = ((103, 137, 62, 255), (128, 153, 71, 255),
                      (151, 166, 82, 255), (82, 119, 55, 255))
        else:
            colors = ((61, 126, 44, 255), (78, 151, 51, 255),
                      (102, 164, 62, 255), (52, 111, 42, 255))
        center = 32 + rng.randint(-3, 3)
        height = rng.randint(32, 59)
        tip_x = center + rng.randint(-15, 15)
        half_width = rng.randint(4, 7)
        color = colors[rng.randrange(len(colors))]
        polygon([(center - half_width, 63), (center - 1, 63 - height * 0.42),
                 (tip_x, 63 - height), (center + 1, 63 - height * 0.70),
                 (center + half_width, 63)], color)
        vein = (184, 175, 91, 255) if dry else (130, 181, 77, 255)
        draw.line([xy(center, 63), xy(tip_x, 63 - height)],
                  fill=vein, width=max(1, round(s)))
    return image


def procedural_glass_texture(
    source_stem: str, size: int, material: str, seed: int
) -> Image.Image | None:
    """Create a transparent voxel-glass tile with a small authored bevel."""
    name = source_stem.lower()
    if not any(token in name for token in ("glass", "pane")) or "water" in name:
        return None

    rng = random.Random(seed)
    color = MATERIAL_TINTS.get(("glass", material), MATERIAL_TINTS[("glass", "clear")])
    is_obsidian = "obsidian" in name
    if is_obsidian:
        color = (74, 100, 111)
    highlight = tuple(min(255, channel + (55 if channel > 110 else 34)) for channel in color)
    shadow = tuple(max(0, round(channel * 0.56)) for channel in color)
    detail_only = "detail" in name
    tile = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    px = tile.load()

    if not detail_only:
        # A faint tint lets the world show through the pane. The shared outer
        # edge is symmetric, so repeating the tile does not create a hard seam.
        for y in range(2, 14):
            for x in range(2, 14):
                alpha = 14 + rng.randrange(0, 9)
                px[x, y] = (*color, alpha)
        for i in range(16):
            px[i, 0] = px[i, 15] = (0, 0, 0, 0)
            px[0, i] = px[15, i] = (0, 0, 0, 0)
        for i in range(1, 15):
            px[i, 1] = px[1, i] = (*highlight, 184)
            px[i, 14] = px[14, i] = (*shadow, 150)
            px[i, 2] = px[i, 13] = (*color, 72)
            px[2, i] = px[13, i] = (*color, 72)

    draw = ImageDraw.Draw(tile)
    if detail_only:
        # Detail faces get sparse specular flecks; they remain mostly clear.
        for _ in range(4):
            x = rng.randrange(3, 13)
            y = rng.randrange(3, 13)
            draw.line((x, y, min(13, x + rng.randrange(1, 3)), y),
                      fill=(*highlight, rng.randrange(150, 211)), width=1)
        if is_obsidian:
            draw.point((8, 8), fill=(125, 191, 198, 230))
    else:
        # The interior glints are generated from a stable seed, not sampled
        # from the reference image. Keep them sparse at the 16 px target size.
        for _ in range(2 if not is_obsidian else 4):
            x = rng.randrange(4, 12)
            y = rng.randrange(4, 12)
            px[x, y] = (*highlight, rng.randrange(128, 205))
            if rng.random() < 0.55 and x < 11:
                px[x + 1, y] = (*highlight, rng.randrange(72, 130))
        if is_obsidian:
            px[7, 6] = (135, 202, 202, 220)
            px[8, 6] = (135, 202, 202, 150)

    if size != 16:
        tile = tile.resize((size, size), Image.Resampling.NEAREST)
    return tile


def procedural_coral_skeleton(
    source_stem: str, size: int, seed: int
) -> Image.Image | None:
    """Synthesize a pale, porous cube texture for coral skeleton blocks."""
    if "coral" not in source_stem.lower() or "skeleton" not in source_stem.lower():
        return None
    rng = random.Random(seed)
    coarse = tile_noise(seed ^ 0xC04A1, 16, 16, 8)
    pixels: list[tuple[int, int, int, int]] = []
    palette = (
        (239, 235, 220, 255),
        (228, 221, 202, 255),
        (212, 204, 185, 255),
        (248, 245, 233, 255),
        (185, 177, 160, 255),
    )
    for field in coarse:
        noise = rng.random()
        if field < -0.62 and noise > 0.22:
            pixels.append(palette[2])
        elif noise > 0.94:
            pixels.append(palette[4])
        elif field > 0.62 or noise > 0.80:
            pixels.append(palette[3])
        elif noise > 0.38:
            pixels.append(palette[1])
        else:
            pixels.append(palette[0])
    image = Image.new("RGBA", (16, 16))
    image.putdata(pixels)
    if size != 16:
        image = image.resize((size, size), Image.Resampling.NEAREST)
    return image


def is_decor_sprite_source(source_stem: str) -> bool:
    name = source_stem.lower()
    # A sand block whose name mentions kelp is still a full cube. Kelp is
    # content metadata here, not the geometry of this sand face texture.
    if "sand_with_kelp" in name or "dirt_with_kelp" in name:
        return False
    if any(token in name for token in (
        "_composite", "_grass_side", "_grass_top", "mushroom_skin",
        "mushroom_block", "flower_pot", "flowerpot",
    )):
        return False
    return any(token in name for token in (
        "fire", "flame", "fern", "grass_1", "dry_grass", "tallgrass",
        "marram", "junglegrass", "flower", "rose", "tulip", "dandelion",
        "blossom", "mushroom", "reeds", "papyrus", "kelp",
        "seagrass", "deadbush", "sapling", "torch", "dry_shrub", "shrub",
        "bush_stem",
        "crops", "carrots", "potatoes", "wheat", "netherstalk", "pumpkin_stem",
        "melon_stem", "cocoa", "apple", "berry", "cactus_seedling", "waterlily", "vine",
    ))


def requires_clear_glass_cutouts(stem: str, family: str) -> bool:
    name = stem.lower()
    return family == "glass" and "water" not in name and any(
        token in name for token in ("glass", "pane")
    )


def is_cross_decor_block(block_name: str) -> bool:
    name = block_name.lower()
    if "sand_with_kelp" in name or "dirt_with_kelp" in name:
        return False
    if any(token in name for token in ("flower_pot", "flowerpot", "mushroom_block", "mushroom_stem", "mushroom_skin")):
        return False
    return any(token in name for token in (
        "sapling", "fern", "tall_grass", "tallgrass", "reeds", "papyrus", "kelp", "seagrass",
        "junglegrass", "dry_grass", "grass_1", "flower", "rose", "tulip", "dandelion", "blossom",
        "mushroom", "deadbush", "dry_shrub", "shrub", "crop", "carrot", "potato", "wheat",
        "nether_wart", "cocoa", "torch", "fire", "cactus_seedling", "waterlily", "apple", "vine",
        "pine_needles",
    ))


def _cactus_surface_detail(image: Image.Image, source_stem: str, seed: int) -> Image.Image:
    """Give cactus side and end faces recognizable ribs and areoles at 16px."""
    size = image.width
    if image.height != size:
        return image
    result = image.convert("RGBA")
    draw = ImageDraw.Draw(result)
    rng = random.Random(seed ^ 0xCA7C05)
    if "top" in source_stem.lower() or source_stem.lower().endswith("_top"):
        center = (size - 1) / 2.0
        radius = max(2.0, size * 0.47)
        coarse, _, fine = _wrapped_noise(seed, size)
        for y in range(size):
            for x in range(size):
                dx, dy = x - center, y - center
                distance = math.hypot(dx, dy)
                angle = math.atan2(dy, dx)
                i = y * size + x
                if distance > radius:
                    color = (43, 104, 51, 255)
                else:
                    groove = abs(math.sin(angle * 6.0))
                    color = (
                        (107, 157, 76, 255) if groove > 0.78 else
                        (69, 132, 62, 255) if groove > 0.38 else
                        (51, 112, 55, 255)
                    )
                grain = round((coarse[i] * 0.35 + fine[i] * 0.65) * 9.0)
                result.putpixel((x, y), tuple(
                    max(0, min(255, channel + grain)) for channel in color[:3]
                ) + (255,))
        draw = ImageDraw.Draw(result)
        draw.ellipse((round(center - 1.5), round(center - 1.5),
                      round(center + 1.5), round(center + 1.5)),
                     fill=(132, 173, 91, 255))
        for angle in (0, 60, 120, 180, 240, 300):
            theta = math.radians(angle)
            tip = (round(center + math.cos(theta) * radius * 0.82),
                   round(center + math.sin(theta) * radius * 0.82))
            draw.line((round(center), round(center), *tip),
                      fill=(137, 174, 95, 255), width=max(1, size // 16))
            draw.point(tip, fill=(229, 220, 171, 255))
        return result

    # Four to eight gently shaded vertical ribs are the defining silhouette
    # of a columnar cactus. Discrete rib bands keep the pattern crisp rather
    # than adding another sinusoidal bark-like grain.
    rib_count = max(4, round(size / 3.2))
    spacing = size / rib_count
    for y in range(size):
        for x in range(size):
            phase = ((x + 0.5) / spacing) % 1.0
            distance = abs(phase - 0.5) * 2.0
            if distance < 0.20:
                color = (100, 157, 72, 255)
            elif distance < 0.58:
                color = (68, 132, 61, 255)
            else:
                color = (43, 104, 52, 255)
            grain = ((x * 17 + y * 29 + seed) % 11) - 5
            result.putpixel((x, y), tuple(
                max(0, min(255, channel + round(grain * 0.8)))
                for channel in color[:3]
            ) + (255,))
    draw = ImageDraw.Draw(result)
    for rib in range(rib_count):
        spine_x = round((rib + 0.5) * spacing) % size
        start_y = rng.randrange(max(1, size))
        step_y = max(3, round(size / 4))
        for row in range(start_y, size + start_y, step_y):
            y = row % size
            draw.point((spine_x % size, y), fill=(231, 222, 177, 255))
            if size >= 32:
                draw.point(((spine_x + 1) % size, y), fill=(176, 166, 119, 255))
    return result


def _blueberry_foliage_detail(image: Image.Image, seed: int) -> Image.Image:
    """Add a few blue berry clusters to green foliage without tinting its leaves."""
    result = image.convert("RGBA")
    size = result.width
    draw = ImageDraw.Draw(result)
    rng = random.Random(seed ^ 0xB1E3B3)
    cluster_count = max(3, round(size * 0.24))
    for _ in range(cluster_count):
        x = rng.randrange(size)
        y = rng.randrange(size)
        berry = (42, 61, 157, 255)
        highlight = (118, 143, 211, 255)
        draw.point((x, y), fill=berry)
        if size >= 16 and rng.random() < 0.65:
            draw.point(((x + 1) % size, y), fill=berry)
        draw.point(((x - 1) % size, (y - 1) % size), fill=highlight)
    return result


def _coral_surface_detail(
    image: Image.Image, source_stem: str, seed: int,
) -> Image.Image:
    """Create porous, color-correct coral surfaces from their semantic names."""
    name = source_stem.lower()
    base = next((color for token, color in (
        ("coral_brown", (157, 111, 73)),
        ("coral_cyan", (74, 157, 169)),
        ("coral_green", (84, 148, 91)),
        ("coral_orange", (210, 119, 67)),
        ("coral_pink", (202, 119, 143)),
    ) if token in name), (184, 104, 102))
    size = image.width
    coarse, medium, fine = _wrapped_noise(seed, size)
    pixel = tile_noise(seed ^ 0xC04A1, size, size, size)
    result = Image.new("RGBA", image.size)
    pixels = []
    for index in range(size * size):
        value = coarse[index] * 0.27 + medium[index] * 0.23
        value += fine[index] * 0.25 + pixel[index] * 0.25
        factor = max(0.68, min(1.28, 1.0 + value * 0.22))
        pixels.append(tuple(
            max(0, min(255, round(channel * factor))) for channel in base
        ) + (255,))
    result.putdata(pixels)
    px = result.load()
    rng = random.Random(seed ^ 0xC04A1)
    pore_count = max(7, round(size * 0.72))
    dark = tuple(round(channel * 0.48) for channel in base) + (255,)
    edge = tuple(round(channel * 1.16) for channel in base) + (255,)
    for _ in range(pore_count):
        x, y = rng.randrange(size), rng.randrange(size)
        px[x, y] = dark
        if rng.random() < 0.58:
            px[(x + 1) % size, y] = dark
        if rng.random() < 0.48:
            px[x, (y + 1) % size] = edge
    return result


def add_semantic_face_detail(image: Image.Image, source_stem: str, seed: int) -> Image.Image:
    """Preserve recognizable functional motifs on block face textures."""
    name = source_stem.lower()
    if "cactus" in name and "seedling" not in name:
        return _cactus_surface_detail(image, source_stem, seed)
    if "blueberry" in name and "with_berries" in name:
        return _blueberry_foliage_detail(image, seed)
    if name.startswith("mtg_coral_") and "skeleton" not in name:
        return _coral_surface_detail(image, source_stem, seed)
    if not any(token in name for token in (
        "furnace_front", "dispenser_front", "dropper_front", "workbench",
        "crafting_table", "bookshelf", "anvil", "chest", "enchantment",
    )):
        return image
    image = image.convert("RGBA")
    draw = ImageDraw.Draw(image)
    size = image.width
    s = size / 64.0
    def point(x: float, y: float) -> tuple[int, int]:
        return round(x * s), round(y * s)
    def box(x0: int, y0: int, x1: int, y1: int) -> tuple[int, int, int, int]:
        return tuple(round(v*s) for v in (x0, y0, x1, y1))
    if "enchantment" in name:
        rng = random.Random(seed)
        pixels = image.load()
        for y in range(size):
            for x in range(size):
                pixels[x, y] = (20, 18, 32, 255) if rng.random() > 0.91 else (14, 17, 30, 255)
        if "_top" in name:
            draw.polygon([point(8, 2), point(13, 8), point(8, 13), point(3, 8)],
                         fill=(48, 31, 71, 255), outline=(106, 72, 142, 255))
            draw.line([point(8, 4), point(10, 7), point(8, 10), point(6, 7), point(8, 4)],
                      fill=(193, 132, 224, 255), width=max(1, round(s)))
            draw.point(point(8, 7), fill=(241, 203, 114, 255))
        elif "_side" in name:
            draw.line([point(7, 3), point(9, 5), point(7, 7), point(9, 9), point(7, 12)],
                      fill=(130, 82, 174, 255), width=max(1, round(2*s)))
            draw.point(point(10, 4), fill=(213, 171, 103, 255))
            draw.point(point(6, 11), fill=(213, 171, 103, 255))
        else:
            draw.line([point(4, 5), point(7, 8), point(4, 11)],
                      fill=(63, 48, 84, 255), width=max(1, round(s)))
            draw.line([point(12, 5), point(9, 8), point(12, 11)],
                      fill=(63, 48, 84, 255), width=max(1, round(s)))
    elif "furnace_front" in name:
        lit = "_lit" in name
        draw.rectangle(box(10, 13, 53, 55), fill=(63, 62, 60, 255), outline=(181, 177, 164, 255), width=max(1, round(3*s)))
        draw.line([point(13, 16), point(49, 16), point(49, 50)], fill=(224, 219, 201, 255), width=max(1, round(2*s)))
        draw.rectangle(box(17, 21, 47, 47), fill=(20, 19, 18, 255), outline=(91, 87, 81, 255), width=max(1, round(2*s)))
        draw.rectangle(box(23, 36, 41, 43), fill=(54, 43, 35, 255))
        if lit:
            draw.polygon([point(25, 42), point(28, 35), point(31, 29), point(34, 36), point(37, 42)], fill=(245, 119, 32, 255))
            draw.polygon([point(28, 42), point(30, 37), point(32, 33), point(35, 42)], fill=(255, 194, 67, 255))
    elif "dispenser_front" in name or "dropper_front" in name:
        center = round(size/2)
        radius = round(size*0.24)
        draw.ellipse((center-radius-3, center-radius-3, center+radius+3, center+radius+3), fill=(55, 55, 53, 255), outline=(183, 177, 162, 255), width=max(1, round(3*s)))
        draw.ellipse((center-radius, center-radius, center+radius, center+radius), fill=(29, 30, 29, 255), outline=(108, 105, 99, 255), width=max(1, round(2*s)))
        draw.ellipse((center-round(3*s), center-round(3*s), center+round(3*s), center+round(3*s)), fill=(131, 129, 122, 255))
    elif "workbench_top" in name:
        draw.rectangle(box(6, 6, 57, 57), outline=(81, 53, 32, 255), width=max(1, round(3*s)))
        for x in (22, 39):
            draw.line([point(x, 9), point(x, 54)], fill=(86, 56, 33, 255), width=max(1, round(2*s)))
        for y in (22, 39):
            draw.line([point(9, y), point(54, y)], fill=(86, 56, 33, 255), width=max(1, round(2*s)))
    elif "workbench" in name or "crafting_table" in name:
        draw.rectangle(box(8, 13, 55, 52), fill=(121, 76, 43, 255), outline=(65, 44, 29, 255), width=max(1, round(3*s)))
        draw.rectangle(box(14, 20, 49, 24), fill=(196, 150, 87, 255))
        draw.rectangle(box(28, 29, 36, 49), fill=(89, 56, 34, 255))
        draw.line([point(17, 31), point(25, 42)], fill=(207, 174, 112, 255), width=max(1, round(2*s)))
        draw.line([point(25, 31), point(17, 42)], fill=(207, 174, 112, 255), width=max(1, round(2*s)))
    elif "bookshelf" in name:
        draw.rectangle(box(4, 5, 59, 58), fill=(104, 65, 38, 255), outline=(62, 39, 26, 255), width=max(1, round(4*s)))
        palette = ((156, 54, 46, 255), (56, 104, 136, 255), (203, 157, 58, 255), (73, 121, 70, 255), (126, 76, 147, 255))
        for row, y in enumerate((9, 32)):
            x = 9
            while x < 55:
                width = 5 + ((x*7 + seed + row*3) % 6)
                draw.rectangle(box(x, y, min(55, x+width), y+18), fill=palette[(x+row+seed) % len(palette)], outline=(53, 43, 34, 255), width=max(1, round(s)))
                draw.line([point(x+2, y+3), point(x+2, y+14)], fill=(230, 209, 156, 255), width=max(1, round(s)))
                x += width + 1
    elif "anvil" in name:
        if "top" in name:
            draw.rectangle(box(5, 7, 58, 56), fill=(105, 112, 116, 255), outline=(54, 59, 62, 255), width=max(1, round(3*s)))
            draw.line([point(12, 16), point(50, 16)], fill=(174, 180, 181, 255), width=max(1, round(2*s)))
        else:
            draw.rectangle(box(11, 9, 52, 18), fill=(115, 121, 125, 255), outline=(54, 59, 62, 255), width=max(1, round(2*s)))
            draw.polygon([point(19, 19), point(44, 19), point(39, 43), point(26, 43)], fill=(91, 98, 102, 255))
            draw.rectangle(box(15, 44, 48, 53), fill=(65, 70, 73, 255), outline=(151, 157, 159, 255), width=max(1, round(2*s)))
    elif "chest" in name:
        front = "front" in name
        draw.rectangle(box(7, 12, 56, 53), fill=(143, 91, 48, 255), outline=(66, 43, 28, 255), width=max(1, round(3*s)))
        draw.rectangle(box(9, 16, 54, 24), fill=(183, 125, 67, 255))
        draw.line([point(9, 27), point(54, 27)], fill=(80, 51, 31, 255), width=max(1, round(3*s)))
        draw.rectangle(box(27, 23, 37, 34), fill=(196, 153, 68, 255) if front else (103, 68, 39, 255), outline=(66, 43, 28, 255), width=max(1, round(2*s)))
    return image


COMPOSITE_SIDE_ROLES = frozenset(("grass_side", "dry_grass_side", "snow_side", "litter_side"))


def texture_palette(role: str, size: int) -> int | None:
    """Limit 16px tiles while retaining extra room for layered side faces."""
    if size != 16:
        return None
    if role in COMPOSITE_SIDE_ROLES:
        # Composite grass/snow/litter faces use several semantic colors. Let
        # them retain more color steps than ordinary materials while keeping
        # the pack's RGBA palette budget bounded.
        return 128
    return 16


def default_texture_seam(family: str, material: str, size: int, role: str = "") -> int:
    """Choose seam treatment without imposing a repeated border on stationary grain."""
    if role == "water_surface":
        # Water fields are generated toroidally; blending the borders again
        # makes a visible grid in a large, flat surface.
        return 0
    if size <= 16 and family == "soil" and material in SAND_MATERIALS:
        # These fields are periodic by construction. Blending the wrapped edge
        # into a shared border texel creates a visible grid every 16 world units.
        return 0
    return max(2, size // 16)


def wrap_edge_contrast(image: Image.Image, axis: str) -> tuple[float, float]:
    """Return wrapped-edge and interior RGB steps over the rendered composite."""
    rgba = image.convert("RGBA")
    backdrop = Image.new("RGBA", rgba.size, (128, 128, 128, 255))
    pixels = Image.alpha_composite(backdrop, rgba).convert("RGB").load()
    width, height = rgba.size

    def difference(a: tuple[int, int, int], b: tuple[int, int, int]) -> float:
        return sum(abs(a[channel] - b[channel]) for channel in range(3)) / 3.0

    if axis == "x":
        boundary = [difference(pixels[0, y], pixels[width - 1, y]) for y in range(height)]
        interior = [
            difference(pixels[x, y], pixels[x + 1, y])
            for y in range(height) for x in range(width - 1)
        ]
    else:
        boundary = [difference(pixels[x, 0], pixels[x, height - 1]) for x in range(width)]
        interior = [
            difference(pixels[x, y], pixels[x, y + 1])
            for x in range(width) for y in range(height - 1)
        ]
    return statistics.mean(boundary), statistics.mean(interior)


def texture_wrap_is_continuous(image: Image.Image, axis: str) -> bool:
    """Allow a natural periodic texel transition; reject an obvious boundary seam."""
    wrapped, interior = wrap_edge_contrast(image, axis)
    # A 16-texel boundary sample is much smaller than the interior sample;
    # allow its expected small-sample variation while still rejecting a sharp
    # one- or two-pixel border.
    return wrapped <= max(interior * 1.75, 4.0)


def periodize(
    image: Image.Image,
    size: int = 16,
    seam: int = 0,
    palette: int | None = None,
    axes: tuple[str, ...] = ("x", "y"),
) -> Image.Image:
    """Resize and, when needed, blend a narrow wrapped seam at the tile center."""
    image = image.convert("RGBA")
    method = Image.Resampling.NEAREST if size <= 16 else Image.Resampling.LANCZOS
    if image.size != (size, size):
        image = ImageOps.fit(image, (size, size), method=method)
    if palette:
        alpha = image.getchannel("A")
        rgb = image.convert("RGB").quantize(colors=palette, method=Image.Quantize.MEDIANCUT).convert("RGB")
        rgb.putalpha(alpha)
        image = rgb
    if seam <= 0:
        return image

    # Move the original borders to the middle, soften only the cut there, and
    # shift back. This avoids flattening a permanent border band of every tile.
    offset_x = size // 2 if "x" in axes else 0
    offset_y = size // 2 if "y" in axes else 0
    shifted = ImageChops.offset(image, offset_x, offset_y)
    px = shifted.load()
    cx = size // 2
    cy = size // 2
    radius = min(seam, max(1, size // 8))
    if "x" in axes:
        for y in range(size):
            for d in range(radius):
                left, right = max(0, cx - 1 - d), min(size - 1, cx + d)
                a, b = px[left, y], px[right, y]
                mix = tuple(round((a[k] + b[k]) / 2) for k in range(4))
                blend = min(1.0, d / radius)
                px[left, y] = tuple(round(mix[k] * (1-blend) + a[k] * blend) for k in range(4))
                px[right, y] = tuple(round(mix[k] * (1-blend) + b[k] * blend) for k in range(4))
    if "y" in axes:
        for x in range(size):
            for d in range(radius):
                top, bottom = max(0, cy - 1 - d), min(size - 1, cy + d)
                a, b = px[x, top], px[x, bottom]
                mix = tuple(round((a[k] + b[k]) / 2) for k in range(4))
                blend = min(1.0, d / radius)
                px[x, top] = tuple(round(mix[k] * (1-blend) + a[k] * blend) for k in range(4))
                px[x, bottom] = tuple(round(mix[k] * (1-blend) + b[k] * blend) for k in range(4))
    result = ImageChops.offset(shifted, -offset_x, -offset_y)
    if palette:
        alpha = result.getchannel("A")
        rgb = result.convert("RGB").quantize(colors=palette, method=Image.Quantize.MEDIANCUT).convert("RGB")
        rgb.putalpha(alpha)
        result = rgb
    return result


def periodize_seamless(
    image: Image.Image,
    family: str,
    material: str,
    size: int,
    role: str,
    axes: tuple[str, ...],
) -> tuple[Image.Image, int]:
    """Use the smallest seam blend that passes the rendered wrap check."""
    palette = texture_palette(role, size)
    seam = default_texture_seam(family, material, size, role)
    result = periodize(image, size, seam, palette, axes)
    if seam == 0 and size <= 16:
        for candidate_seam in range(1, max(1, size // 8) + 1):
            if all(texture_wrap_is_continuous(result, axis) for axis in axes):
                break
            seam = candidate_seam
            result = periodize(image, size, seam, palette, axes)
    return result, seam


def image_pixel_hash(image: Image.Image) -> str:
    return hashlib.sha256(image.convert("RGBA").tobytes()).hexdigest()


def texture_statistics(image: Image.Image) -> dict[str, float]:
    """Return coarse, non-reconstructive traits for aggregate calibration."""
    rgba = image.convert("RGBA")
    # Compare what the texture contributes over a neutral world surface.
    # Reading RGB under fully transparent pixels unfairly penalizes cutouts
    # and glass, whose hidden color channels are not rendered.
    backdrop = Image.new("RGBA", rgba.size, (128, 128, 128, 255))
    rgb = Image.alpha_composite(backdrop, rgba).convert("RGB")
    stat = ImageStat.Stat(rgb)
    px = rgb.load()
    width, height = rgb.size
    neighbor_x = 0.0
    neighbor_y = 0.0
    luminance = [0.0] * (width * height)
    for y in range(height):
        for x in range(width):
            a = px[x, y]
            luminance[y * width + x] = a[0] * 0.2126 + a[1] * 0.7152 + a[2] * 0.0722
            b = px[(x + 1) % width, y]
            c = px[x, (y + 1) % height]
            neighbor_x += sum(abs(a[i] - b[i]) for i in range(3)) / 3.0
            neighbor_y += sum(abs(a[i] - c[i]) for i in range(3)) / 3.0
    pixel_count = max(1, width * height)
    neighbor_x /= pixel_count
    neighbor_y /= pixel_count
    neighbor_delta = (neighbor_x + neighbor_y) * 0.5
    coarse_means: list[float] = []
    coarse_step = max(2, min(width, height) // 4)
    for y0 in range(0, height, coarse_step):
        for x0 in range(0, width, coarse_step):
            samples = [luminance[y * width + x]
                       for y in range(y0, min(height, y0 + coarse_step))
                       for x in range(x0, min(width, x0 + coarse_step))]
            if samples:
                coarse_means.append(statistics.mean(samples))
    coarse_contrast = statistics.pstdev(coarse_means) if len(coarse_means) > 1 else 0.0
    alpha = rgba.getchannel("A")
    return {
        "mean_rgb_r": float(stat.mean[0]),
        "mean_rgb_g": float(stat.mean[1]),
        "mean_rgb_b": float(stat.mean[2]),
        "luminance_std": float(statistics.pstdev(luminance)) if len(luminance) > 1 else 0.0,
        "neighbor_contrast": neighbor_delta,
        "neighbor_contrast_x": neighbor_x,
        "neighbor_contrast_y": neighbor_y,
        "coarse_contrast": coarse_contrast,
        "alpha_coverage": sum(1 for value in alpha.getdata() if value >= 128) / max(1, width * height),
    }


def derive_reference_calibration(reference_pack: Path) -> dict[str, Any]:
    """Aggregate image traits by family; never returns or stores source pixels."""
    texture_dir = reference_pack / "textures" / "blocks"
    if not texture_dir.is_dir():
        raise ValueError(f"reference pack has no textures/blocks directory: {reference_pack}")
    families: dict[str, list[dict[str, float]]] = defaultdict(list)
    source_textures: dict[str, dict[str, float]] = {}
    source_hashes: dict[str, str] = {}
    for path in sorted(texture_dir.glob("*.png")):
        family = infer_family(path.stem)
        with Image.open(path) as image:
            stats = texture_statistics(image)
        families[family].append(stats)
        source_textures[path.stem] = stats
        source_hashes[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    if not families:
        raise ValueError(f"reference pack has no PNG textures: {reference_pack}")
    summary: dict[str, dict[str, float]] = {}
    for family, samples in sorted(families.items()):
        keys = samples[0].keys()
        values = {key: statistics.median(sample[key] for sample in samples) for key in keys}
        # Calibration consists only of broad numeric summaries. It cannot
        # reconstruct an original tile or extract its patches.
        contrast = values["neighbor_contrast"]
        luminance = values["luminance_std"]
        values["detail_scale"] = max(0.65, min(1.45, contrast / 22.0))
        values["contrast_scale"] = max(0.7, min(1.35, luminance / 48.0))
        values["coarse_scale"] = max(0.65, min(1.4, values["coarse_contrast"] / 18.0))
        values["micro_scale"] = max(0.65, min(1.4, contrast / 20.0))
        summary[family] = values
    license_path = reference_pack / "pack.json"
    source_license = "UNKNOWN"
    if license_path.is_file():
        try:
            source_license = json.loads(license_path.read_text(encoding="utf-8-sig")).get("license", "UNKNOWN")
        except (OSError, json.JSONDecodeError):
            pass
    return {
        "schema_version": 1,
        "mode": "aggregate-calibration",
        "source_pack": reference_pack.name,
        "source_license": source_license,
        "source_file_count": len(source_hashes),
        "source_files_sha256": source_hashes,
        "source_textures": source_textures,
        "source_pixels_used_as_output": False,
        "families": summary,
    }


def calibration_for_family(calibration: dict[str, Any] | None, family: str) -> dict[str, float] | None:
    if not calibration:
        return None
    values = calibration.get("families", {}).get(family)
    if not isinstance(values, dict):
        return None
    return {key: float(value) for key, value in values.items() if isinstance(value, (int, float))}


def calibration_for_texture(
    calibration: dict[str, Any] | None, source_stem: str, family: str,
) -> dict[str, float] | None:
    """Combine family calibration with pixel-free stats for one named source tile."""
    values = calibration_for_family(calibration, family) or {}
    texture_stats = (calibration or {}).get("source_textures", {}).get(source_stem)
    if isinstance(texture_stats, dict):
        values.update({key: float(value) for key, value in texture_stats.items() if isinstance(value, (int, float))})
        detail_reference = (
            8.4 if family == "soil" and any(token in source_stem.lower() for token in ("sand", "silt"))
            else 22.0
        )
        values["detail_scale"] = max(0.65, min(1.45, values["neighbor_contrast"] / detail_reference))
        values["contrast_scale"] = max(0.7, min(1.35, values["luminance_std"] / 48.0))
        values["coarse_scale"] = max(0.65, min(1.4, values.get("coarse_contrast", 18.0) / 18.0))
        values["micro_scale"] = max(0.65, min(1.4, values["neighbor_contrast"] / 20.0))
        values["color_weight"] = 0.75
    return values or None


def save_texture_if_changed(image: Image.Image, path: Path) -> None:
    if path.is_file():
        try:
            with Image.open(path) as existing:
                if image_pixel_hash(existing) == image_pixel_hash(image):
                    return
        except Exception:
            pass
    image.save(path, format="PNG", optimize=True)


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


def remap_block_textures(block: dict[str, Any], replacements: dict[str, str]) -> dict[str, Any]:
    adapted = copy.deepcopy(block)
    source_stems = adapted.get("textures") or []
    missing = sorted({stem for stem in source_stems if stem not in replacements})
    if missing:
        raise ValueError(f"{block.get('name', '<unknown>')}: no replacement texture for {', '.join(missing)}")
    adapted["textures"] = [replacements[stem] for stem in source_stems]
    return adapted


def ensure_stable_block_ids(blocks: list[dict[str, Any]], reserved_start: int = 1025) -> list[dict[str, Any]]:
    """Copy definitions and assign deterministic IDs where a legacy source omitted one."""
    result = copy.deepcopy(blocks)
    used_ids = {
        block["id"]
        for block in result
        if isinstance(block.get("id"), int) and 1 <= block["id"] < reserved_start
    }
    next_id = max(used_ids, default=0) + 1
    for block in result:
        if isinstance(block.get("id"), int) and 1 <= block["id"] < reserved_start:
            continue
        while next_id in used_ids and next_id < reserved_start:
            next_id += 1
        if next_id >= reserved_start:
            raise ValueError(f"no stable block IDs remain below reserved range {reserved_start}")
        block["id"] = next_id
        used_ids.add(next_id)
        next_id += 1
    return result


def stable_pack_block_ids(
    names: list[str], previous_ids: dict[str, int], reserved_ids: set[int],
    reserved_start: int = 1025, max_id: int = 4095,
) -> dict[str, int]:
    """Keep published IDs attached to names and allocate free IDs for additions."""
    used_ids = {block_id for block_id in reserved_ids if 1 <= block_id <= max_id}
    result: dict[str, int] = {}
    for name in names:
        previous_id = previous_ids.get(name)
        if (
            isinstance(previous_id, int)
            and reserved_start <= previous_id <= max_id
            and previous_id not in used_ids
        ):
            result[name] = previous_id
            used_ids.add(previous_id)
    next_id = reserved_start
    for name in names:
        if name in result:
            continue
        while next_id in used_ids and next_id <= max_id:
            next_id += 1
        if next_id > max_id:
            raise ValueError(f"generated block catalog exceeds the stable pack ID range {reserved_start}..{max_id}")
        result[name] = next_id
        used_ids.add(next_id)
        next_id += 1
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
    if "waterlily" in value:
        return "organic"
    if "sand_with_kelp" in value or "dirt_with_kelp" in value:
        return "soil"
    if "mycel" in value:
        return "soil"
    if "cloud" in value:
        return "organic"
    if "default_grass" in value:
        return "organic"
    if any(token in value for token in ("dirt_with_dry_grass", "litter")):
        return "soil"
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
    if any(token in value for token in ("grass", "fern", "leaf", "leaves", "flower", "sapling", "mushroom", "cactus", "vine", "crop", "cocoa", "coral", "reed", "moss", "lichen", "bush", "shrub", "plant", "needle", "apple", "berry", "melon", "pumpkin", "potato", "carrot", "wheat", "lily", "papyrus", "deadbush", "mycel", "sponge", "stem")):
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
    if family == "glass" and "obsidian" in words:
        selected = "smoke"
    if family == "ceramic" and (any(t in words for t in ("fire", "lava", "torch", "glowstone", "lit")) or "redtorch" in clean):
        selected = "scarlet" if "fire" in words or "torch" in words or "redtorch" in clean else "amber"
    if family == "glass" and any(t in words for t in ("water", "river", "flowing")):
        selected = "cyan" if "cyan" in materials else "clear"
    if family == "glass" and any(t in words for t in ("ice", "frost", "snow")):
        selected = "clear"
    if family == "fabric":
        cloth_index = re.search(r"(?:cloth|wool|carpet)[_-]?(\d+)", clean)
        cloth_palette = ("ivory", "charcoal", "charcoal", "ochre", "slate", "umber", "ivory", "ivory", "charcoal", "cream", "black", "umber", "umber", "ivory", "tan", "charcoal")
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
        # Minetest mod texture names often concatenate species and object
        # names (for example "jungletree") instead of separating them with
        # underscores. Match those documented aliases explicitly so tree
        # bark never falls through to a random wood species.
        wood_aliases = (
            ("redwood", ("redwood",)), ("mangrove", ("mangrove",)),
            ("jungle", ("jungle", "jungletree", "junglewood")),
            ("birch", ("birch",)), ("cedar", ("cedar",)),
            ("maple", ("maple",)), ("walnut", ("walnut",)),
            ("beech", ("beech",)), ("willow", ("willow",)),
            ("poplar", ("poplar",)), ("acacia", ("acacia",)),
            ("larch", ("larch",)), ("aspen", ("aspen",)),
            ("pine", ("pine", "spruce", "conifer")),
            ("ash", ("ash",)), ("oak", ("oak",)),
        )
        selected = next((
            material
            for material, aliases in wood_aliases
            if material in materials and any(alias in words for alias in aliases)
        ), None)
        if selected is None and "aspen" in words and ("wood", "aspen") in MATERIAL_TINTS:
            # Aspen is also used by the legacy reference set. Keep its
            # distinct bark tint even when the block catalog has no aspen
            # family of its own.
            selected = "aspen"
        selected = selected or "oak"
    elif family == "soil":
        selected = (
            "desert_sand" if "desert" in words or "hellsand" in words else
            "red_earth" if "red" in words and "sand" in words else
            "pale_sand" if "sand" in words or "snow" in words else
            "red_earth" if "red" in words else
            "clay" if "clay" in words else
            "peat" if "peat" in words else
            "loam"
        )
    elif family == "rock":
        selected = "chalk" if "white" in words else next((m for m in materials if m in words), "obsidian" if "bedrock" in words else "basalt" if "stone" in words else "granite")
    elif family == "masonry":
        selected = next((m for m in materials if m in words), "brick" if "brick" in words else "cobble" if "cobble" in words else "granite")
    elif family == "metal":
        selected = next((m for m in materials if m in words), "gold" if "gold" in words else "copper" if "copper" in words else "iron")
    elif family == "crystal":
        selected = (
            "emerald" if "block_emerald" in clean else
            "citrine" if "block_redstone" in clean else
            "jade" if "emerald" in words else
            "azurite" if "lapis" in words else
            "amethyst" if any(word.startswith("enchant") for word in words) else
            "garnet" if "ruby" in words or "redstone" in words else
            "citrine" if "gold" in words or "mese" in words or "lightgem" in words else
            "fluorite" if "diamond" in words else
            next((m for m in materials if m in words), "quartz")
        )
    elif family == "organic":
        if "blueberry" in clean and any(token in words for token in ("leaves", "bush", "sapling")):
            # Blueberry foliage is green; blue belongs only on berry-bearing
            # variants, not on every leaf pixel as the flower fallback did.
            selected = "leaf"
        elif "cloud" in words:
            selected = "flower_white"
        elif "shrub" in words or "deadbush" in words:
            selected = "lichen"
        elif "flower" in words or "rose" in words or "dandelion" in words:
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
                "needle" if any(t in words for t in ("spruce", "pine", "conifer", "needle")) else
                "leaf" if "leaves" in words else
                next((m for m in ("coral", "kelp", "reed", "fern", "moss", "lichen", "leaf") if m in words), "leaf")
            )
    elif family in ("fabric", "ceramic", "glass") and selected is None:
        selected = {"fabric": "tan", "ceramic": "cream", "glass": "clear"}[family]
    if selected not in materials and not (family == "wood" and (family, selected) in MATERIAL_TINTS):
        selected = materials[stable_seed(clean + family) % len(materials)]
    variant = "natural"
    if family == "soil" and "mycel" in words:
        variant = "rooted"
    elif family == "ceramic" and ("torch" in words or "redtorch" in clean):
        variant = "glossy"
    elif family == "crystal" and ("block_emerald" in clean or "block_redstone" in clean):
        variant = "bright"
    elif family == "wood":
        explicit_variant = next((
            candidate for candidate in (
                "stripped", "aged", "dark", "pale", "carved", "cut",
                "weathered",
            ) if candidate in words
        ), None)
        trunk_texture = (
            any(token in words for token in ("bark", "log", "tree"))
            or any(token.endswith("tree") for token in words)
        )
        # `_side` occurs on workbenches, chests, bookshelves and planks as
        # often as it does on logs. Reserve bark/endgrain for named trunks;
        # crafted wood faces use the plank grain unless explicitly labeled.
        variant = (
            explicit_variant if explicit_variant is not None else
            "endgrain" if trunk_texture and any(t in words for t in ("top", "end")) else
            "bark" if trunk_texture else
            "plank"
        )
    elif family == "masonry" and "brick" in words:
        variant = "brick"
    elif family == "masonry" and "cobble" in words:
        variant = "cobble"
    elif family in ("rock", "masonry") and any(t in words for t in ("smooth", "polished")):
        variant = "polished" if "polished" in variants else "cut"
    elif family == "glass":
        variant = (
            "prism" if "obsidian" in words else
            "frosted" if any(t in words for t in ("ice", "frost", "snow")) else
            "plain"
        )
    elif family == "metal":
        variant = "polished" if "gold" in words or "iron" in words else "raw"
    elif family == "organic":
        variant = (
            "spotted" if "blueberry" in clean and "with_berries" in clean and "spotted" in variants else
            "dry" if "shrub" in words or "deadbush" in words else
            "bright" if any(t in words for t in ("flower", "torch", "glowstone")) else
            "fresh"
        )
    if variant not in variants:
        variant = variants[stable_seed(family + clean) % len(variants)]
    return selected, variant


def source_texture_role(source_stem: str, family: str) -> str:
    value = source_stem.lower()
    if "water" in value and "waterlily" not in value:
        return "water_surface"
    if "cactus" in value and "seedling" not in value:
        if "top" in value or value.endswith("_top"):
            return "cactus_top"
        if "bottom" in value or value.endswith("_bottom"):
            return "cactus_bottom"
        return "cactus_side"
    if "sand_with_kelp" in value or "dirt_with_kelp" in value:
        return "side"
    if is_leaf_material(family, "", value):
        return "leaf_cutout"
    if value.endswith("default_dry_grass") or value.endswith("default_dry_grass_top"):
        return "dry_grass_top"
    if value.endswith("default_coniferous_litter") or value.endswith("default_rainforest_litter") or value.endswith("litter_top"):
        return "litter_top"
    if value.endswith("default_snow") or value in ("snow", "snow_top"):
        return "snow_top"
    if value.endswith("default_grass") or value == "default_grass" or value.endswith("grass_top"):
        return "grass_top"
    if any(token in value for token in ("dirt_with_snow", "snow_side", "snow_composite")):
        return "snow_side"
    if any(token in value for token in ("dirt_with_dry_grass", "dry_grass_side")):
        return "dry_grass_side"
    if any(token in value for token in ("coniferous_litter_composite", "rainforest_litter_composite")):
        return "litter_side"
    if any(token in value for token in ("dirt_with_grass", "grass_side", "grass_with_dirt", "grass_composite")):
        return "grass_side"
    if is_decor_sprite_source(source_stem):
        return "decor_sprite"
    if any(token in value for token in ("ore", "stone_with_", "mineral")):
        return "ore"
    if family == "wood" and any(token in value for token in ("top", "end", "tree_end")):
        trunk_texture = any(token in value for token in ("bark", "log", "tree"))
        return "endgrain" if trunk_texture else "top"
    if family == "soil" and any(token in value for token in ("snow", "grass_top")):
        return "top"
    return "side"


def block_face_textures(record: dict[str, str], base_stem: str) -> tuple[list[str], list[tuple[str, str]]]:
    """Map material semantics to the renderer's 0..3 sides, 4 top, 5 bottom."""
    family = record["family"]
    material = record["material"]
    variant = record["variant"]
    recipe = load_catalog().get("face_recipes", {}).get(family, {})
    split = bool(recipe.get("split"))
    split = split or variant in recipe.get("split_variants", [])
    split = split or material in recipe.get("split_materials", [])
    if split:
        derived: list[tuple[str, str]] = []
        face_stems = [base_stem] * 4
        for index, face in ((4, "top"), (5, "bottom")):
            role = recipe.get(face, face)
            if family == "wood" and variant in ("bark", "endgrain", "stripped"):
                role = "endgrain"
            if family == "organic" and role == "grass_top" and material != "moss":
                role = "top"
            if role == "side":
                face_stems.append(base_stem)
                continue
            suffix = "endgrain" if role == "endgrain" else face
            stem = f"{base_stem}_{suffix}"
            face_stems.append(stem)
            derived.append((stem, role))
        return face_stems, derived
    return [base_stem] * 6, []


def make_replacement_texture(
    source_stem: str,
    target_stem: str,
    family: str,
    kind: str,
    output_dir: Path,
    hashes: set[str],
    frame_count: int = 1,
    calibration: dict[str, Any] | None = None,
    size: int = DEFAULT_TEXTURE_SIZE,
    review_required: bool = False,
) -> dict[str, Any]:
    material, variant = material_variant(source_stem, family)
    role = source_texture_role(source_stem, family)
    if family == "soil" and role == "snow_side":
        material, variant = "loam", "natural"
    elif family == "soil" and role in ("dry_grass_side", "dry_grass_top"):
        material, variant = "dry_mud", "dry"
    elif family == "soil" and role in ("litter_side", "litter_top"):
        material, variant = "peat", "rooted"
    base_seed = stable_seed(f"cubatarium:{kind}:{source_stem}:{family}")
    reference_pack = (
        "minetest_default_16"
        if kind == "minetest_reference_only"
        else "cubatarium_geology_cc0"
    )
    leaf_reference_transparency = (
        reference_leaf_transparency(source_stem) if role == "leaf_cutout" else None
    )
    # Minetest's default and spruce masks leave too few openings for a dense
    # canopy (16% and 12% transparent). Preserve a more open minimum for our
    # authored replacements while retaining the source measurement in QA data.
    leaf_transparency = (
        max(MIN_REPLACEMENT_LEAF_TRANSPARENCY,
            leaf_reference_transparency)
        if leaf_reference_transparency is not None else None
    )
    sprite_alpha_reference_coverage = (
        reference_alpha_coverage(source_stem, reference_pack)
        if role == "decor_sprite" else None
    )
    sprite_alpha_coverage = semantic_sprite_alpha_coverage(
        source_stem, sprite_alpha_reference_coverage
    )

    def make_sprite(texture_seed: int) -> Image.Image | None:
        sprite_image = procedural_decor_sprite(source_stem, size, texture_seed)
        if (sprite_image is not None and sprite_alpha_coverage is not None
                and 0.02 <= sprite_alpha_coverage < 0.98):
            sprite_image = fit_alpha_coverage(
                sprite_image, sprite_alpha_coverage, texture_seed
            )
        return sprite_image

    cal = calibration_for_texture(calibration, source_stem, family)
    image = generated_texture(family, material, variant, base_seed, size, role, cal)
    glass = (
        procedural_glass_texture(source_stem, size, material, base_seed)
        if family == "glass" and role != "water_surface" else None
    )
    if glass is not None:
        image = glass
    coral = procedural_coral_skeleton(source_stem, size, base_seed)
    if coral is not None:
        image = coral
    sprite = make_sprite(base_seed) if role == "decor_sprite" else None
    if sprite is not None:
        image = sprite
    image = add_semantic_face_detail(image, source_stem, base_seed)
    seam_width = 0
    if role != "decor_sprite":
        axes = ("x",) if role in COMPOSITE_SIDE_ROLES else ("x", "y")
        image, seam_width = periodize_seamless(
            image, family, material, size, role, axes
        )
    if role == "leaf_cutout":
        if material == "needle":
            image = apply_needle_cutout(
                image, base_seed, leaf_transparency or 0.38, variant
            )
        else:
            image = apply_leaf_cutout(image, base_seed, leaf_transparency or 0.38)
    digest = image_pixel_hash(image)
    salt = 0
    while digest in hashes:
        salt += 1
        if salt > 512:
            raise RuntimeError(f"could not make a unique texture for {source_stem} after 512 deterministic variants")
        image = generated_texture(family, material, f"{variant}_{salt}", base_seed + salt, size, role, cal)
        glass = (
            procedural_glass_texture(source_stem, size, material, base_seed + salt)
            if family == "glass" and role != "water_surface" else None
        )
        if glass is not None:
            image = glass
        coral = procedural_coral_skeleton(source_stem, size, base_seed + salt)
        if coral is not None:
            image = coral
        sprite = make_sprite(base_seed + salt) if role == "decor_sprite" else None
        if sprite is not None:
            image = sprite
        image = add_semantic_face_detail(image, source_stem, base_seed + salt)
        if role != "decor_sprite":
            axes = ("x",) if role in COMPOSITE_SIDE_ROLES else ("x", "y")
            image, seam_width = periodize_seamless(
                image, family, material, size, role, axes
            )
        if role == "leaf_cutout":
            if material == "needle":
                image = apply_needle_cutout(
                    image, base_seed + salt, leaf_transparency or 0.38,
                    variant,
                )
            else:
                image = apply_leaf_cutout(
                    image, base_seed + salt, leaf_transparency or 0.38
                )
        digest = image_pixel_hash(image)
    hashes.add(digest)
    if frame_count > 1:
        # Animate Cubatarium-authored water and lava as a wrapped scrolling
        # tile. ImageChops.offset wraps edges, so every frame remains tileable
        # and the final frame transitions continuously back to the first.
        motion_x, motion_y = ((1, 0) if "water" in source_stem.lower() else (1, 1))
        strip = Image.new("RGBA", (image.width, image.height * frame_count))
        for frame_index in range(frame_count):
            if role == "decor_sprite":
                sprite_frame = make_sprite(base_seed + frame_index)
                frame = sprite_frame if sprite_frame is not None else image
            else:
                shifted = ImageChops.offset(image, motion_x * frame_index,
                                            motion_y * frame_index)
                frame = (
                    shifted if role == "water_surface"
                    else periodize(shifted, image.width, 1, 16 if size == 16 else None)
                )
            strip.paste(frame, (0, frame_index * image.height))
        image = strip
    save_texture_if_changed(image, texture_path(output_dir, target_stem))
    digest = image_pixel_hash(image)
    hashes.add(digest)
    entry = {
        "stem": target_stem,
        "family": family,
        "material": material,
        "variant": variant,
        "role": role,
        "seed": base_seed,
        "backend": "procedural",
        "seam_blend_width": seam_width,
        "reference_mode": calibration.get("mode", "qa-only") if calibration else "qa-only",
        "resolution": [size, size],
        "pixel_sha256": digest,
        "source": {
            "kind": kind,
            "reference_only": source_stem,
            "source_license": "CC-BY-SA-3.0" if kind == "minetest_reference_only" else "CC0-1.0",
            "reference_file_sha256": hashlib.sha256(
                (REPO / "resource_packs" / ("minetest_default_16" if kind == "minetest_reference_only" else "cubatarium_geology_cc0") / "textures" / "blocks" / f"{source_stem}.png").read_bytes()
            ).hexdigest(),
        },
        "license": "REVIEW_REQUIRED" if review_required else "CC0-1.0",
        "license_review_required": review_required,
    }
    if leaf_transparency is not None:
        entry["alpha_mask_reference_transparency"] = round(
            leaf_reference_transparency or 0.0, 4
        )
        entry["alpha_mask_target_transparency"] = round(leaf_transparency, 4)
        entry["alpha_mask_reference_pack"] = "minetest_default_16"
    if sprite_alpha_coverage is not None and 0.02 <= sprite_alpha_coverage < 0.98:
        entry["sprite_alpha_reference_coverage"] = round(
            sprite_alpha_reference_coverage or 0.0, 4
        )
        entry["sprite_alpha_target_coverage"] = round(sprite_alpha_coverage, 4)
        entry["sprite_alpha_reference_pack"] = reference_pack
    if frame_count > 1:
        entry["animation_frame_count"] = frame_count
    return entry


def block_definition(record: dict[str, str], face_stems: list[str], block_id: int) -> dict[str, Any]:
    family = record["family"]
    variant = record["variant"]
    hardness = {"soil": 0.65, "wood": 1.4, "fabric": 0.35, "glass": 0.45, "organic": 0.45, "ceramic": 1.1, "metal": 3.0, "crystal": 2.8}.get(family, 1.8)
    types = ["building", "natural"] if family in ("rock", "soil", "wood", "masonry") else ["building", "decoration"]
    render: dict[str, Any] = {}
    structured_surface = any(token in record["name"] for token in (
        "grass", "snow", "litter", "brick", "tile", "bark", "endgrain",
        "ore", "furnace", "workbench", "crafting_table", "anvil",
        "dispenser", "dropper", "bookshelf", "door", "log",
    ))
    randomizable_material = (
        family == "soil" or
        (family == "rock" and variant not in ("layered", "banded", "veined"))
    )
    render["texture_variation"] = (
        0.56 if family == "wood" and variant == "bark" else
        0.0 if any(token in record["name"] for token in ("furnace", "workbench", "crafting_table", "anvil", "dispenser", "dropper", "bookshelf"))
        else 1.0 if randomizable_material and not structured_surface
        else 0.48
    )
    physics: dict[str, Any] | None = None
    if family == "glass":
        render["transparent"] = True
    if is_leaf_material(family, record["material"], record["name"]):
        render.update({"style": "cutout", "texture_variation": 1.0})
        physics = {"movement": {"occupancy": 0.0}}
    mushroom_cap = record["material"].startswith("mushroom_") and record["material"] != "mushroom_stem"
    if family == "organic" and (record["material"] in ("blossom", "fern", "reed", "mushroom", "kelp", "coral") or record["material"].startswith("flower_") or mushroom_cap):
        shape = (
            "flame" if "fire" in record["name"] else
            "reed" if record["material"] in ("reed", "kelp") else
            "cluster" if record["material"] == "coral" else
            "tuft" if record["material"] == "fern" else
            "cross"
        )
        if mushroom_cap or record["material"] == "mushroom":
            shape = "cross"
        render.update({"style": "cross", "shape": shape, "transparent": True, "double_sided": True})
        render["texture_variation"] = 1.0
        physics = {"movement": {"occupancy": 0.0}}
    elif family == "organic" and record["material"] == "mushroom_stem":
        # A mushroom stem is a narrow cross-sprite decor voxel, so it keeps
        # both its transparent silhouette and pass-through movement behavior.
        render.update({"style": "cross", "transparent": True, "double_sided": True})
        physics = {"movement": {"occupancy": 0.0}}
    dig_group = "cracky" if family in ("rock", "masonry", "metal", "crystal") else "crumbly"
    definition = {
        "name": record["name"],
        "id": block_id,
        "displayName": " ".join(part.capitalize() for part in record["name"][3:].split("_")),
        "hardness": hardness,
        "dig": {"level": 0, "groups": {dig_group: 2}},
        "types": types,
        "textures": face_stems,
        "render": render,
        "assetgen": {"family": family, "material": record["material"], "variant": record["variant"]},
    }
    if physics is not None:
        definition["physics"] = physics
    return definition


def create_catalog_texture(
    record: dict[str, str], stem: str, role: str, output_dir: Path,
    hashes: set[str], calibration: dict[str, Any] | None = None,
    size: int = DEFAULT_TEXTURE_SIZE, review_required: bool = False,
) -> dict[str, Any]:
    seed = stable_seed(f"cubatarium:{record['name']}:{role}")
    family_calibration = calibration_for_family(calibration, record["family"])
    leaf_transparency = catalog_leaf_transparency(record)
    image = generated_texture(record["family"], record["material"], record["variant"], seed, size, role, family_calibration)
    glass = procedural_glass_texture(record["name"], size, record["material"], seed) if record["family"] == "glass" else None
    if glass is not None:
        image = glass
    sprite = procedural_decor_sprite(record["name"], size, seed)
    leaf_cutout = is_leaf_material(record["family"], record["material"], record["name"])
    output_role = "leaf_cutout" if leaf_cutout else "decor_sprite" if sprite is not None else role
    if sprite is not None:
        image = sprite
    image = add_semantic_face_detail(image, record["name"], seed)
    seam_width = 0
    if output_role != "decor_sprite":
        axes = ("x",) if role in COMPOSITE_SIDE_ROLES else ("x", "y")
        image, seam_width = periodize_seamless(
            image, record["family"], record["material"], size, role, axes
        )
    if leaf_cutout:
        if record["material"] == "needle":
            image = apply_needle_cutout(
                image, seed, leaf_transparency, record["variant"]
            )
        else:
            image = apply_leaf_cutout(image, seed, leaf_transparency)
    digest = image_pixel_hash(image)
    salt = 0
    while digest in hashes:
        salt += 1
        if salt > 512:
            raise RuntimeError(f"could not make a unique texture for {record['name']} after 512 deterministic variants")
        image = generated_texture(record["family"], record["material"], f"{record['variant']}_{salt}", seed + salt, size, role, family_calibration)
        glass = procedural_glass_texture(record["name"], size, record["material"], seed + salt) if record["family"] == "glass" else None
        if glass is not None:
            image = glass
        sprite = procedural_decor_sprite(record["name"], size, seed + salt)
        if sprite is not None:
            image = sprite
        image = add_semantic_face_detail(image, record["name"], seed + salt)
        if output_role != "decor_sprite":
            axes = ("x",) if role in COMPOSITE_SIDE_ROLES else ("x", "y")
            image, seam_width = periodize_seamless(
                image, record["family"], record["material"], size, role, axes
            )
        if leaf_cutout:
            if record["material"] == "needle":
                image = apply_needle_cutout(
                    image, seed + salt, leaf_transparency, record["variant"]
                )
            else:
                image = apply_leaf_cutout(image, seed + salt, leaf_transparency)
        digest = image_pixel_hash(image)
    hashes.add(digest)
    save_texture_if_changed(image, texture_path(output_dir, stem))
    entry = {
        "stem": stem,
        "family": record["family"],
        "material": record["material"],
        "variant": record["variant"],
        "role": output_role,
        "seed": seed,
        "backend": "procedural",
        "seam_blend_width": seam_width,
        "reference_mode": calibration.get("mode", "qa-only") if calibration else "qa-only",
        "resolution": [size, size],
        "pixel_sha256": digest,
        "source": {"kind": "procedural_parameters"},
        "reference_calibration": calibration.get("source_pack") if calibration else None,
        "license": "REVIEW_REQUIRED" if review_required else "CC0-1.0",
        "license_review_required": review_required,
    }
    if leaf_cutout:
        entry["alpha_mask_target_transparency"] = round(leaf_transparency, 4)
        entry["alpha_mask_reference_pack"] = "procedural_material_profile"
    return entry


def block_name(records: list[dict[str, str]], family: str, material: str, variant: str) -> str:
    wanted = f"cu_{family}_{material}_{variant}"
    for entry in records:
        if entry["name"] == wanted:
            return wanted
    # An unsupported variant must never silently turn a flower into a
    # mushroom (or a birch tree into a different wood). Keep the requested
    # material and choose a deterministic available variant instead.
    candidates = [
        r for r in records
        if r["family"] == family and r["material"] == material
    ]
    if not candidates:
        raise ValueError(f"no generated block for {family}/{material}")
    return candidates[stable_seed(wanted) % len(candidates)]["name"]


def add_object_block(blocks: list[dict[str, Any]], x: int, y: int, z: int, kind: str) -> None:
    blocks.append({"dx": x, "dy": y, "dz": z, "type": kind})


def object_document(
    name: str,
    voxels: list[dict[str, Any]],
    tags: list[str],
    seed: int,
    material: str | None = None,
    placement_mode: str | None = None,
) -> dict[str, Any]:
    source: dict[str, Any] = {"mod": "cubatarium/assetgen", "seed": seed, "license": "CC0-1.0"}
    if material:
        source["material_parameter"] = material
    document = {
        "name": name,
        "version": 2,
        "displayName": name.replace("asset_", "").replace("_", " ").title(),
        "source": source,
        "tags": tags,
        "anchor": [0, 0, 0],
        "blocks": voxels,
    }
    if placement_mode:
        document["placement"] = {"mode": placement_mode}
    return document


def make_tree(name: str, wood: str, records: list[dict[str, str]], size: int = 5) -> dict[str, Any]:
    trunk = block_name(records, "wood", wood, "bark")
    conifers = {"cedar", "larch", "pine", "redwood", "spruce"}
    foliage_material = "needle" if wood in conifers else "leaf"
    foliage_variant = {
        "acacia": "sparse", "ash": "mature", "beech": "dense",
        "birch": "young", "aspen": "fresh", "jungle": "dense", "mangrove": "dark",
        "maple": "fresh", "oak": "mature", "poplar": "pale",
        "walnut": "dark", "willow": "sparse",
        # Keep evergreen and deciduous conifers distinguishable in a scene:
        # cedar is mature green, larch has lighter new needles, pine is dark,
        # and redwood uses a dense deep-green crown.
        "cedar": "mature", "larch": "young", "pine": "dark",
        "redwood": "dense", "spruce": "fresh",
    }.get(wood, "fresh")
    leaves = block_name(records, "organic", foliage_material, foliage_variant)
    voxels: list[dict[str, Any]] = []
    height = max(size, {
        "oak": 8, "ash": 9, "beech": 8, "birch": 11, "aspen": 11,
        "cedar": 11, "larch": 10, "pine": 10, "redwood": 13,
        "spruce": 13, "jungle": 11, "maple": 8, "walnut": 8, "willow": 9,
        "poplar": 14, "acacia": 8, "mangrove": 8,
    }.get(wood, 7))
    tree_seed = stable_seed(f"tree:{wood}:{size}")
    rng = random.Random(tree_seed)
    if wood == "acacia":
        # A slanted trunk and one raised branch support a broad, flat crown.
        for y in range(height):
            x = 0 if y < height - 4 else min(2, (y - (height - 4) + 1) // 2)
            add_object_block(voxels, x, y, 0, trunk)
        for step in range(1, 3):
            add_object_block(voxels, step, height - 3 + step // 2, 0, trunk)
        center_x = 2
    else:
        center_x = 0
        trunk_width = 2 if wood in ("jungle", "redwood") else 1
        trunk_min = -(trunk_width // 2)
        for y in range(height):
            for x in range(trunk_min, trunk_min + trunk_width):
                for z in range(trunk_min, trunk_min + trunk_width):
                    add_object_block(voxels, x, y, z, trunk)

    if wood in ("mangrove", "jungle"):
        # Buttress and stilt roots make these trees read differently at ground
        # level and keep the trunk from looking like a bare vertical pole.
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            for step in range(1, 4 if wood == "mangrove" else 3):
                x = round(dx * step * (0.85 if wood == "mangrove" else 0.55))
                z = round(dz * step * (0.85 if wood == "mangrove" else 0.55))
                y = max(0, 2 - step)
                add_object_block(voxels, x, y, z, trunk)

    if wood in conifers:
        # A broad, tapered needle crown replaces the old narrow column. Larch
        # has gaps between its branch tiers; redwood keeps most foliage high.
        crown_bottom = max(2, round(height * (
            0.60 if wood == "redwood" else 0.18 if wood == "spruce" else 0.30
        )))
        radius_cap, radius_scale = {
            "cedar": (3, 3.2),
            "larch": (3, 3.2),
            "pine": (4, 3.6),
            "redwood": (3, 2.5),
            "spruce": (4, 3.6),
        }[wood]
        gap_chance = {
            "cedar": 0.025,
            "larch": 0.30,
            "pine": 0.16,
            "redwood": 0.07,
            "spruce": 0.04,
        }[wood]
        for y in range(crown_bottom, height + 1):
            progress = (height - y) / max(1, height - crown_bottom)
            radius = min(radius_cap, max(0, round(radius_scale * progress)))
            if wood == "larch" and y % 3 == 0:
                radius = max(0, radius - 1)
            if radius == 0:
                add_object_block(voxels, 0, y, 0, leaves)
                continue
            for x in range(-radius, radius + 1):
                for z in range(-radius, radius + 1):
                    if x == 0 and z == 0:
                        continue
                    if (x / max(1, radius)) ** 2 + (z / max(1, radius)) ** 2 > 1.35:
                        continue
                    if rng.random() < gap_chance:
                        continue
                    add_object_block(voxels, x, y, z, leaves)
    else:
        crown_bottom = height - {
            "oak": 4, "beech": 4, "maple": 4, "walnut": 4, "ash": 4,
            "jungle": 4, "mangrove": 4, "willow": 4,
            "birch": 5, "aspen": 7, "poplar": 8,
        }.get(wood, 3)
        profiles: dict[str, list[tuple[int, int]]] = {
            # Broadleaf species need different voxel silhouettes, not just
            # different leaf tints. The extra layer gives their crowns a
            # readable outline at the default worldgen object scale.
            "oak": [(2, 2), (3, 2), (4, 3), (4, 2), (3, 3), (1, 1)],
            "ash": [(1, 1), (2, 2), (2, 2), (3, 2), (2, 2), (1, 1)],
            "beech": [(1, 1), (2, 2), (3, 3), (3, 3), (2, 2), (1, 1)],
            "birch": [(1, 1), (1, 1), (2, 1), (2, 1), (1, 1), (1, 1)],
            "aspen": [(1, 1), (1, 1), (2, 2), (2, 1), (2, 1), (2, 1), (1, 1), (1, 1)],
            "jungle": [(2, 2), (3, 3), (4, 3), (3, 3), (2, 2)],
            "mangrove": [(2, 2), (3, 2), (3, 3), (2, 2), (1, 1)],
            "maple": [(1, 1), (2, 2), (3, 2), (3, 2), (2, 2), (1, 1)],
            "poplar": [(1, 1), (1, 1), (2, 1), (2, 1), (2, 1), (2, 1), (1, 1), (1, 1), (1, 1)],
            "walnut": [(2, 2), (4, 3), (4, 2), (3, 2), (2, 1), (1, 1)],
            "willow": [(3, 2), (4, 3), (4, 3), (3, 2), (2, 2)],
            "acacia": [(3, 2), (4, 3), (4, 3), (2, 2)],
        }
        profile = profiles.get(wood, [(2, 2), (3, 2), (2, 2), (1, 1)])
        crown_offsets = {
            # Asymmetry suggests major limbs and keeps every specimen from
            # reading as a centered sphere.
            "oak": [(0, 0), (-1, 0), (-1, 1), (0, 1), (1, 1), (1, 0)],
            "ash": [(0, 0), (0, 0), (0, 0), (1, 0), (1, 0), (1, 0)],
            "walnut": [(0, 0), (-1, 0), (-1, 1), (0, 1), (1, 1), (1, 1)],
        }.get(wood, [(0, 0)] * len(profile))
        canopy_gap = {
            "oak": 0.18, "ash": 0.24, "beech": 0.03,
            "maple": 0.12, "walnut": 0.20,
            "birch": 0.14, "poplar": 0.16,
        }.get(wood, 0.12)
        for level, (radius_x, radius_z) in enumerate(profile):
            y = crown_bottom + level
            if y > height + (1 if wood not in ("acacia",) else 0):
                break
            if wood in ("birch", "poplar") and level == len(profile) - 1:
                radius_x, radius_z = 0, 0
            offset_x, offset_z = crown_offsets[level] if level < len(crown_offsets) else (0, 0)
            for x in range(-radius_x, radius_x + 1):
                for z in range(-radius_z, radius_z + 1):
                    if x == 0 and z == 0:
                        continue
                    if radius_x and radius_z:
                        edge_limit = 1.12 if wood in ("oak", "ash", "beech", "maple", "walnut") else 1.35
                        if (x / radius_x) ** 2 + (z / radius_z) ** 2 > edge_limit:
                            continue
                    if rng.random() < (0.03 if wood == "jungle" else canopy_gap):
                        continue
                    add_object_block(voxels, x + center_x + offset_x, y, z + offset_z, leaves)

        if wood in ("oak", "ash", "maple", "walnut"):
            # Short, attached limbs break up the single-pole look and explain
            # the crown's wider or offset silhouette. They stay inside foliage
            # except for the last voxel, as on a low-resolution tree model.
            branch_layout = {
                "oak": ((1, 0, 2), (-1, 0, 2), (0, 1, 2), (0, -1, 2)),
                "ash": ((1, 0, 2), (-1, 0, 2), (0, 1, 2), (0, -1, 1)),
                "maple": ((1, 0, 3), (-1, 0, 3), (0, 1, 2), (0, -1, 2)),
                "walnut": ((1, 0, 2), (-1, 0, 2), (0, 1, 2), (0, -1, 2)),
            }[wood]
            branch_y = max(crown_bottom, height - 3)
            for dx, dz, length in branch_layout:
                for step in range(1, length + 1):
                    branch_x = center_x + dx * step
                    branch_z = dz * step
                    branch_level = branch_y + (1 if step == length and wood in ("ash", "maple", "walnut") else 0)
                    add_object_block(voxels, branch_x, branch_level, branch_z, trunk)

        if wood == "maple":
            # Four attached lobes break the rounded base into a maple-like
            # crown without flattening its top into a broad rectangular cap.
            for dx, dz in ((-4, 0), (4, 0), (0, -3), (0, 3)):
                inward_x = -1 if dx > 0 else 1 if dx < 0 else 0
                inward_z = -1 if dz > 0 else 1 if dz < 0 else 0
                for y in (height - 2, height - 1):
                    add_object_block(voxels, dx, y, dz, leaves)
                    add_object_block(voxels, dx + inward_x, y, dz + inward_z, leaves)

        if wood == "willow":
            # Willow foliage hangs below its wide crown in separated curtains.
            for x, z in ((-3, 0), (3, 0), (0, -2), (0, 2), (-2, -2), (2, 2)):
                length = rng.choice((2, 3, 4))
                for drop in range(1, length + 1):
                    add_object_block(voxels, x, height - 2 - drop, z, leaves)

    unique_voxels: dict[tuple[int, int, int], dict[str, Any]] = {}
    for voxel in voxels:
        position = (voxel["dx"], voxel["dy"], voxel["dz"])
        if position not in unique_voxels or voxel["type"] == trunk:
            unique_voxels[position] = voxel
    voxels = [unique_voxels[position] for position in sorted(unique_voxels)]
    return object_document(name, voxels, ["worldgen", "vegetation"], tree_seed, wood)


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
    if material in ("moss", "lichen"):
        # Moss and lichen spread across the ground instead of forming a cube.
        radius = rng.choice((1, 2))
        for x in range(-radius, radius + 1):
            for z in range(-radius, radius + 1):
                if rng.random() > 0.32 + 0.08 * (abs(x) + abs(z)):
                    voxel_map[(x, 0, z)] = leaf
    elif material == "fern":
        # The renderer's tuft mesh supplies the frond height. Keep the prefab
        # on one surface-following layer so it remains passable and placeable.
        for x, z in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1)):
            if rng.random() > 0.16:
                voxel_map[(x, 0, z)] = leaf
    elif material in ("reed", "kelp"):
        # Each voxel renders four tapered reed cards. Keep the prefab to a few
        # adjacent clusters so that these do not multiply into a solid-looking
        # wall of stems when several prefabs land close together.
        stalk_block = block_name(records, "organic", material, "fresh")
        cluster_count = rng.choice((1, 2, 2, 3))
        locations = [(0, 0)] + rng.sample(
            ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1)),
            cluster_count - 1,
        )
        for x, z in locations:
            voxel_map[(x, 0, z)] = stalk_block
    elif material == "coral":
        # Cluster-shaped coral sprites carry the branch silhouette; a single
        # vertical plant layer avoids stacked, unplaceable transparent voxels.
        for x, z in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1)):
            if rng.random() > 0.14:
                voxel_map[(x, 0, z)] = leaf
    elif material == "blossom":
        # A flat footprint uses layered leaf and flower sprite blocks; their
        # renderer meshes provide height without solid collision voxels.
        foliage = block_name(records, "organic", "leaf", "fresh")
        for x, z in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1)):
            voxel_map[(x, 0, z)] = leaf if rng.random() > 0.32 else foliage
    else:
        radius = rng.choice((1, 2))
        for x in range(-radius, radius + 1):
            for z in range(-radius, radius + 1):
                if abs(x) + abs(z) <= radius * 2 and rng.random() > 0.26:
                    voxel_map[(x, 0, z)] = leaf
    voxels = [{"dx": x, "dy": y, "dz": z, "type": block} for (x, y, z), block in sorted(voxel_map.items())]
    placement_mode = None if material in ("moss", "lichen") else "vertical_plant"
    return object_document(
        name, voxels, ["worldgen", "ground_cover", "vegetation"], seed,
        material, placement_mode,
    )


def make_flower_patch(name: str, records: list[dict[str, str]], seed: int, flower_material: str | None = None) -> dict[str, Any]:
    rng = random.Random(seed)
    palette = ["flower_white", "flower_yellow", "flower_red", "flower_pink", "flower_blue", "flower_purple", "flower_orange"]
    if flower_material:
        palette = [flower_material]
    cells = ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1))
    dominant = rng.choice(palette)
    accent = rng.choice([material for material in palette if material != dominant]) if len(palette) > 1 else dominant
    split_x, split_z = rng.choice(((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1)))
    split_threshold = rng.choice((0, 1))
    voxels = []
    used_materials: set[str] = set()
    for x, z in cells:
        if rng.random() <= 0.23:
            continue
        # A meadow patch should read as one or two nearby species, not as a
        # random rainbow. Seeded half-plane clustering keeps each prefab stable.
        side = x * split_x + z * split_z
        material = accent if side >= split_threshold and rng.random() < 0.72 else dominant
        variant = rng.choice(("fresh", "mature", "young", "bright", "spotted"))
        used_materials.add(material)
        voxels.append({
            "dx": x, "dy": 0, "dz": z,
            "type": block_name(records, "organic", material, variant),
        })
    if not voxels:
        material = dominant
        used_materials.add(material)
        voxels.append({
            "dx": 0, "dy": 0, "dz": 0,
            "type": block_name(records, "organic", material, "fresh"),
        })
    return object_document(
        name, voxels, ["worldgen", "ground_cover", "plants"], seed,
        ",".join(sorted(used_materials)), "vertical_plant",
    )


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
    voxels: list[dict[str, Any]] = []
    positions = rng.sample(((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1)), rng.choice((2, 3, 4)))
    for x, z in positions:
        voxels.append({"dx": x, "dy": 0, "dz": z, "type": cap})
    return object_document(name, voxels, ["worldgen", "ground_cover", "fungi"], seed, cap_material, "vertical_plant")


def make_coral_fan(name: str, records: list[dict[str, str]], seed: int) -> dict[str, Any]:
    rng = random.Random(seed)
    coral = block_name(records, "organic", "coral", rng.choice(("fresh", "dense", "bright", "mature")))
    sand = block_name(records, "soil", "pale_sand", "natural")
    voxels = [{"dx": x, "dy": 0, "dz": 0, "type": sand} for x in range(-2, 3)]
    for x in range(-2, 3):
        voxels.append({"dx": x, "dy": 1, "dz": 0, "type": coral})
    return object_document(name, voxels, ["decoration", "aquatic"], seed, "coral", "surface_layer")


def make_stalagmites(name: str, records: list[dict[str, str]], seed: int) -> dict[str, Any]:
    rng = random.Random(seed)
    stone = block_name(records, "rock", rng.choice(("limestone", "chalk", "tuff")), rng.choice(("natural", "layered", "rough")))
    # A shared, uneven foot joins several pointed mineral columns into one
    # cave formation. The old generator stacked isolated full-width columns,
    # which read as fence posts instead of stalagmites in voxel silhouettes.
    voxels_by_position: dict[tuple[int, int, int], dict[str, Any]] = {}
    centers = ((-2, 0), (0, 0), (2, 0), (-1, 1), (1, -1))
    for index, (x, z) in enumerate(centers):
        height = rng.choice((2, 3, 4)) if index != 1 else rng.choice((4, 5))
        for dx, dz in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
            position = (x + dx, 0, z + dz)
            voxels_by_position[position] = {
                "dx": position[0], "dy": 0, "dz": position[2], "type": stone,
            }
        for y in range(1, height):
            position = (x, y, z)
            voxels_by_position[position] = {
                "dx": x, "dy": y, "dz": z, "type": stone,
            }
    voxels = [voxels_by_position[position] for position in sorted(voxels_by_position)]
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
    for wood in ("oak", "pine", "birch", "aspen", "cedar", "spruce", "maple", "ash", "walnut", "beech", "willow", "poplar", "acacia", "jungle", "mangrove", "larch", "redwood"):
        objects.append(make_tree(f"asset_tree_{wood}", wood, records, 6 if wood in ("redwood", "jungle", "spruce") else 5))
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
                rule["surface_constraint"] = (
                    "underwater_floor" if name == "asset_coral_fan" else "any_land"
                )
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
    create_block_face_sheets(pack_dir, pack_dir / "previews" / "block_faces")


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


def create_block_face_sheets(pack_dir: Path, preview_dir: Path, limit: int = 120) -> int:
    """Create representative six-face previews grouped across material families."""
    blocks_dir = pack_dir / "blocks"
    textures_dir = pack_dir / "textures" / "blocks"
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for path in sorted(blocks_dir.glob("*.json")):
        try:
            block = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        family = (block.get("assetgen") or {}).get("family") or "source"
        grouped[str(family)].append(block)
    selected: list[dict[str, Any]] = []
    families = sorted(grouped)
    while len(selected) < limit and any(grouped.values()):
        for family in families:
            if grouped[family] and len(selected) < limit:
                selected.append(grouped[family].pop(0))
    if not selected:
        return 0
    preview_dir.mkdir(parents=True, exist_ok=True)
    per_page, cols, cell_w, cell_h = 20, 4, 168, 176
    font = ImageFont.load_default()
    for page_start in range(0, len(selected), per_page):
        page = selected[page_start:page_start + per_page]
        rows = math.ceil(len(page) / cols)
        canvas = Image.new("RGB", (cols * cell_w, rows * cell_h), (28, 31, 34))
        draw = ImageDraw.Draw(canvas)
        for offset, block in enumerate(page):
            x, y = (offset % cols) * cell_w, (offset // cols) * cell_h
            stems = block.get("textures", [])[:6]
            layout = ((64, 0, 4, "top"), (4, 48, 0, "front"), (46, 48, 1, "right"),
                      (88, 48, 2, "back"), (130, 48, 3, "left"), (64, 96, 5, "bottom"))
            for px, py, face, label in layout:
                if face >= len(stems):
                    continue
                image_path = textures_dir / f"{stems[face]}.png"
                if not image_path.is_file():
                    continue
                with Image.open(image_path) as source:
                    tile = source.convert("RGBA").crop((0, 0, source.width, source.width)).resize((40, 40), Image.Resampling.NEAREST)
                canvas.paste(tile, (x + px, y + py + 4), tile)
                draw.text((x + px, y + py + 44), label, font=font, fill=(220, 224, 228))
            draw.text((x + 4, y + 160), str(block.get("name", "block"))[:24], font=font, fill=(245, 245, 245))
        canvas.save(preview_dir / f"block_faces_{page_start // per_page + 1:02d}.png", optimize=True)
    return len(selected)


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
    if args.blocks:
        output_dir = args.output or (args.blocks / "previews" / "block_faces")
        count = create_block_face_sheets(args.blocks.resolve(), output_dir.resolve(), args.limit)
        print(json.dumps({"blocks": count, "preview_directory": str(output_dir.resolve())}, ensure_ascii=False))
        return 0
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


def command_calibrate(args: argparse.Namespace) -> int:
    calibration = derive_reference_calibration(REPO / "resource_packs" / args.reference_pack)
    output = args.output or (REPO / "assetgen_output" / "calibration" / f"{args.reference_pack}_aggregate.json")
    write_json(output, calibration)
    print(json.dumps({
        "mode": calibration["mode"],
        "reference_pack": calibration["source_pack"],
        "source_file_count": calibration["source_file_count"],
        "families": sorted(calibration["families"]),
        "output": str(output.resolve()),
        "source_pixels_used_as_output": False,
    }, ensure_ascii=False))
    return 0


def command_compare(args: argparse.Namespace) -> int:
    pack_dir = args.pack.resolve()
    output_dir = args.output.resolve()
    if output_dir.is_relative_to(pack_dir):
        raise ValueError("comparison reports must be written outside the resource pack")
    count = create_reference_comparison(pack_dir, args.reference_pack, output_dir)
    face_preview_blocks = create_block_face_sheets(pack_dir, output_dir / "block_faces")
    manifest = json.loads((pack_dir / "asset_manifest.json").read_text(encoding="utf-8-sig"))
    expected_kind = "minetest_reference_only" if args.reference_pack == "minetest_default_16" else "geology_reference_only"
    reference_dir = REPO / "resource_packs" / args.reference_pack / "textures" / "blocks"
    metrics: list[dict[str, Any]] = []
    for item in manifest.get("textures", []):
        source = item.get("source", {})
        if source.get("kind") != expected_kind:
            continue
        source_path = reference_dir / f"{source.get('reference_only')}.png"
        candidate_path = texture_path(pack_dir, item["stem"])
        if not source_path.is_file() or not candidate_path.is_file():
            continue
        with Image.open(source_path) as source_image, Image.open(candidate_path) as candidate_image:
            source_stats = texture_statistics(source_image)
            candidate_stats = texture_statistics(candidate_image)
        color_delta = sum(abs(source_stats[key] - candidate_stats[key]) for key in ("mean_rgb_r", "mean_rgb_g", "mean_rgb_b")) / 3.0
        metrics.append({
            "source_texture": source.get("reference_only"),
            "candidate_texture": item["stem"],
            "family": item.get("family", "unknown"),
            "material": item.get("material", "unknown"),
            "variant": item.get("variant", "unknown"),
            "role": item.get("role", "side"),
            "mean_rgb_delta": color_delta,
            "neighbor_contrast_delta": abs(source_stats["neighbor_contrast"] - candidate_stats["neighbor_contrast"]),
            "luminance_std_delta": abs(source_stats["luminance_std"] - candidate_stats["luminance_std"]),
            "alpha_coverage_delta": abs(source_stats["alpha_coverage"] - candidate_stats["alpha_coverage"]),
            "source_stats": source_stats,
            "candidate_stats": candidate_stats,
        })
    averages = {
        key: statistics.mean(row[key] for row in metrics) if metrics else 0.0
        for key in ("mean_rgb_delta", "neighbor_contrast_delta", "luminance_std_delta", "alpha_coverage_delta")
    }
    by_family: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in metrics:
        by_family[str(row["family"])].append(row)
    family_diagnostics = {
        family: {
            "compared_textures": len(rows),
            "aggregate_diagnostics": {
                key: statistics.mean(row[key] for row in rows)
                for key in ("mean_rgb_delta", "neighbor_contrast_delta", "luminance_std_delta", "alpha_coverage_delta")
            },
        }
        for family, rows in sorted(by_family.items())
    }
    calibration = derive_reference_calibration(reference_dir.parent.parent) if args.reference_mode == "aggregate-calibration" else None
    report = {
        "schema_version": 2,
        "reference_pack": args.reference_pack,
        "reference_mode": args.reference_mode,
        "compared_textures": len(metrics),
        "contact_sheets": math.ceil(count / 80),
        "face_preview_blocks": face_preview_blocks,
        "aggregate_diagnostics": averages,
        "family_diagnostics": family_diagnostics,
        "worst_color_mismatches": sorted(metrics, key=lambda row: row["mean_rgb_delta"], reverse=True)[:25],
        "worst_detail_mismatches": sorted(
            metrics,
            key=lambda row: row["neighbor_contrast_delta"] + row["luminance_std_delta"],
            reverse=True,
        )[:25],
        "worst_alpha_mismatches": sorted(
            metrics, key=lambda row: row["alpha_coverage_delta"], reverse=True
        )[:25],
        "texture_metrics": sorted(metrics, key=lambda row: str(row["source_texture"])),
        "calibration_summary": ({"families": calibration["families"], "source_file_count": calibration["source_file_count"], "source_pixels_used_as_output": False} if calibration else None),
        "note": "Color and detail statistics are measured after compositing over neutral gray so transparent RGB values do not affect QA; alpha coverage is reported separately. Visual review of contact sheets remains required.",
    }
    write_json(output_dir / "comparison_report.json", report)
    print(json.dumps({
        "report": str((output_dir / "comparison_report.json").resolve()),
        "compared_textures": len(metrics),
        "contact_sheets": report["contact_sheets"],
        "aggregate_diagnostics": averages,
        "family_diagnostics": family_diagnostics,
    }, ensure_ascii=False))
    return 0


def build_pack(
    no_preview: bool = False,
    add_worldgen: bool = True,
    reference_mode: str = "qa-only",
    size: int = DEFAULT_TEXTURE_SIZE,
) -> dict[str, Any]:
    catalog = load_catalog()
    records = family_records(catalog)
    calibration = derive_reference_calibration(REPO / "resource_packs" / "minetest_default_16") if reference_mode == "aggregate-calibration" else None
    review_required = reference_mode == "aggregate-calibration"
    pack_dir = PACK_DIR if not review_required else STAGING_DIR / "cubatarium_worldgen_aggregate_cc0_review"
    pack_dir.mkdir(parents=True, exist_ok=True)
    (pack_dir / "blocks").mkdir(parents=True, exist_ok=True)
    (pack_dir / "objects").mkdir(parents=True, exist_ok=True)
    (pack_dir / "textures" / "blocks").mkdir(parents=True, exist_ok=True)
    write_json(pack_dir / "pack.json", {
        "id": PACK_ID,
        "name": "Cubatarium Worldgen" if not review_required else "Cubatarium Worldgen (license review required)",
        "version": 1,
        "license": "REVIEW_REQUIRED" if review_required else "CC0-1.0",
        "resolution": size,
        "priority": 21,
        "pack_format": 1,
        "worldgen_role": "primary",
        "merge_mode": "override",
        "depends": [],
        "conflicts": ["minetest_default_16"],
        "min_game_version": "0.1.0",
        "assetgen_reference_mode": reference_mode,
    })
    minetest_blocks = ensure_stable_block_ids(load_block_files("minetest_default_16"))
    previous_pack_ids = {
        str(block["name"]): block["id"]
        for block in load_block_files(PACK_ID)
        if isinstance(block.get("name"), str) and isinstance(block.get("id"), int)
    }
    geology_blocks = load_block_files("cubatarium_geology_cc0")
    reserved_block_ids = {
        block["id"] for block in minetest_blocks
        if isinstance(block.get("id"), int)
    }
    pack_block_ids = stable_pack_block_ids(
        [record["name"] for record in records]
        + [str(block["name"]) for block in geology_blocks],
        previous_pack_ids,
        reserved_block_ids,
    )
    for block in minetest_blocks:
        name = block.get("name", "").lower()
        render = block.setdefault("render", {})
        if not isinstance(render, dict):
            render = {}
            block["render"] = render
        if name == "water" and render.get("style") == "fluid":
            animation = block.setdefault("animation", {})
            if isinstance(animation, dict):
                # A full-tile 16-step scroll returns to its initial phase
                # without a large jump at the end of the loop.
                animation["frame_count"] = 16
                animation.setdefault("frametime", 6)
        if is_cross_decor_block(name):
            render["transparent"] = True
            render["style"] = "cross"
            render["double_sided"] = True
        if render.get("style") == "cross":
            render["shape"] = (
                "flame" if "fire" in name else
                "pad" if any(token in name for token in ("waterlily", "lily_pad")) else
                "cross" if "mushroom" in name else
                "reed" if any(token in name for token in ("reed", "papyrus", "kelp", "seagrass", "vine")) else
                "tuft" if any(token in name for token in ("grass", "fern", "deadbush", "shrub", "crop", "carrot", "potato", "wheat", "cactus_seedling", "pine_needles")) else
                "cluster" if "bush_sapling" in name else
                "cross"
            )
            render["texture_variation"] = 1.0
            render["double_sided"] = True
            physics = block.setdefault("physics", {})
            movement = physics.setdefault("movement", {})
            movement["occupancy"] = 0.0
        if is_leaf_material("organic", "", name):
            render["style"] = "cutout"
            render["texture_variation"] = 1.0
            physics = block.setdefault("physics", {})
            movement = physics.setdefault("movement", {})
            movement["occupancy"] = 0.0
        else:
            machine_face = any(token in name for token in ("furnace", "crafting_table", "bookshelf", "anvil", "dispenser", "dropper"))
            fluid_face = any(token in name for token in ("water", "lava"))
            bark_side = name in ("tree_bark", "tree_log", "wood")
            randomizable_surface = (
                any(token in name for token in ("sand", "dirt", "gravel", "stone", "clay", "mud"))
                and not any(token in name for token in (
                    "grass", "snow", "litter", "brick", "tile", "tree", "wood",
                    "log", "ore", "furnace", "workbench", "crafting_table",
                ))
            )
            render["texture_variation"] = (
                0.56 if bark_side
                else 0.36 if fluid_face and "water" in name
                else 0.0 if machine_face or fluid_face
                else 1.0 if randomizable_surface
                else 0.48
            )
            if render.get("style") == "cross":
                render["texture_variation"] = 1.0
        if name == "fire":
            render.update({"transparent": True, "style": "cross", "shape": "flame", "double_sided": True})
    animated_texture_frames: dict[str, int] = {}
    for block in minetest_blocks:
        animation = block.get("animation")
        textures = block.get("textures", [])
        frame_count = animation.get("frame_count") if isinstance(animation, dict) else None
        if len(textures) == 6 and isinstance(frame_count, int) and frame_count > 1:
            for stem in textures:
                if isinstance(stem, str):
                    animated_texture_frames[stem] = max(
                        animated_texture_frames.get(stem, 1), frame_count
                    )
    minetest_stems = minetest_replacement_stems()
    geology_stems = {p.stem: f"geo_{safe_name(p.stem)}" for p in (REPO / "resource_packs" / "cubatarium_geology_cc0" / "textures" / "blocks").glob("*.png")}
    hashes: set[str] = set()
    manifest_textures: list[dict[str, Any]] = []
    preview_entries: list[tuple[str, Path]] = []
    for stem, target in minetest_stems.items():
        entry = make_replacement_texture(stem, target, infer_family(stem), "minetest_reference_only", pack_dir, hashes, animated_texture_frames.get(stem, 1), calibration, size, review_required)
        manifest_textures.append(entry)
        preview_entries.append((target, texture_path(pack_dir, target)))
    for stem, target in geology_stems.items():
        entry = make_replacement_texture(stem, target, infer_family(stem), "geology_reference_only", pack_dir, hashes, calibration=calibration, size=size, review_required=review_required)
        manifest_textures.append(entry)
        preview_entries.append((target, texture_path(pack_dir, target)))
    for record in records:
        target = record["name"]
        face_stems, derived_roles = block_face_textures(record, target)
        face_role_map = {"side": target, **{role: stem for stem, role in derived_roles}}
        if target not in face_role_map.values():
            face_role_map["side"] = target
        roles_by_stem = {stem: role for role, stem in face_role_map.items()}
        for stem, role in sorted(roles_by_stem.items()):
            entry = create_catalog_texture(record, stem, role, pack_dir, hashes, calibration, size, review_required)
            manifest_textures.append(entry)
            preview_entries.append((stem, texture_path(pack_dir, stem)))
        write_json(pack_dir / "blocks" / f"{target}.json", block_definition(record, face_stems, pack_block_ids[target]))
    included_block_sources: list[dict[str, Any]] = []
    for block in minetest_blocks:
        adapted = remap_block_textures(block, minetest_stems)
        adapted["license"] = "CC0-1.0"
        adapted["provenance"] = {
            "source_pack": "minetest_default_16",
            "source_kind": "Cubatarium-authored block definition",
            "license": "CC0-1.0",
            "adaptation": "Cubatarium replacement texture stems",
        }
        write_json(pack_dir / "blocks" / f"{adapted['name']}.json", adapted)
        included_block_sources.append({
            "name": adapted["name"],
            "id": adapted["id"],
            "source_pack": "minetest_default_16",
            "source_kind": "Cubatarium-authored block definition",
            "license": "CC0-1.0",
        })
    for block in geology_blocks:
        adapted = remap_block_textures(block, geology_stems)
        adapted["id"] = pack_block_ids[str(adapted["name"])]
        adapted["license"] = "CC0-1.0"
        adapted["provenance"] = {
            "source_pack": "cubatarium_geology_cc0",
            "license": "CC0-1.0",
        }
        write_json(pack_dir / "blocks" / f"{adapted['name']}.json", adapted)
        included_block_sources.append({
            "name": adapted["name"],
            "id": adapted["id"],
            "source_pack": "cubatarium_geology_cc0",
            "license": "CC0-1.0",
        })
    override_map: dict[str, list[dict[str, Any]]] = {}
    for pack_id, blocks, replacements in (("minetest_default_16", minetest_blocks, minetest_stems), ("cubatarium_geology_cc0", geology_blocks, geology_stems)):
        for block in blocks:
            grouped: dict[str, list[str]] = defaultdict(list)
            for face_index, stem in enumerate((block.get("textures") or [])[:6]):
                if stem in replacements:
                    grouped[replacements[stem]].append(FACE_NAMES[face_index])
            if grouped:
                override_map.setdefault(block["name"], []).extend({"stem": stem, "faces": faces} for stem, faces in sorted(grouped.items()))
    write_json(pack_dir / "texture_overrides.json", override_map)
    (pack_dir / "texture_overrides.yaml").write_text(
        yaml.safe_dump(override_map, sort_keys=False, allow_unicode=True), encoding="utf-8"
    )
    objects = generated_objects(records)
    for obj in objects:
        write_json(pack_dir / "objects" / f"{obj['name']}.json", obj)
    worldgen_rules_added = ensure_worldgen_rules(objects) if add_worldgen and not review_required else 0
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
        "generator": {
            "name": "Cubatarium AssetGen",
            "version": 32,
            "backend": "procedural",
            "texture_synthesis": "named_tree_species_stable_ids_and_sand_match_v32",
            "resolution": size,
            "profile": "voxel64" if size > 16 else "voxel16",
            "reference_mode": reference_mode,
        },
        "reference_calibration": calibration,
        "counts": {
            "block_definitions": len(records) + len(minetest_blocks) + len(geology_blocks),
            "procedural_cc0_block_definitions": len(records),
            "minetest_block_definitions_included": len(minetest_blocks),
            "geology_block_definitions_included": len(geology_blocks),
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
        "block_provenance": included_block_sources,
        "textures": sorted(manifest_textures, key=lambda item: item["stem"]),
    }
    write_json(pack_dir / "asset_manifest.json", texture_manifest)
    # Keep the generated pack in sync with the catalog. Rebuilding after a
    # catalog edit used to leave removed block definitions and textures behind
    # in the pack directory. Besides inflating inventory counts, those stale
    # files could keep old or shifted block IDs visible in existing worlds.
    expected_block_files = {
        f"{record['name']}.json" for record in records
    } | {f"{item['name']}.json" for item in included_block_sources}
    for path in (pack_dir / "blocks").glob("*.json"):
        if path.name not in expected_block_files:
            path.unlink()
    expected_texture_files = {f"{item['stem']}.png" for item in manifest_textures}
    for path in (pack_dir / "textures" / "blocks").glob("*.png"):
        if path.name not in expected_texture_files:
            path.unlink()
    expected_object_files = {f"{item['name']}.json" for item in objects}
    for path in (pack_dir / "objects").glob("*.json"):
        if path.name not in expected_object_files:
            path.unlink()
    geology_notice = (REPO / "resource_packs" / "cubatarium_geology_cc0" / "LICENSE.txt").read_text(encoding="utf-8-sig").strip()
    license_text = (
        "All block definitions, voxel objects, and texture images in this pack are Cubatarium content released under CC0-1.0.\n"
        if not review_required else
        "This staging pack requires license review before CC0 publication.\n"
    )
    license_text += (
        "License: https://creativecommons.org/publicdomain/zero/1.0/\n\n"
        "The Cubatarium-authored block definitions formerly distributed in the minetest_default_16 pack are included here with Cubatarium replacement texture stems. The minetest_default_16 texture pixels were used only as external references and are not included in this pack.\n\n"
        "The integrated Cubatarium Geology definitions and their replacement textures are CC0. The source pack notice follows:\n"
        f"{geology_notice}\n\n"
    )
    license_text += (
        "Reference file names classify material families. A small set of scalar properties, including foliage alpha coverage, calibrates newly drawn masks. Source pixel patterns are not copied into generated textures.\n"
        if not review_required else
        "Aggregate per-family color and spatial statistics from the CC BY-SA reference pack influenced generation. No source pixels or patches were copied; do not release this staging output as CC0 until its provenance has been reviewed.\n"
    )
    license_text += "Minecraft Java Edition 26.3 counts are used only as a catalog-size benchmark; no Minecraft assets are included.\n"
    (pack_dir / "LICENSE.txt").write_text(license_text, encoding="utf-8")
    create_previews(pack_dir, preview_entries, no_preview)
    return {"blocks": len(records) + len(minetest_blocks) + len(geology_blocks), "textures": len(manifest_textures), "minetest_block_definitions": len(minetest_blocks), "geology_block_definitions": len(geology_blocks), "minetest_replacements": len(minetest_stems), "geology_replacements": len(geology_stems), "objects": len(objects), "worldgen_rules": worldgen_rule_count, "worldgen_rules_added_this_build": worldgen_rules_added, "reference_mode": reference_mode, "pack_directory": str(pack_dir.resolve()), "license_review_required": review_required}


def build_photo64_overlay(
    no_preview: bool = False,
    reference_mode: str = "qa-only",
) -> dict[str, Any]:
    """Build a selectable 64px texture-only overlay for the default CC0 pack."""
    base_pack = PACK_DIR if reference_mode == "qa-only" else STAGING_DIR / "cubatarium_worldgen_aggregate_cc0_review"
    manifest_path = base_pack / "asset_manifest.json"
    if not manifest_path.is_file():
        raise RuntimeError(f"build the 16px base pack first: {manifest_path}")
    base_manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    calibration = derive_reference_calibration(REPO / "resource_packs" / "minetest_default_16") if reference_mode == "aggregate-calibration" else None
    review_required = reference_mode == "aggregate-calibration"
    pack_dir = PHOTO64_PACK_DIR if not review_required else STAGING_DIR / "cubatarium_worldgen_photo64_aggregate_cc0_review"
    for directory in (pack_dir / "blocks", pack_dir / "textures" / "blocks"):
        directory.mkdir(parents=True, exist_ok=True)
    write_json(pack_dir / "pack.json", {
        "id": PHOTO64_PACK_ID,
        "name": "Cubatarium Worldgen Photo 64" if not review_required else "Cubatarium Photo 64 (license review required)",
        "version": 1,
        "license": "REVIEW_REQUIRED" if review_required else "CC0-1.0",
        "resolution": 64,
        "priority": 22,
        "pack_format": 1,
        "worldgen_role": "secondary",
        "merge_mode": "override",
        "depends": [PACK_ID],
        "conflicts": [],
        "min_game_version": "0.1.0",
        "allow_resolution_mix": True,
        "assetgen_reference_mode": reference_mode,
    })

    texture_map: dict[str, str] = {}
    overlay_textures: list[dict[str, Any]] = []
    preview_entries: list[tuple[str, Path]] = []
    hashes: set[str] = set()
    for item in base_manifest.get("textures", []):
        stem = item.get("stem")
        if not isinstance(stem, str):
            continue
        source = item.get("source", {})
        source_stem = source.get("reference_only") or stem
        role = item.get("role") or source_texture_role(source_stem, item.get("family", "rock"))
        family = item.get("family", "rock")
        material = item.get("material", "granite")
        variant = item.get("variant", "natural")
        seed = int(item.get("seed", stable_seed(stem)))
        frame_count = int(item.get("animation_frame_count", 1))
        sprite_alpha_reference_coverage = item.get("sprite_alpha_reference_coverage")
        sprite_alpha_coverage = item.get(
            "sprite_alpha_target_coverage", sprite_alpha_reference_coverage
        )

        def make_sprite(texture_seed: int) -> Image.Image | None:
            sprite_image = procedural_decor_sprite(source_stem, 64, texture_seed)
            if (sprite_image is not None and isinstance(sprite_alpha_coverage, (int, float))
                    and 0.02 <= float(sprite_alpha_coverage) < 0.98):
                sprite_image = fit_alpha_coverage(
                    sprite_image, float(sprite_alpha_coverage), texture_seed
                )
            return sprite_image

        def make_overlay_image(texture_variant: str, texture_seed: int) -> Image.Image:
            base = generated_texture(
                family, material, texture_variant, texture_seed, 64, role,
                calibration_for_texture(calibration, source_stem, family),
            )
            glass = procedural_glass_texture(source_stem, 64, material, texture_seed) if family == "glass" else None
            if glass is not None:
                base = glass
            coral = procedural_coral_skeleton(source_stem, 64, texture_seed)
            if coral is not None:
                base = coral
            sprite = make_sprite(texture_seed) if role == "decor_sprite" else None
            if sprite is not None:
                base = sprite
            base = add_semantic_face_detail(base, source_stem, texture_seed)
            if role != "decor_sprite":
                base = periodize(base, 64, 4)
            if frame_count <= 1:
                return base
            motion_x, motion_y = ((1, 0) if "water" in source_stem.lower() else (1, 1))
            strip = Image.new("RGBA", (64, 64 * frame_count))
            for frame_index in range(frame_count):
                if role == "decor_sprite":
                    sprite_frame = make_sprite(texture_seed + frame_index)
                    frame = sprite_frame if sprite_frame is not None else base
                else:
                    frame = periodize(ImageChops.offset(base, motion_x * frame_index, motion_y * frame_index), 64, 4)
                strip.paste(frame, (0, frame_index * 64))
            return strip

        image = make_overlay_image(variant, seed)
        overlay_stem = f"p64_{stem}"
        digest = image_pixel_hash(image)
        salt = 0
        while digest in hashes:
            salt += 1
            if salt > 512:
                raise RuntimeError(f"could not make a unique 64px overlay for {stem} after 512 deterministic variants")
            image = make_overlay_image(f"{variant}_{salt}", seed + salt)
            digest = image_pixel_hash(image)
        hashes.add(digest)
        save_texture_if_changed(image, texture_path(pack_dir, overlay_stem))
        texture_map[stem] = overlay_stem
        overlay_textures.append({
            "stem": overlay_stem,
            "source_16px_stem": stem,
            "family": family,
            "material": material,
            "variant": variant,
            "role": role,
            "seed": seed,
            "backend": "procedural",
            "reference_mode": reference_mode,
            "resolution": [64, 64],
            "animation_frame_count": frame_count if frame_count > 1 else None,
            "pixel_sha256": digest,
            "source": {"kind": "material_parameters", "source_texture": stem},
            "license": "REVIEW_REQUIRED" if review_required else "CC0-1.0",
            "license_review_required": review_required,
        })
        if isinstance(sprite_alpha_coverage, (int, float)) and 0.02 <= float(sprite_alpha_coverage) < 0.98:
            if isinstance(sprite_alpha_reference_coverage, (int, float)):
                overlay_textures[-1]["sprite_alpha_reference_coverage"] = round(
                    float(sprite_alpha_reference_coverage), 4
                )
            overlay_textures[-1]["sprite_alpha_target_coverage"] = round(
                float(sprite_alpha_coverage), 4
            )
        preview_entries.append((overlay_stem, texture_path(pack_dir, overlay_stem)))

    override_map: dict[str, list[dict[str, Any]]] = {}
    for path in sorted((base_pack / "blocks").glob("*.json")):
        block = json.loads(path.read_text(encoding="utf-8-sig"))
        grouped: dict[str, list[str]] = defaultdict(list)
        for index, stem in enumerate((block.get("textures") or [])[:6]):
            target = texture_map.get(stem)
            if target:
                grouped[target].append(FACE_NAMES[index])
        if grouped:
            override_map[block["name"]] = [
                {"stem": target, "faces": faces}
                for target, faces in sorted(grouped.items())
            ]
    write_json(pack_dir / "texture_overrides.json", override_map)
    (pack_dir / "texture_overrides.yaml").write_text(yaml.safe_dump(override_map, sort_keys=False, allow_unicode=True), encoding="utf-8")
    write_json(pack_dir / "asset_manifest.json", {
        "schema_version": 1,
        "generator": {"name": "Cubatarium AssetGen", "version": 2, "backend": "procedural", "resolution": 64, "profile": "photo64", "reference_mode": reference_mode},
        "reference_calibration": calibration,
        "counts": {"textures": len(overlay_textures), "block_face_overrides": len(override_map)},
        "textures": overlay_textures,
    })
    (pack_dir / "LICENSE.txt").write_text(
        "Texture-only 64px override pack for Cubatarium Worldgen.\n"
        + ("CC0-1.0. All images were generated from material parameters.\n" if not review_required else "License review required before CC0 publication. Aggregate Minetest texture statistics affected synthesis; no source pixels or patches were copied.\n"),
        encoding="utf-8",
    )
    create_previews(pack_dir, preview_entries, no_preview)
    return {"pack_directory": str(pack_dir.resolve()), "textures": len(overlay_textures), "overridden_blocks": len(override_map), "reference_mode": reference_mode, "license_review_required": review_required}


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
    size = 16 if args.profile == "voxel16" else 64
    style = "photo-based seamless game material" if args.profile == "photo64" else "seamless detailed voxel material"
    prompt = args.prompt or f"{style}, {args.material or 'natural material'}, {size}x{size} tile, no lighting, no shadows, no perspective"
    call_args: dict[str, Any] = {"prompt": prompt, "generator": generator}
    if args.input:
        call_args.update({"image": Image.open(args.input).convert("RGB"), "strength": args.strength})
    result = pipe(**call_args).images[0]
    return result


def synthesize_quilt(reference_path: Path, size: int, seed: int) -> Image.Image:
    """Small patch-quilting synthesizer for explicitly licensed photo inputs."""
    with Image.open(reference_path) as source_image:
        source = source_image.convert("RGBA")
    patch = max(8, size // 2)
    overlap = max(2, size // 8)
    step = max(1, patch - overlap)
    source_w, source_h = source.size
    if source_w < patch or source_h < patch:
        source = ImageOps.fit(source, (max(patch, source_w), max(patch, source_h)), method=Image.Resampling.LANCZOS)
        source_w, source_h = source.size
    rng = random.Random(seed)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    for y in range(0, size, step):
        for x in range(0, size, step):
            w, h = min(patch, size - x), min(patch, size - y)
            candidates: list[tuple[float, Image.Image]] = []
            for _ in range(40):
                sx = rng.randrange(max(1, source_w - w + 1))
                sy = rng.randrange(max(1, source_h - h + 1))
                candidate = source.crop((sx, sy, sx + w, sy + h))
                score = 0.0
                weight = 0
                if x > 0:
                    width = min(overlap, w, x)
                    a = out.crop((x, y, x + width, y + h)).convert("RGB")
                    b = candidate.crop((0, 0, width, h)).convert("RGB")
                    diff = ImageChops.difference(a, b)
                    score += sum(ImageStat.Stat(diff).mean) * width * h
                    weight += width * h
                if y > 0:
                    height = min(overlap, h, y)
                    a = out.crop((x, y, x + w, y + height)).convert("RGB")
                    b = candidate.crop((0, 0, w, height)).convert("RGB")
                    diff = ImageChops.difference(a, b)
                    score += sum(ImageStat.Stat(diff).mean) * w * height
                    weight += w * height
                candidates.append((score / max(1, weight), candidate))
            candidates.sort(key=lambda item: item[0])
            pool = candidates[:max(1, len(candidates) // 8)]
            tile = rng.choice(pool)[1]
            mask = Image.new("L", (w, h), 255)
            mask_px = mask.load()
            for py in range(h):
                for px in range(w):
                    alpha = 255
                    if x > 0 and px < overlap:
                        alpha = min(alpha, int(255 * (px + 1) / (overlap + 1)))
                    if y > 0 and py < overlap:
                        alpha = min(alpha, int(255 * (py + 1) / (overlap + 1)))
                    mask_px[px, py] = alpha
            out.paste(tile, (x, y), mask)
    return out


def generate_texture_command(args: argparse.Namespace) -> int:
    backend = args.backend
    size = 16 if args.profile == "voxel16" else 64
    texture_family = args.family or infer_family(args.material or "rock")
    texture_material = safe_name(args.material or "basalt")
    seam = (
        args.seam if args.seam is not None
        else default_texture_seam(texture_family, texture_material, size, args.role)
    )
    calibration = getattr(args, "_calibration", None)
    if args.reference_mode == "aggregate-calibration" and calibration is None:
        calibration = derive_reference_calibration(
            REPO / "resource_packs" / args.reference_pack
        )
    if args.resource_profile == "offline" and backend == "auto":
        backend = "quilting" if args.input else "procedural"
    elif args.resource_profile == "offline" and backend == "diffusers":
        raise RuntimeError("--backend diffusers requires a non-offline --resource-profile")
    if backend == "auto":
        if not args.model:
            args.model = cached_diffusers_model_id()
        backend = "diffusers" if args.model else "quilting" if args.input else "procedural"
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
        image = generated_texture(
            family, material, variant, args.seed, size,
            getattr(args, "role", "side"), calibration_for_family(calibration, family)
        )
        if args.input:
            # This mode transfers only a broad color centroid, not patches.
            reference = Image.open(args.input).convert("RGB").resize((4, 4), Image.Resampling.BOX)
            avg = tuple(sum(reference.getpixel((x, y))[c] for x in range(4) for y in range(4)) // 16 for c in range(3))
            tint = Image.new("RGBA", image.size, (*avg, 255))
            image = Image.blend(image, tint, 0.12)
    elif backend == "quilting":
        if not args.input:
            raise RuntimeError("--backend quilting requires --input with a reference image")
        image = synthesize_quilt(args.input, size, args.seed)
    axes = ("x",) if args.role in COMPOSITE_SIDE_ROLES else ("x", "y")
    output = periodize(image, size, seam, texture_palette(args.role, size), axes)
    output_path = args.output.resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output.save(output_path, format="PNG", optimize=True)
    model_path = Path(args.model) if args.model else None
    if model_path and not model_path.exists():
        model_path = cached_model_dir(args.model)
    model_metadata = {}
    if model_path and (model_path / "assetgen-model.json").is_file():
        model_metadata = json.loads((model_path / "assetgen-model.json").read_text(encoding="utf-8-sig"))
    calibration_metadata = None
    if calibration:
        calibration_bytes = json.dumps(calibration, sort_keys=True, separators=(",", ":")).encode("utf-8")
        calibration_metadata = {
            "mode": calibration.get("mode"),
            "source_pack": calibration.get("source_pack"),
            "source_file_count": calibration.get("source_file_count"),
            "summary_sha256": hashlib.sha256(calibration_bytes).hexdigest(),
        }
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
        "source_license": args.source_license if args.input else None,
        "reference_mode": args.reference_mode,
        "reference_calibration": calibration_metadata,
        "seam_blend_width": seam,
        "license": "CC0-1.0" if not (args.input and args.source_license != "CC0-1.0") and backend != "diffusers" and args.reference_mode == "qa-only" else "REVIEW_REQUIRED",
        "license_review_required": bool((args.input and args.source_license != "CC0-1.0") or backend == "diffusers" or args.reference_mode == "aggregate-calibration"),
        "resolution": [size, size],
    }
    write_json(output_path.with_suffix(output_path.suffix + ".assetgen.json"), provenance)
    if not getattr(args, "quiet", False):
        print(json.dumps(provenance, ensure_ascii=False))
    return 0


def texture_batch_command(args: argparse.Namespace) -> int:
    catalog_path = args.catalog or CATALOG_PATH
    catalog = load_catalog(catalog_path)
    records = family_records(catalog)
    calibration = None
    if args.reference_mode == "aggregate-calibration":
        calibration = derive_reference_calibration(REPO / "resource_packs" / args.reference_pack)
    calibration_report = None
    if calibration:
        payload = json.dumps(calibration, sort_keys=True, separators=(",", ":")).encode("utf-8")
        calibration_report = {
            "mode": calibration.get("mode"),
            "source_pack": calibration.get("source_pack"),
            "source_file_count": calibration.get("source_file_count"),
            "summary_sha256": hashlib.sha256(payload).hexdigest(),
        }
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
        item._calibration = calibration
        item.quiet = True
        generate_texture_command(item)
        sidecar = item.output.with_suffix(item.output.suffix + ".assetgen.json")
        entries.append(json.loads(sidecar.read_text(encoding="utf-8-sig")))
    report = {
        "schema_version": 1,
        "catalog": str(catalog_path.resolve()),
        "backend_requested": args.backend,
        "profile": args.profile,
        "reference_mode": args.reference_mode,
        "license_review_required": args.reference_mode == "aggregate-calibration" or args.backend == "diffusers",
        "calibration": calibration_report,
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
    tile = 24
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
                if family == "organic" and material == "leaf":
                    base = {
                        "fresh": (56, 91, 40), "mature": (48, 79, 36),
                        "dense": (42, 73, 33), "sparse": (71, 103, 49),
                        "spotted": (60, 88, 40), "striped": (49, 82, 37),
                        "pale": (91, 113, 52), "dark": (35, 61, 30),
                        "young": (70, 111, 51), "dry": (117, 105, 49),
                    }.get(record["variant"], base)
                elif family == "organic" and material == "needle":
                    base = (53, 91, 46)
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


def audit_photo64_pack(pack_dir: Path) -> tuple[list[str], dict[str, int]]:
    errors: list[str] = []
    try:
        pack_manifest = json.loads((pack_dir / "pack.json").read_text(encoding="utf-8-sig"))
        asset_manifest = json.loads((pack_dir / "asset_manifest.json").read_text(encoding="utf-8-sig"))
        overrides = json.loads((pack_dir / "texture_overrides.json").read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"cannot read 64px overlay metadata: {exc}"], {}
    if pack_manifest.get("id") != PHOTO64_PACK_ID or pack_manifest.get("resolution") != 64:
        errors.append("photo overlay must declare cubatarium_worldgen_photo64_cc0 at 64px")
    if pack_manifest.get("license") != "CC0-1.0":
        errors.append("photo overlay is not cleared for CC0 publication")
    texture_dir = pack_dir / "textures" / "blocks"
    textures = list(texture_dir.glob("*.png"))
    manifest_textures = asset_manifest.get("textures", [])
    manifest_stems = {item.get("stem") for item in manifest_textures if isinstance(item, dict)}
    if manifest_stems != {path.stem for path in textures}:
        errors.append("64px overlay manifest stems do not match its PNG files")
    unique: set[str] = set()
    for path in textures:
        try:
            with Image.open(path) as image_file:
                image = image_file.convert("RGBA")
        except Exception as exc:
            errors.append(f"invalid image {path.name}: {exc}")
            continue
        frames = 1
        item = next((entry for entry in manifest_textures if isinstance(entry, dict) and entry.get("stem") == path.stem), {})
        if isinstance(item.get("animation_frame_count"), int):
            frames = max(1, item["animation_frame_count"])
        if image.size != (64, 64 * frames):
            errors.append(f"{path.name}: expected 64x{64 * frames}, got {image.width}x{image.height}")
        alpha_min, alpha_max = image.getchannel("A").getextrema()
        role = item.get("role")
        family = item.get("family", "unknown")
        if requires_clear_glass_cutouts(path.stem, family) and (alpha_min != 0 or alpha_max < 128):
            errors.append(f"{path.name}: glass surface must have transparent interior and visible bevel pixels")
        elif family == "glass" and alpha_min >= 255:
            errors.append(f"{path.name}: glass texture must include alpha transparency")
        elif role in ("decor_sprite", "leaf_cutout") and alpha_min >= 255:
            errors.append(f"{path.name}: decor sprite must have transparent background pixels")
        elif item.get("family") != "glass" and role not in ("decor_sprite", "leaf_cutout") and alpha_min < 255:
            errors.append(f"{path.name}: non-glass texture unexpectedly includes transparency")
        for frame_index in range(frames):
            frame = image.crop((0, frame_index * 64, 64, (frame_index + 1) * 64))
            pixels = frame.load()
            if role != "decor_sprite" and (any(pixels[0, y] != pixels[63, y] for y in range(64)) or any(pixels[x, 0] != pixels[x, 63] for x in range(64))):
                errors.append(f"{path.name}: frame {frame_index} is not tileable")
                break
        if image_pixel_hash(image) in unique:
            errors.append(f"pixel-identical duplicate texture {path.name}")
        unique.add(image_pixel_hash(image))
    base_pack = PACK_DIR if pack_manifest.get("assetgen_reference_mode") != "aggregate-calibration" else STAGING_DIR / "cubatarium_worldgen_aggregate_cc0_review"
    base_blocks_dir = base_pack / "blocks"
    base_blocks = [json.loads(path.read_text(encoding="utf-8-sig")) for path in sorted(base_blocks_dir.glob("*.json"))]
    if set(overrides) != {block["name"] for block in base_blocks}:
        errors.append("64px overlay must override every block in the base pack")
    for name, entries in overrides.items():
        resolved_faces: set[str] = set()
        for entry in entries if isinstance(entries, list) else []:
            stem = entry.get("stem") if isinstance(entry, dict) else None
            if not isinstance(stem, str) or stem not in manifest_stems:
                errors.append(f"{name}: unresolved 64px texture override {stem!r}")
                continue
            resolved_faces.update(face for face in entry.get("faces", []) if isinstance(face, str))
        if resolved_faces != set(FACE_NAMES):
            errors.append(f"{name}: expected overrides for all six face roles, got {sorted(resolved_faces)}")
    return errors, {
        "textures": len(textures),
        "overridden_blocks": len(overrides),
        "unique_pixel_textures": len(unique),
        "resolution": 64,
    }


def audit_pack(pack_dir: Path) -> tuple[list[str], dict[str, int]]:
    errors: list[str] = []
    try:
        pack_manifest = json.loads((pack_dir / "pack.json").read_text(encoding="utf-8-sig"))
        if pack_manifest.get("id") == PHOTO64_PACK_ID:
            return audit_photo64_pack(pack_dir)
        if pack_manifest.get("worldgen_role") != "primary":
            errors.append("standalone worldgen pack must declare worldgen_role=primary")
        if pack_manifest.get("license") != "CC0-1.0":
            errors.append("standalone worldgen pack must declare CC0-1.0")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        errors.append(f"cannot read pack metadata: {exc}")
    blocks_dir = pack_dir / "blocks"
    textures_dir = pack_dir / "textures" / "blocks"
    objects_dir = pack_dir / "objects"
    provenance_path = pack_dir / "asset_manifest.json"
    try:
        provenance = json.loads(provenance_path.read_text(encoding="utf-8-sig")) if provenance_path.is_file() else {}
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        provenance = {}
        errors.append(f"cannot read asset manifest: {exc}")
    manifest_textures = provenance.get("textures", [])
    manifest_by_stem = {item.get("stem"): item for item in manifest_textures if isinstance(item, dict)}
    blocks = [json.loads(path.read_text(encoding="utf-8-sig")) for path in sorted(blocks_dir.glob("*.json"))] if blocks_dir.exists() else []
    textures = sorted(textures_dir.glob("*.png")) if textures_dir.exists() else []
    animated_texture_frames: dict[str, int] = {}
    for block in blocks:
        animation = block.get("animation")
        block_textures = block.get("textures", [])
        frame_count = animation.get("frame_count") if isinstance(animation, dict) else None
        if len(block_textures) == 6 and isinstance(frame_count, int) and frame_count > 1:
            for stem in block_textures:
                if isinstance(stem, str):
                    animated_texture_frames[stem] = max(
                        animated_texture_frames.get(stem, 1), frame_count
                    )
    unique_pixels: set[str] = set()
    duplicate_pixels: list[str] = []
    alpha_extrema: dict[str, tuple[int, int]] = {}
    for path in textures:
        try:
            image = Image.open(path).convert("RGBA")
        except Exception as exc:
            errors.append(f"invalid image {path.name}: {exc}")
            continue
        frame_count = animated_texture_frames.get(path.stem, 1)
        resolution = int(pack_manifest.get("resolution", 16))
        expected_size = (resolution, resolution * frame_count)
        if image.size != expected_size:
            errors.append(f"{path.name}: expected {expected_size[0]}x{expected_size[1]}, got {image.size}")
            continue
        role = manifest_by_stem.get(path.stem, {}).get("role")
        colors = image.getcolors(maxcolors=257)
        palette_limit = 48 if role == "leaf_cutout" else 128 if role in COMPOSITE_SIDE_ROLES else 32
        if resolution <= 16 and (colors is None or len(colors) > palette_limit):
            errors.append(
                f"{path.name}: palette exceeds {palette_limit} RGBA colors"
            )
        alpha_extrema[path.stem] = image.getchannel("A").getextrema()
        if role == "leaf_cutout":
            leaf_manifest = manifest_by_stem.get(path.stem, {})
            target_transparency = leaf_manifest.get(
                "alpha_mask_target_transparency",
                leaf_manifest.get("alpha_mask_reference_transparency"),
            )
            if isinstance(target_transparency, (int, float)):
                alpha_frame = image.getchannel("A").crop((0, 0, resolution, resolution))
                actual_holes = sum(value < 128 for value in alpha_frame.getdata())
                expected_holes = round(target_transparency * resolution * resolution)
                if actual_holes != expected_holes:
                    errors.append(
                        f"{path.name}: leaf transparency has {actual_holes} holes; "
                        f"manifest target requires {expected_holes}"
                    )
                source_kind = leaf_manifest.get("source", {}).get("kind")
                minimum_transparency = (
                    MIN_REPLACEMENT_LEAF_TRANSPARENCY
                    if source_kind == "minetest_reference_only"
                    else MIN_CATALOG_LEAF_TRANSPARENCY
                    if source_kind == "procedural_parameters"
                    else None
                )
                if (minimum_transparency is not None and
                        target_transparency < minimum_transparency):
                    errors.append(
                        f"{path.name}: leaf transparency {target_transparency:.3f} "
                        f"is below the authored minimum {minimum_transparency:.2f} "
                        f"for {source_kind}"
                    )
        if role == "decor_sprite":
            target_coverage = manifest_by_stem.get(path.stem, {}).get(
                "sprite_alpha_target_coverage",
                manifest_by_stem.get(path.stem, {}).get(
                    "sprite_alpha_reference_coverage"
                ),
            )
            if isinstance(target_coverage, (int, float)):
                expected_opaque = round(target_coverage * resolution * resolution)
                for frame_index in range(frame_count):
                    alpha_frame = image.getchannel("A").crop(
                        (0, frame_index * resolution,
                         resolution, (frame_index + 1) * resolution)
                    )
                    actual_opaque = sum(
                        value >= 128 for value in alpha_frame.getdata()
                    )
                    if actual_opaque != expected_opaque:
                        errors.append(
                            f"{path.name}: sprite frame {frame_index} has "
                            f"{actual_opaque} opaque texels; authored profile "
                            f"requires {expected_opaque}"
                        )
                        break
        for frame_index in range(frame_count):
            frame = image.crop((0, frame_index * resolution, resolution, (frame_index + 1) * resolution))
            # Layered side faces (grass over soil, snow over dirt, etc.) must
            # tile across neighboring blocks horizontally. Their top and
            # bottom rows carry different materials by design. A good wrapped
            # tile has edge-to-edge steps comparable to its internal steps;
            # exact equality created a repeated border on fine grain.
            periodic_axes = (
                ("x",) if role in COMPOSITE_SIDE_ROLES
                else ("x", "y")
            )
            discontinuous_axis = next((
                axis for axis in periodic_axes
                if not texture_wrap_is_continuous(frame, axis)
            ), None)
            if role != "decor_sprite" and discontinuous_axis:
                errors.append(
                    f"{path.name}: animation frame {frame_index} has an obvious "
                    f"{discontinuous_axis}-axis tile seam"
                )
                break
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
    manifest_path = pack_dir / "texture_overrides.json"
    overrides = json.loads(manifest_path.read_text(encoding="utf-8-sig")) if manifest_path.exists() else {}
    minetest_blocks = ensure_stable_block_ids(load_block_files("minetest_default_16"))
    geology_blocks = load_block_files("cubatarium_geology_cc0")
    replacement_maps = {
        "minetest_default_16": minetest_replacement_stems(),
        "cubatarium_geology_cc0": {path.stem: f"geo_{safe_name(path.stem)}" for path in (REPO / "resource_packs" / "cubatarium_geology_cc0" / "textures" / "blocks").glob("*.png")},
    }
    for source_pack, source_blocks in (("minetest_default_16", minetest_blocks), ("cubatarium_geology_cc0", geology_blocks)):
        replacements = replacement_maps[source_pack]
        for source_block in source_blocks:
            included = name_to_block.get(source_block["name"])
            if included is None:
                errors.append(f"standalone pack is missing {source_pack} block {source_block['name']}")
                continue
            try:
                expected_textures = remap_block_textures(source_block, replacements)["textures"]
            except ValueError as exc:
                errors.append(str(exc))
                continue
            if included.get("textures") != expected_textures:
                errors.append(f"{source_pack}/{source_block['name']}: standalone block definition does not use its replacement textures")
            if source_pack == "minetest_default_16" and included.get("id") != source_block.get("id"):
                errors.append(f"{source_pack}/{source_block['name']}: imported block id changed from {source_block.get('id')} to {included.get('id')}")
            elif source_pack == "cubatarium_geology_cc0" and (
                not isinstance(included.get("id"), int) or not 1025 <= included["id"] <= 4095
            ):
                errors.append(f"{source_pack}/{source_block['name']}: integrated block id is outside the Cubatarium range")
            if included.get("license") != "CC0-1.0":
                errors.append(f"{source_pack}/{source_block['name']}: included Cubatarium block definition must declare CC0-1.0")
            expected_source_kind = "Cubatarium-authored block definition" if source_pack == "minetest_default_16" else None
            included_provenance = included.get("provenance", {})
            if expected_source_kind and (not isinstance(included_provenance, dict) or included_provenance.get("source_kind") != expected_source_kind):
                errors.append(f"{source_pack}/{source_block['name']}: missing Cubatarium-authored provenance")
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
    face_names = FACE_NAMES
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
    counts = provenance.get("counts", {})
    replaced = counts.get("minetest_textures_replaced", 0)
    if replaced < minetest_count:
        errors.append(f"Minetest texture replacement count {replaced} below source file count {minetest_count}")
    if len(manifest_textures) != len(textures):
        errors.append(f"asset manifest texture entries {len(manifest_textures)} do not match PNG count {len(textures)}")
    if len({item.get("stem") for item in manifest_textures if isinstance(item, dict)}) != len(manifest_textures):
        errors.append("asset manifest has duplicate texture stems")
    minetest_manifest_sources = {item.get("source", {}).get("reference_only") for item in manifest_textures if item.get("source", {}).get("kind") == "minetest_reference_only"}
    expected_minetest_sources = {path.stem for path in (REPO / "resource_packs" / "minetest_default_16" / "textures" / "blocks").glob("*.png")}
    if minetest_manifest_sources != expected_minetest_sources:
        errors.append(f"Minetest replacement source coverage differs: missing={sorted(expected_minetest_sources - minetest_manifest_sources)[:8]}, extra={sorted(minetest_manifest_sources - expected_minetest_sources)[:8]}")
    for stem, extrema in alpha_extrema.items():
        item = manifest_by_stem.get(stem, {})
        family = item.get("family")
        role = item.get("role")
        has_transparency = extrema[0] < 255
        if requires_clear_glass_cutouts(stem, family) and (extrema[0] != 0 or extrema[1] < 128):
            errors.append(f"{stem}.png: glass surface must have transparent interior and visible bevel pixels")
        elif family == "glass" and not has_transparency:
            errors.append(f"{stem}.png: glass texture must include alpha transparency")
        elif role in ("decor_sprite", "leaf_cutout") and not has_transparency:
            errors.append(f"{stem}.png: {role} texture must include transparent pixels")
        elif family != "glass" and role not in ("decor_sprite", "leaf_cutout") and has_transparency:
            errors.append(f"{stem}.png: non-glass texture unexpectedly includes transparency")
    object_count = len(list(objects_dir.glob("*.json"))) if objects_dir.exists() else 0
    block_names = set(name_to_block)
    block_materials = {
        name: (block.get("assetgen", {}).get("family"), block.get("assetgen", {}).get("material"))
        for name, block in name_to_block.items()
        if isinstance(block.get("assetgen"), dict)
    }
    passable_organic_materials = {
        "fern", "reed", "kelp", "coral", "blossom", "mushroom_stem",
    }
    for name, (family, material) in block_materials.items():
        if family != "organic" or not material:
            continue
        is_passable_plant = (
            material in passable_organic_materials
            or material == "mushroom"
            or material.startswith(("flower_", "mushroom_"))
            or is_leaf_material(family, material, name)
        )
        if not is_passable_plant:
            continue
        block = name_to_block[name]
        render = block.get("render", {})
        movement = block.get("physics", {}).get("movement", {})
        if render.get("style") not in ("cross", "cutout"):
            errors.append(f"{name}: decorative plant must use cross or cutout rendering")
        if float(movement.get("occupancy", 1.0)) >= 1.0:
            errors.append(f"{name}: decorative plant must be passable")
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
        object_materials: set[tuple[str | None, str | None]] = set()
        voxel_types: list[str] = []
        for voxel in voxels:
            if not isinstance(voxel, dict) or voxel.get("type") not in block_names:
                errors.append(f"{object_path.name}: unresolved voxel block {voxel.get('type') if isinstance(voxel, dict) else None!r}")
                break
            object_materials.add(block_materials.get(voxel["type"], (None, None)))
            voxel_types.append(str(voxel["type"]))
            try:
                position = (int(voxel["dx"]), int(voxel["dy"]), int(voxel["dz"]))
            except (KeyError, TypeError, ValueError):
                errors.append(f"{object_path.name}: voxel coordinate is missing or invalid")
                break
            if position in positions:
                errors.append(f"{object_path.name}: duplicate voxel coordinate {position}")
                break
            positions.add(position)
        object_name = object_path.stem
        organic_materials = {
            material for family, material in object_materials if family == "organic" and material
        }
        if object_name.startswith("asset_tree_"):
            species = object_name.removeprefix("asset_tree_")
            wood_species = {
                material for family, material in object_materials if family == "wood" and material
            }
            conifers = {"cedar", "larch", "pine", "redwood", "spruce"}
            expected_foliage = "needle" if species in conifers else "leaf"
            if wood_species != {species} or organic_materials != {expected_foliage}:
                errors.append(
                    f"{object_path.name}: tree materials do not match its name "
                    f"(wood={sorted(wood_species)}, foliage={sorted(organic_materials)})"
                )
            wood_voxels = [
                voxel for voxel in voxels
                if block_materials.get(str(voxel.get("type")), (None, None))[0] == "wood"
            ]
            foliage_voxels = [
                voxel for voxel in voxels
                if block_materials.get(str(voxel.get("type")), (None, None)) == ("organic", expected_foliage)
            ]
            wood_rows = {int(voxel["dy"]) for voxel in wood_voxels}
            if wood_rows and any(y not in wood_rows for y in range(min(wood_rows), max(wood_rows) + 1)):
                errors.append(f"{object_path.name}: tree trunk has a gap in its vertical stem")
            if species in {"birch", "aspen", "poplar"} and foliage_voxels:
                canopy_width = max(int(voxel["dx"]) for voxel in foliage_voxels) - min(int(voxel["dx"]) for voxel in foliage_voxels) + 1
                if canopy_width > 7:
                    errors.append(f"{object_path.name}: columnar canopy is too wide for its named species ({canopy_width} voxels)")
            if species == "acacia" and foliage_voxels:
                canopy_width = max(int(voxel["dx"]) for voxel in foliage_voxels) - min(int(voxel["dx"]) for voxel in foliage_voxels) + 1
                canopy_height = max(int(voxel["dy"]) for voxel in foliage_voxels) - min(int(voxel["dy"]) for voxel in foliage_voxels) + 1
                if canopy_width < canopy_height + 2:
                    errors.append(f"{object_path.name}: acacia canopy should be wide and flat")
            if species == "willow" and foliage_voxels and wood_rows:
                foliage_bottom = min(int(voxel["dy"]) for voxel in foliage_voxels)
                if foliage_bottom >= max(wood_rows) - 2:
                    errors.append(f"{object_path.name}: willow foliage is missing its hanging curtains")
            if species == "mangrove" and wood_voxels:
                base_roots = [
                    voxel for voxel in wood_voxels
                    if int(voxel["dy"]) == min(wood_rows)
                    and abs(int(voxel["dx"])) + abs(int(voxel["dz"])) >= 2
                ]
                if not base_roots:
                    errors.append(f"{object_path.name}: mangrove is missing exposed stilt roots")
            if species in conifers and foliage_voxels:
                foliage_by_y: dict[int, int] = defaultdict(int)
                for voxel in foliage_voxels:
                    foliage_by_y[int(voxel["dy"])] += 1
                low_rows = sorted(foliage_by_y.items())[:2]
                high_rows = sorted(foliage_by_y.items(), reverse=True)[:2]
                if low_rows and high_rows and sum(count for _, count in low_rows) <= sum(count for _, count in high_rows):
                    errors.append(f"{object_path.name}: conifer crown does not taper toward its top")
        elif object_name.startswith("asset_boulder_"):
            material = object_name.removeprefix("asset_boulder_")
            if object_materials != {("rock", material)}:
                errors.append(f"{object_path.name}: boulder material does not match its name {sorted(object_materials)}")
        elif object_name.startswith("asset_shrub_"):
            material = object_name.removeprefix("asset_shrub_")
            expected = {material, "leaf"} if material == "blossom" else {material}
            if organic_materials != expected:
                errors.append(f"{object_path.name}: shrub material does not match its name {sorted(organic_materials)}")
        elif object_name.startswith("asset_flower_patch_"):
            if not organic_materials or any(not material.startswith("flower_") for material in organic_materials):
                errors.append(
                    f"{object_path.name}: flower patch contains non-flower organic material "
                    f"{sorted(organic_materials)}"
                )
            if any(family != "organic" for family, _ in object_materials):
                errors.append(f"{object_path.name}: flower patch must not replace ground voxels")
        elif object_name.startswith("asset_mushroom_cluster_"):
            if not any(
                material.startswith("mushroom_") and material != "mushroom_stem"
                for material in organic_materials
            ):
                errors.append(f"{object_path.name}: mushroom cluster is missing a mushroom sprite")
            if any(family != "organic" for family, _ in object_materials):
                errors.append(f"{object_path.name}: mushroom cluster must not replace ground voxels")
        elif object_name == "asset_coral_fan":
            if ("organic", "coral") not in object_materials or ("soil", "pale_sand") not in object_materials:
                errors.append(f"{object_path.name}: coral fan needs coral over a sand base")
        elif object_name == "asset_crystal_cluster":
            if not any(family == "crystal" for family, _ in object_materials) or not any(family == "rock" for family, _ in object_materials):
                errors.append(f"{object_path.name}: crystal cluster needs crystals anchored in rock")
        elif object_name == "asset_mossy_ruin_piece":
            if not any(family == "masonry" for family, _ in object_materials) or ("organic", "moss") not in object_materials:
                errors.append(f"{object_path.name}: mossy ruin needs masonry and visible moss")

        placement = obj.get("placement", {})
        placement_mode = placement.get("mode") if isinstance(placement, dict) else None
        if (
            object_name.startswith(("asset_flower_patch_", "asset_mushroom_cluster_"))
            and placement_mode != "vertical_plant"
        ):
            errors.append(f"{object_path.name}: plant decor must use passable vertical_plant placement")
        if placement_mode == "vertical_plant":
            anchor = obj.get("anchor", [0, 0, 0])
            anchor_y = anchor[1] if isinstance(anchor, list) and len(anchor) == 3 else 0
            if any(
                int(voxel["dy"]) != anchor_y
                for voxel in voxels
                if isinstance(voxel, dict) and "dy" in voxel
            ):
                errors.append(f"{object_path.name}: vertical plant voxels must share the anchor layer")
            if any(
                float(name_to_block[block_type].get("physics", {}).get("movement", {}).get("occupancy", 1.0)) >= 1.0
                for block_type in voxel_types
            ):
                errors.append(f"{object_path.name}: vertical plant object contains a blocking voxel")
        elif placement_mode == "surface_layer":
            solid_dy: set[int] = set()
            non_solid_dy: set[int] = set()
            for voxel in voxels:
                if not isinstance(voxel, dict) or voxel.get("type") not in name_to_block:
                    continue
                block = name_to_block[voxel["type"]]
                occupancy = float(block.get("physics", {}).get("movement", {}).get("occupancy", 1.0))
                (solid_dy if occupancy >= 1.0 else non_solid_dy).add(int(voxel["dy"]))
            if len(solid_dy) != 1:
                errors.append(f"{object_path.name}: surface-layer ground voxels must occupy one height")
            elif non_solid_dy and non_solid_dy != {next(iter(solid_dy)) + 1}:
                errors.append(f"{object_path.name}: surface-layer decor must sit directly above its ground layer")
    return errors, {
        "blocks": len(blocks),
        "procedural_cc0_blocks": len(family_records(load_catalog())),
        "integrated_minetest_blocks": len(minetest_blocks),
        "integrated_geology_blocks": len(geology_blocks),
        "textures": len(textures),
        "unique_pixel_textures": len(unique_pixels),
        "minetest_textures": minetest_count,
        "minetest_overrides": len(overrides),
        "objects": object_count,
    }


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
    build.add_argument("--profile", choices=("voxel16", "photo64"), default="voxel16")
    build.add_argument("--reference-mode", choices=("qa-only", "aggregate-calibration"), default="qa-only")
    audit = sub.add_parser("audit", help="check generated pack against counts and current Minetest references")
    audit.add_argument("--minecraft-version", default="26.3")
    audit.add_argument("--pack", type=Path, default=PACK_DIR)
    benchmark = sub.add_parser("benchmark-minecraft", help="count extracted Minecraft assets without copying them")
    benchmark.add_argument("--minecraft-root", type=Path, required=True)
    benchmark.add_argument("--version", default="26.3")
    texture = sub.add_parser("texture", help="generate a single 16x16 or 64x64 material texture")
    texture.add_argument("action", nargs="?", choices=("batch",), help="use 'batch' to build the texture catalog")
    texture.add_argument("--material")
    texture.add_argument("--family", choices=sorted(FAMILY_COLORS))
    texture.add_argument("--variant", default="natural")
    texture.add_argument("--role", choices=("side", "top", "bottom", "endgrain", "grass_side", "dry_grass_side", "snow_side", "litter_side", "grass_top", "dry_grass_top", "snow_top", "litter_top", "water_surface", "ore"), default="side")
    texture.add_argument("--profile", choices=("voxel16", "voxel64", "photo64"), default="voxel16")
    texture.add_argument("--reference-mode", choices=("qa-only", "aggregate-calibration"), default="qa-only")
    texture.add_argument("--reference-pack", choices=("minetest_default_16", "cubatarium_geology_cc0"), default="minetest_default_16")
    texture.add_argument("--prompt")
    texture.add_argument("--input", type=Path)
    texture.add_argument("--source-license", choices=("CC0-1.0", "CC-BY-3.0", "CC-BY-SA-3.0", "CC-BY-4.0", "CC-BY-SA-4.0", "REVIEW_REQUIRED"), default="REVIEW_REQUIRED")
    texture.add_argument("--output", type=Path)
    texture.add_argument("--output-dir", type=Path)
    texture.add_argument("--catalog", type=Path)
    texture.add_argument("--backend", choices=("auto", "procedural", "quilting", "diffusers"), default="auto")
    texture.add_argument("--model")
    texture.add_argument("--revision", help="pin a model revision when allowing runtime downloads")
    texture.add_argument("--allow-download", action="store_true", help="allow the model backend to access the network")
    texture.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    texture.add_argument("--resource-profile", choices=("offline", "cpu", "low-vram", "balanced", "quality"), default="balanced")
    texture.add_argument("--seed", type=int, default=0)
    texture.add_argument("--strength", type=float, default=0.55)
    texture.add_argument("--seam", type=int, help="seam blend width; defaults by material family")
    texture.add_argument("--quiet", action="store_true", help="suppress the generation manifest on stdout")
    calibrate = sub.add_parser("calibrate", help="derive only aggregate texture statistics from a reference pack")
    calibrate.add_argument("--reference-pack", choices=("minetest_default_16", "cubatarium_geology_cc0"), default="minetest_default_16")
    calibrate.add_argument("--output", type=Path)
    compare = sub.add_parser("compare", help="create reference contact sheets and broad material/detail diagnostics")
    compare.add_argument("--reference-pack", choices=("minetest_default_16", "cubatarium_geology_cc0"), default="minetest_default_16")
    compare.add_argument("--reference-mode", choices=("qa-only", "aggregate-calibration"), default="qa-only")
    compare.add_argument("--pack", type=Path, default=PACK_DIR)
    compare.add_argument("--output", type=Path, default=REPO / "assetgen_output" / "reference_qa" / "minetest_default_16")
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
    validate = sub.add_parser("validate", help="validate pack definitions and their declared texture resolution")
    validate.add_argument("--pack", type=Path, default=PACK_DIR)
    preview = sub.add_parser("preview", help="create contact sheets or a voxel object preview")
    preview_target = preview.add_mutually_exclusive_group(required=True)
    preview_target.add_argument("--textures", type=Path)
    preview_target.add_argument("--object", type=Path)
    preview_target.add_argument("--blocks", type=Path, help="resource pack directory for six-face contact sheets")
    preview_target.add_argument("--compare-reference", choices=("minetest_default_16", "cubatarium_geology_cc0"))
    preview.add_argument("--output", type=Path)
    preview.add_argument("--pack", type=Path, default=PACK_DIR)
    preview.add_argument("--limit", type=int, default=120, help="maximum number of representative blocks in the face preview")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = make_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "build-pack":
            result = build_photo64_overlay(args.no_preview, args.reference_mode) if args.profile == "photo64" else build_pack(args.no_preview, not args.no_worldgen, args.reference_mode, 16)
            print(json.dumps(result, ensure_ascii=False))
            return 0
        if args.command == "audit":
            return command_audit(args)
        if args.command == "benchmark-minecraft":
            return command_benchmark(args)
        if args.command == "calibrate":
            return command_calibrate(args)
        if args.command == "compare":
            return command_compare(args)
        if args.command == "texture":
            if args.seam is not None and not 0 <= args.seam <= 8:
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
