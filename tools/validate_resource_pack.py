#!/usr/bin/env python3
"""Validate a Cubatarium resource pack directory."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

RESERVED_NAMES = {"__missing__", "__air__"}
FACE_COUNT = 6
CREATURE_REQUIRED_FIELDS = ("id", "display_name", "role")


def validate_creatures(pack_dir: Path, pack_id: str) -> int:
    """Optional creatures/ overlay validation (warnings only)."""
    creatures_dir = pack_dir / "creatures"
    if not creatures_dir.is_dir():
        return 0
    errors = 0
    for species_dir in sorted(creatures_dir.iterdir()):
        if not species_dir.is_dir():
            continue
        json_path = species_dir / "creature.json"
        if not json_path.is_file():
            warn(f"{species_dir}: missing creature.json")
            continue
        try:
            data = json.loads(json_path.read_text(encoding="utf-8-sig"))
        except json.JSONDecodeError as e:
            warn(f"{json_path}: {e}")
            errors += 1
            continue
        for key in CREATURE_REQUIRED_FIELDS:
            if key not in data:
                warn(f"{json_path}: missing recommended field '{key}'")
        cid = data.get("id", species_dir.name)
        if cid != species_dir.name:
            warn(f"{json_path}: id '{cid}' != folder '{species_dir.name}'")
        tex_dir = species_dir / "textures"
        if tex_dir.is_dir():
            pngs = list(tex_dir.glob("*.png"))
            if not pngs:
                warn(f"{tex_dir}: no PNG textures")
    if creatures_dir.is_dir():
        print(f"  creatures/: optional overlay OK ({pack_id})")
    return errors


def validate_skins(pack_dir: Path, pack_id: str) -> int:
    skins_dir = pack_dir / "skins"
    if not skins_dir.is_dir():
        return 0
    errors = 0
    for skin_dir in sorted(skins_dir.iterdir()):
        if not skin_dir.is_dir():
            continue
        json_path = skin_dir / "skin.json"
        if not json_path.is_file():
            warn(f"{skin_dir}: missing skin.json")
            continue
        try:
            data = json.loads(json_path.read_text(encoding="utf-8-sig"))
        except json.JSONDecodeError as e:
            warn(f"{json_path}: {e}")
            errors += 1
            continue
        if "id" not in data:
            warn(f"{json_path}: missing id")
    print(f"  skins/: optional overlay OK ({pack_id})")
    return errors


def err(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)


def warn(msg: str) -> None:
    print(f"WARN: {msg}", file=sys.stderr)


def validate_pack(pack_dir: Path) -> int:
    errors = 0
    pack_json = pack_dir / "pack.json"
    if not pack_json.is_file():
        err(f"missing pack.json in {pack_dir}")
        return 1

    try:
        manifest = json.loads(pack_json.read_text(encoding="utf-8-sig"))
    except json.JSONDecodeError as e:
        err(f"pack.json parse error: {e}")
        return 1

    for key in ("id", "priority", "license", "resolution"):
        if key not in manifest:
            warn(f"pack.json missing recommended field '{key}'")

    pack_id = manifest.get("id", pack_dir.name)
    blocks_dir = pack_dir / "blocks"
    tex_dir = pack_dir / "textures" / "blocks"
    if not blocks_dir.is_dir():
        err(f"missing blocks/ in {pack_dir}")
        return 1

    names: set[str] = set()
    for block_path in sorted(blocks_dir.glob("*.json")):
        try:
            data = json.loads(block_path.read_text(encoding="utf-8-sig"))
        except json.JSONDecodeError as e:
            err(f"{block_path}: {e}")
            errors += 1
            continue

        name = data.get("name", "")
        if not name:
            err(f"{block_path}: missing name")
            errors += 1
            continue
        if "::" in name:
            err(f"{block_path}: block name must not contain '::' (use local name only)")
            errors += 1
            continue
        if name in RESERVED_NAMES:
            err(f"{block_path}: reserved name '{name}'")
            errors += 1
        if name in names:
            err(f"duplicate block name '{name}' in pack {pack_id}")
            errors += 1
        names.add(name)

        textures = data.get("textures", [])
        if not isinstance(textures, list) or len(textures) not in (FACE_COUNT, 12):
            err(f"{block_path}: textures must have 6 or 12 entries")
            errors += 1
            continue

        stems = textures[:FACE_COUNT]
        widths: set[int] = set()
        heights: set[int] = set()
        animation = data.get("animation")
        frame_count = animation.get("frame_count") if isinstance(animation, dict) else None
        for stem in stems:
            if not isinstance(stem, str) or not stem:
                err(f"{block_path}: invalid texture stem")
                errors += 1
                break
            png = tex_dir / f"{stem}.png"
            if not png.is_file():
                warn(f"{block_path}: missing PNG for stem '{stem}'")
                continue
            try:
                import struct

                raw = png.read_bytes()
                if raw[:8] != b"\x89PNG\r\n\x1a\n":
                    warn(f"{png}: not a PNG")
                    continue
                w = struct.unpack(">I", raw[16:20])[0]
                h = struct.unpack(">I", raw[20:24])[0]
                widths.add(w)
                heights.add(h)
                if isinstance(frame_count, int) and frame_count > 0:
                    if len(textures) == FACE_COUNT:
                        expected_h = w * frame_count
                        if h != expected_h:
                            err(
                                f"{block_path}: {stem}.png height {h} != "
                                f"width×frame_count ({w}×{frame_count}={expected_h})"
                            )
                            errors += 1
                    elif len(textures) > FACE_COUNT and len(textures) % FACE_COUNT == 0:
                        layer_frames = len(textures) // FACE_COUNT
                        if layer_frames != frame_count:
                            err(
                                f"{block_path}: texture layers {layer_frames} != "
                                f"animation.frame_count {frame_count}"
                            )
                            errors += 1
                        if h != w:
                            err(
                                f"{block_path}: {stem}.png must be square for layer animation ({w}×{h})"
                            )
                            errors += 1
            except Exception:
                warn(f"{png}: could not read PNG dimensions")

        if len(widths) > 1:
            err(f"{block_path}: face PNG widths differ: {widths}")
            errors += 1

    errors += validate_creatures(pack_dir, pack_id)
    errors += validate_skins(pack_dir, pack_id)

    if errors:
        err(f"{pack_dir}: {errors} error(s)")
        return 1
    print(f"OK: {pack_id} ({len(names)} blocks)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Cubatarium resource pack(s)")
    parser.add_argument("pack_dirs", nargs="+", type=Path, help="Path(s) to pack root")
    args = parser.parse_args()
    rc = 0
    for p in args.pack_dirs:
        rc = max(rc, validate_pack(p.resolve()))
    return rc


if __name__ == "__main__":
    sys.exit(main())
