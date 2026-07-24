#!/usr/bin/env python3
"""Overlay Minecraft 64x32 skin regions on character.png for UV audit."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from import_luanti_creature_textures import SKIN64x32, skin_regions

RESEARCH_DEFAULT = Path(r"E:/Work/Home/CubatariumTextureResearch")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--skin",
        type=Path,
        default=RESEARCH_DEFAULT
        / "minetest_game/mods/player_api/models/character.png",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=ROOT / "tools" / "debug_uv_overlays" / "human_skin_overlay.png",
    )
    args = parser.parse_args()
    skin = Image.open(args.skin).convert("RGBA")
    overlay = skin.copy()
    draw = ImageDraw.Draw(overlay, "RGBA")
    w, h = skin.size
    colors = [
        (255, 80, 80, 100),
        (80, 200, 80, 100),
        (80, 120, 255, 100),
        (255, 200, 0, 100),
    ]
    for i, (name, box) in enumerate(SKIN64x32.items()):
        x, y, bw, bh = box
        color = colors[i % len(colors)]
        draw.rectangle((x, y, x + bw, y + bh), outline=color[:3] + (255,), width=1)
        draw.rectangle((x, y, x + bw, y + bh), fill=color)
        draw.text((x + 1, y + 1), name, fill=(255, 255, 255, 255))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    overlay.save(args.out)
    print(f"wrote {args.out}")
    regions = skin_regions(skin)
    print("regions:", ", ".join(sorted(regions.keys())))


if __name__ == "__main__":
    main()
