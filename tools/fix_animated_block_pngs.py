#!/usr/bin/env python3
"""Resize animated block PNGs to match block JSON (vertical strip or layer frames)."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
FACE_COUNT = 6


def fix_pack(pack_dir: Path, dry_run: bool) -> int:
    from animated_texture_utils import animation_mode, resample_strip, to_square_frame

    try:
        from PIL import Image
    except ImportError:
        raise SystemExit("Pillow required: pip install pillow")

    blocks_dir = pack_dir / "blocks"
    tex_dir = pack_dir / "textures" / "blocks"
    if not blocks_dir.is_dir():
        return 0
    fixed = 0
    for block_path in sorted(blocks_dir.glob("*.json")):
        try:
            block = json.loads(block_path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        anim = block.get("animation")
        if not anim:
            continue
        frame_count = anim.get("frame_count")
        if not isinstance(frame_count, int) or frame_count < 1:
            continue
        textures = block.get("textures", [])
        if not isinstance(textures, list) or not textures:
            continue
        mode = animation_mode(len(textures))
        if mode == "strip":
            stems = {s for s in textures[:FACE_COUNT] if isinstance(s, str)}
        elif mode == "layers":
            stems = {s for s in textures[:FACE_COUNT] if isinstance(s, str)}
        else:
            continue
        for stem in stems:
            png = tex_dir / f"{stem}.png"
            if not png.is_file():
                continue
            img = Image.open(png).convert("RGBA")
            w, h = img.size
            if mode == "strip":
                expected_h = w * frame_count
                if h == expected_h:
                    continue
                out = resample_strip(img, frame_count, w)
            else:
                if h == w:
                    continue
                out = to_square_frame(img, w)
            if dry_run:
                print(
                    f"would fix {png.relative_to(REPO)}: {w}x{h} -> {out.size[0]}x{out.size[1]} ({mode})"
                )
            else:
                out.save(png)
                print(
                    f"fixed {png.relative_to(REPO)}: {w}x{h} -> {out.size[0]}x{out.size[1]} ({mode})"
                )
            fixed += 1
    return fixed


def main() -> int:
    parser = argparse.ArgumentParser(description="Fix animated block PNGs for block JSON")
    parser.add_argument("--pack", type=str, required=True, help="Pack folder name under resource_packs/")
    parser.add_argument("--packs-dir", type=Path, default=REPO / "resource_packs")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    pack_dir = args.packs_dir.resolve() / args.pack
    if not pack_dir.is_dir():
        print(f"ERROR: pack not found: {pack_dir}")
        return 1
    n = fix_pack(pack_dir, args.dry_run)
    print(f"Done — {n} PNG(s) {'would be ' if args.dry_run else ''}fixed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
