#!/usr/bin/env python3
"""Rebuild git-tracked CC0 resource packs from Kenney Voxel Pack tiles."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pillow", "-q"])
    from PIL import Image

REPO = Path(__file__).resolve().parents[1]
KENNEY_TILES = Path(r"E:/Work/Home/CubatariumTextureResearch/kenney_voxel_pack/PNG/Tiles")
KENNEY_URL = "https://opengameart.org/sites/default/files/voxel-pack-updated.zip"

# Minimal survival set for cubatarium_cc0_base (fallback pack).
BASE_STEM_TO_KENNEY: dict[str, str] = {
    "dirt": "dirt",
    "stone": "stone",
    "grass_side": "dirt_grass",
    "grass_top": "grass_top",
    "tree_side": "trunk_side",
    "tree_top": "trunk_top",
    "sand": "sand",
    "sandstone": "greystone_sand",
    "gravel": "gravel_stone",
    "snow": "snow",
    "clay": "stone_dirt",
    "ice": "ice",
    "hellrock": "rock",
    "bedrock": "greystone",
}

# Extended Kenney mapping for kenney_voxel_* packs (~43 sandbox blocks).
KENNEY_STEM_TO_KENNEY: dict[str, str] = {
    **BASE_STEM_TO_KENNEY,
    "glass": "glass",
    "obsidian": "greystone",
    "hellsand": "redsand",
    "whiteStone": "greystone",
    "oreCoal": "stone_coal",
    "oreIron": "stone_iron",
    "oreGold": "stone_gold",
    "oreDiamond": "stone_diamond",
    "oreEmerald": "redstone_emerald",
    "oreLapis": "stone_silver",
    "oreRedstone": "redstone",
    "blockIron": "stone_iron",
    "blockGold": "stone_gold",
    "blockDiamond": "redstone_emerald",
    "blockEmerald": "redstone_emerald",
    "blockLapis": "stone_silver",
    "blockRedstone": "redstone",
    "stoneMoss": "rock_moss",
    "stonebricksmooth": "greystone",
    "stonebricksmooth_carved": "brick_grey",
    "stonebricksmooth_cracked": "greystone",
    "stonebricksmooth_mossy": "rock_moss",
    "cactus_side": "cactus_side",
    "cactus_top": "cactus_top",
    "cactus_bottom": "cactus_inside",
    "leaves_opaque": "leaves",
    "melon_side": "leaves_orange",
    "melon_top": "leaves_orange",
    "brick": "brick_red",
    "cloth_0": "cotton_tan",
    "cloth_1": "cotton_red",
    "cloth_2": "cotton_blue",
    "cloth_3": "cotton_green",
    "cloth_4": "cotton_tan",
    "cloth_5": "cotton_red",
    "cloth_6": "cotton_blue",
    "cloth_7": "cotton_green",
    "cloth_8": "cotton_tan",
    "cloth_9": "cotton_red",
    "cloth_10": "cotton_blue",
    "cloth_11": "cotton_green",
    "cloth_12": "cotton_tan",
    "cloth_13": "cotton_red",
    "cloth_14": "cotton_blue",
    "cloth_15": "cotton_green",
}

ANIMATED_STEMS: dict[str, list[str]] = {
    "water": ["water", "water", "water", "water"],
    "lava": ["lava", "lava", "lava", "lava"],
    "fire_0": ["redsand", "redstone"],
}

PACK_SPECS = [
    {
        "mapping": REPO / "tools/cubatarium_cc0_mapping.yaml",
        "stem_map": BASE_STEM_TO_KENNEY,
        "out": REPO / "resource_packs/cubatarium_cc0_base",
        "pack_id": "cubatarium_cc0_base",
        "name": "Cubatarium CC0 Base",
        "priority": 10,
        "resolution": 16,
    },
    {
        "mapping": REPO / "tools/kenney_full_mapping.yaml",
        "stem_map": KENNEY_STEM_TO_KENNEY,
        "out": REPO / "resource_packs/kenney_voxel_16",
        "pack_id": "kenney_voxel_16",
        "name": "Kenney Voxel 16",
        "priority": 5,
        "resolution": 16,
    },
    {
        "mapping": REPO / "tools/kenney_full_mapping.yaml",
        "stem_map": KENNEY_STEM_TO_KENNEY,
        "out": REPO / "resource_packs/kenney_voxel_128",
        "pack_id": "kenney_voxel_128",
        "name": "Kenney Voxel 128",
        "priority": 5,
        "resolution": 128,
    },
]


def ensure_kenney_tiles() -> None:
    if KENNEY_TILES.is_dir() and any(KENNEY_TILES.glob("*.png")):
        return
    print("Kenney tiles missing — downloading voxel-pack-updated.zip …")
    import tempfile
    import zipfile

    try:
        import requests
    except ImportError:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "requests", "-q"])
        import requests

    dest_root = KENNEY_TILES.parents[2]
    dest_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        archive = Path(tmp) / "kenney.zip"
        with requests.get(KENNEY_URL, stream=True, timeout=120) as resp:
            resp.raise_for_status()
            archive.write_bytes(resp.content)
        with zipfile.ZipFile(archive) as zf:
            zf.extractall(dest_root)
    if not any(KENNEY_TILES.glob("*.png")):
        raise SystemExit(f"Kenney tiles not found under {KENNEY_TILES}")


def load_kenney_image(tile_name: str) -> Image.Image:
    path = KENNEY_TILES / f"{tile_name}.png"
    if not path.is_file():
        raise FileNotFoundError(f"Missing Kenney tile: {path}")
    return Image.open(path).convert("RGBA")


def resize_tile(img: Image.Image, size: int) -> Image.Image:
    if img.size == (size, size):
        return img
    return img.resize((size, size), Image.Resampling.NEAREST)


def write_strip(stem: str, frame_tiles: list[str], out_dir: Path, frame_size: int) -> None:
    frames = [resize_tile(load_kenney_image(name), frame_size) for name in frame_tiles]
    total_h = frame_size * len(frames)
    strip = Image.new("RGBA", (frame_size, total_h))
    for i, frame in enumerate(frames):
        strip.paste(frame, (0, i * frame_size))
    out_dir.mkdir(parents=True, exist_ok=True)
    strip.save(out_dir / f"{stem}.png")


def write_static(stem: str, tile_name: str, out_dir: Path, size: int) -> None:
    img = resize_tile(load_kenney_image(tile_name), size)
    out_dir.mkdir(parents=True, exist_ok=True)
    img.save(out_dir / f"{stem}.png")


def export_staging(staging: Path, resolution: int, stem_map: dict[str, str]) -> None:
    if staging.exists():
        import shutil

        shutil.rmtree(staging)
    tex = staging / "textures" / "blocks"
    tex.mkdir(parents=True)

    for stem, kenney_name in stem_map.items():
        write_static(stem, kenney_name, tex, resolution)

    for stem, frames in ANIMATED_STEMS.items():
        write_strip(stem, frames, tex, resolution)

    print(f"  staged {len(list(tex.glob('*.png')))} textures at {resolution}px")


def build_pack(spec: dict, staging: Path) -> None:
    from build_resource_pack import build_pack as build_pack_impl
    import yaml

    mapping = yaml.safe_load(spec["mapping"].read_text(encoding="utf-8"))
    build_pack_impl(
        mapping,
        staging / "textures" / "blocks",
        spec["out"],
        spec["pack_id"],
        spec["priority"],
        spec["resolution"],
    )
    pack_json = spec["out"] / "pack.json"
    import json

    data = json.loads(pack_json.read_text(encoding="utf-8"))
    data["name"] = spec["name"]
    pack_json.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def validate_pack(path: Path) -> None:
    validator = REPO / "tools/validate_resource_pack.py"
    if validator.is_file():
        subprocess.check_call([sys.executable, str(validator), str(path)])


def main() -> int:
    sys.path.insert(0, str(REPO / "tools"))
    ensure_kenney_tiles()
    print(f"Using Kenney tiles: {KENNEY_TILES}")

    staging_by_res: dict[int, Path] = {}

    for spec in PACK_SPECS:
        res = spec["resolution"]
        stem_map = spec["stem_map"]
        staging_key = (res, id(stem_map))
        staging = REPO / "resource_packs" / f"_build_staging_{res}_{'full' if stem_map is KENNEY_STEM_TO_KENNEY else 'base'}"
        print(f"\n[{spec['pack_id']}] resolution {res}px, {len(stem_map)} stems")
        if not staging.exists():
            export_staging(staging, res, stem_map)
        else:
            export_staging(staging, res, stem_map)
        build_pack(spec, staging)
        validate_pack(spec["out"])
        block_count = len(list((spec["out"] / "blocks").glob("*.json")))
        tex_count = len(list((spec["out"] / "textures" / "blocks").glob("*.png")))
        print(f"  -> {spec['out']} ({block_count} blocks, {tex_count} textures)")

    print("\nDone. Rebuild/run the game to pick up new textures.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
