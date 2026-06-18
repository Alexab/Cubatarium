#!/usr/bin/env python3
"""Build resource packs from CubatariumTextureResearch sources."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from typing import Any

try:
    from PIL import Image
except ImportError:
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pillow", "-q"])
    from PIL import Image

try:
    import yaml
except ImportError:
    subprocess.check_call([sys.executable, "-m", "pip", "install", "pyyaml", "-q"])
    import yaml

REPO = Path(__file__).resolve().parents[1]
RESEARCH = Path(r"E:/Work/Home/CubatariumTextureResearch")

PACK_SPECS = [
    {
        "pack_id": "minetest_default_16",
        "name": "Minetest Default 16",
        "mapping": REPO / "tools/minetest_mapping.yaml",
        "source": RESEARCH / "minetest_default" / "textures",
        "out": REPO / "resource_packs/minetest_default_16",
        "priority": 4,
        "resolution": 16,
        "source_is_flat": True,
    },
    {
        "pack_id": "seamless_patterns_16",
        "name": "Seamless Patterns 16",
        "mapping": REPO / "tools/seamless_patterns_mapping.yaml",
        "source": RESEARCH / "seamless_pattern_pack",
        "out": REPO / "resource_packs/seamless_patterns_16",
        "priority": 7,
        "resolution": 16,
        "source_is_flat": True,
    },
    {
        "pack_id": "kenney_pattern_pixel_16",
        "name": "Kenney Pattern Pixel 16",
        "mapping": REPO / "tools/kenney_pattern_pixel_mapping.yaml",
        "source": RESEARCH / "kenney_pattern_pixel",
        "out": REPO / "resource_packs/kenney_pattern_pixel_16",
        "priority": 8,
        "resolution": 16,
        "source_is_flat": False,
    },
    {
        "pack_id": "goncalo_patterns_16",
        "name": "Goncalo Pixel Patterns 16",
        "mapping": REPO / "tools/goncalo_patterns_mapping.yaml",
        "source": RESEARCH / "goncalo_pixel_patterns",
        "out": REPO / "resource_packs/goncalo_patterns_16",
        "priority": 8,
        "resolution": 16,
        "source_is_flat": False,
    },
    {
        "pack_id": "sbs_sandbox_terrain_16",
        "name": "SBS Sandbox Terrain 16",
        "mapping": REPO / "tools/sbs_sandbox_mapping.yaml",
        "source": RESEARCH / "sbs_sandbox_terrain",
        "out": REPO / "resource_packs/sbs_sandbox_terrain_16",
        "priority": 8,
        "resolution": 16,
        "source_is_flat": False,
    },
    {
        "pack_id": "kenney_pattern_lines_16",
        "name": "Kenney Pattern Lines 16",
        "mapping": REPO / "tools/kenney_pattern_lines_mapping.yaml",
        "source": RESEARCH / "kenney_pattern_lines",
        "out": REPO / "resource_packs/kenney_pattern_lines_16",
        "priority": 9,
        "resolution": 16,
        "source_is_flat": False,
    },
    {
        "pack_id": "oga_mc_inspired_16",
        "name": "OGA MC Inspired 16",
        "mapping": REPO / "tools/oga_mc_inspired_mapping.yaml",
        "source": RESEARCH / "oga_mc_inspired" / "files",
        "out": REPO / "resource_packs/oga_mc_inspired_16",
        "priority": 9,
        "resolution": 16,
        "source_is_flat": True,
    },
    {
        "pack_id": "refi_textures_16",
        "name": "REFI Textures 16",
        "mapping": REPO / "tools/refi_textures_mapping.yaml",
        "source": RESEARCH / "refi_textures" / "textures",
        "out": REPO / "resource_packs/refi_textures_16",
        "priority": 5,
        "resolution": 16,
        "source_is_flat": False,
    },
    {
        "pack_id": "programmer_art_16",
        "name": "ProgrammerArt 16",
        "mapping": REPO / "tools/programmer_art_mapping.yaml",
        "source": RESEARCH / "programmer_art" / "textures" / "blocks",
        "out": REPO / "resource_packs/programmer_art_16",
        "priority": 5,
        "resolution": 16,
        "source_is_flat": True,
    },
    {
        "pack_id": "snez_16",
        "name": "Snez 16",
        "mapping": REPO / "tools/snez_mapping.yaml",
        "source": RESEARCH / "snez" / "Snez",
        "out": REPO / "resource_packs/snez_16",
        "priority": 6,
        "resolution": 16,
        "source_is_flat": True,
    },
    {
        "pack_id": "too_many_stones_16",
        "name": "Too Many Stones 16",
        "mapping": REPO / "tools/too_many_stones_mapping.yaml",
        "source": RESEARCH / "too_many_stones" / "textures",
        "out": REPO / "resource_packs/too_many_stones_16",
        "priority": 7,
        "resolution": 16,
        "source_is_flat": True,
    },
]


def resize_square(img: Image.Image, size: int) -> Image.Image:
    if img.size == (size, size):
        return img
    return img.resize((size, size), Image.Resampling.NEAREST)


def parse_texture_ref(ref: Any) -> tuple[str, int | None, tuple[int, int, int, int] | None, dict | None]:
    """Return (relative_path, sheet_tile_index, crop_box, composite_spec)."""
    if isinstance(ref, str):
        return ref, None, None, None
    if isinstance(ref, dict):
        if "composite" in ref:
            return "", None, None, ref["composite"]
        if "sheet" in ref:
            return ref["sheet"], int(ref.get("tile", 0)), None, None
        if "file" in ref:
            crop = ref.get("crop")
            box = tuple(crop) if crop else None
            return ref["file"], None, box, None
    raise ValueError(f"Invalid texture ref: {ref!r}")


def composite_images(base: Image.Image, overlay: Image.Image) -> Image.Image:
    """Luanti-style alpha overlay (^ operator)."""
    base = base.convert("RGBA")
    overlay = overlay.convert("RGBA")
    if overlay.size != base.size:
        overlay = overlay.resize(base.size, Image.Resampling.NEAREST)
    out = base.copy()
    out.alpha_composite(overlay)
    return out


def load_texture_image(source_root: Path, ref: Any, flat: bool) -> Image.Image:
    rel, tile_idx, crop, composite = parse_texture_ref(ref)
    if composite:
        base_img = load_texture_image(source_root, composite["base"], flat)
        overlay_img = load_texture_image(source_root, composite["overlay"], flat)
        return composite_images(base_img, overlay_img)

    path = source_root / rel
    if not path.is_file() and flat:
        alt = source_root / Path(rel).name
        if alt.is_file():
            path = alt
    if not path.is_file():
        stem = Path(rel).stem
        matches = list(source_root.rglob(f"{stem}.png"))
        if not matches:
            matches = list(source_root.rglob(f"*{Path(rel).name}"))
        if not matches:
            raise FileNotFoundError(f"Texture not found: {rel} under {source_root}")
        path = matches[0]

    img = Image.open(path).convert("RGBA")
    if tile_idx is not None:
        tw = th = 128
        cols = img.width // tw
        col = tile_idx % cols
        row = tile_idx // cols
        img = img.crop((col * tw, row * th, (col + 1) * tw, (row + 1) * th))
    elif crop:
        x, y, w, h = crop
        img = img.crop((x, y, x + w, y + h))
    return img


def write_static(stem: str, ref: Any, source_root: Path, out_dir: Path, size: int, flat: bool) -> None:
    img = resize_square(load_texture_image(source_root, ref, flat), size)
    out_dir.mkdir(parents=True, exist_ok=True)
    img.save(out_dir / f"{stem}.png")


def write_animated_or_strip(
    stem: str,
    ref: Any,
    source_root: Path,
    out_dir: Path,
    size: int,
    flat: bool,
) -> None:
    img = load_texture_image(source_root, ref, flat)
    w, h = img.size
    if h > w and w > 0 and h % w == 0:
        frames = h // w
        if w != size:
            img = img.resize((size, frames * size), Image.Resampling.NEAREST)
    else:
        img = resize_square(img, size)
    out_dir.mkdir(parents=True, exist_ok=True)
    img.save(out_dir / f"{stem}.png")


def collect_block_stems(blocks: dict) -> set[str]:
    stems: set[str] = set()
    for spec in blocks.values():
        if isinstance(spec, str):
            stems.add(spec)
        else:
            faces = spec.get("faces")
            if faces:
                stems.update(faces if len(faces) == 6 else faces[:6])
            elif "texture" in spec:
                stems.add(spec["texture"])
    return stems


def export_staging(spec: dict, mapping: dict, staging: Path) -> None:
    import shutil

    if staging.exists():
        shutil.rmtree(staging)
    tex_dir = staging / "textures" / "blocks"
    tex_dir.mkdir(parents=True)

    source_root: Path = spec["source"]
    flat = spec.get("source_is_flat", True)
    resolution = spec["resolution"]
    textures: dict[str, Any] = mapping.get("textures", {})
    animated: dict[str, list[Any]] = mapping.get("animated", {})

    needed = collect_block_stems(mapping.get("blocks", {}))
    for stem in sorted(needed):
        if stem in animated:
            frames = animated[stem]
            ref = frames[0] if len(frames) == 1 else frames
            if isinstance(ref, list):
                # multi-frame from separate files — stack vertically
                imgs = [resize_square(load_texture_image(source_root, r, flat), resolution) for r in ref]
                strip = Image.new("RGBA", (resolution, resolution * len(imgs)))
                for i, frame in enumerate(imgs):
                    strip.paste(frame, (0, i * resolution))
                strip.save(tex_dir / f"{stem}.png")
            else:
                write_animated_or_strip(stem, ref, source_root, tex_dir, resolution, flat)
        elif stem in textures:
            write_static(stem, textures[stem], source_root, tex_dir, resolution, flat)
        else:
            write_static(stem, f"{stem}.png", source_root, tex_dir, resolution, flat)

    print(f"  staged {len(list(tex_dir.glob('*.png')))} textures at {resolution}px")


def build_pack(spec: dict, staging: Path) -> None:
    from build_resource_pack import build_pack as build_pack_impl

    mapping = yaml.safe_load(spec["mapping"].read_text(encoding="utf-8"))
    build_pack_impl(
        mapping,
        staging / "textures" / "blocks",
        spec["out"],
        spec["pack_id"],
        spec["priority"],
        spec["resolution"],
    )
    data = json.loads((spec["out"] / "pack.json").read_text(encoding="utf-8"))
    data["name"] = spec["name"]
    mapping = yaml.safe_load(spec["mapping"].read_text(encoding="utf-8"))
    if mapping.get("pack_version"):
        data["version"] = mapping["pack_version"]
    (spec["out"] / "pack.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def validate_pack(path: Path) -> None:
    validator = REPO / "tools/validate_resource_pack.py"
    if validator.is_file():
        subprocess.check_call([sys.executable, str(validator), str(path)])


def main() -> int:
    sys.path.insert(0, str(REPO / "tools"))

    only = set(sys.argv[1:]) if len(sys.argv) > 1 else None
    specs = [s for s in PACK_SPECS if only is None or s["pack_id"] in only]

    for spec in specs:
        mapping_path = spec["mapping"]
        if not mapping_path.is_file():
            print(f"SKIP {spec['pack_id']}: missing {mapping_path}")
            continue
        if not spec["source"].exists():
            print(f"SKIP {spec['pack_id']}: missing source {spec['source']}")
            continue

        mapping = yaml.safe_load(mapping_path.read_text(encoding="utf-8"))
        staging = REPO / "resource_packs" / f"_build_staging_{spec['pack_id']}"
        print(f"\n[{spec['pack_id']}]")
        export_staging(spec, mapping, staging)
        build_pack(spec, staging)
        validate_pack(spec["out"])
        block_count = len(list((spec["out"] / "blocks").glob("*.json")))
        tex_count = len(list((spec["out"] / "textures" / "blocks").glob("*.png")))
        print(f"  -> {spec['out']} ({block_count} blocks, {tex_count} textures)")

    print("\nDone.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
