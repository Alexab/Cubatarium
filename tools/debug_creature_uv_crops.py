#!/usr/bin/env python3
"""Draw manual/auto UV crop rects on Luanti mob atlases for validation."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import yaml
from PIL import Image, ImageDraw

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
RESEARCH_DEFAULT = Path(r"E:/Work/Home/CubatariumTextureResearch")

from bake_rigid_creature_textures import load_atlas, load_yaml


def draw_rects(
    atlas: Image.Image,
    rects: dict[str, tuple[float, float, float, float]],
    out_path: Path,
) -> None:
    w, h = atlas.size
    overlay = atlas.convert("RGBA").copy()
    draw = ImageDraw.Draw(overlay, "RGBA")
    colors = {
        "body": (0, 200, 80, 120),
        "face": (255, 80, 80, 120),
        "leg": (80, 120, 255, 120),
        "arm": (255, 200, 0, 120),
    }
    for stem, rect in rects.items():
        u0, v0, u1, v1 = rect
        box = (int(u0 * w), int(v0 * h), int(u1 * w), int(v1 * h))
        color = colors.get(stem, (255, 255, 255, 100))
        draw.rectangle(box, outline=color[:3] + (255,), width=2)
        draw.rectangle(box, fill=color)
        draw.text((box[0] + 2, box[1] + 2), stem, fill=(255, 255, 255, 255))
    out_path.parent.mkdir(parents=True, exist_ok=True)
    overlay.save(out_path)
    print(f"wrote {out_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--research", type=Path, default=RESEARCH_DEFAULT)
    parser.add_argument("--species", action="append")
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=ROOT / "tools" / "debug_uv_overlays",
    )
    args = parser.parse_args()

    sources = load_yaml(TOOLS / "creature_luanti_sources.yaml")
    species_list = args.species or list(sources["species"].keys())
    research = args.research.resolve()

    for species_id in species_list:
        spec = sources["species"][species_id]
        manual = spec.get("manual_uv", {})
        if not manual:
            print(f"skip {species_id} (no manual_uv)")
            continue
        atlas = load_atlas(spec, research)
        rects = {k: tuple(v) for k, v in manual.items() if len(v) == 4}
        draw_rects(atlas, rects, args.out_dir / f"{species_id}_uv_overlay.png")


if __name__ == "__main__":
    main()
