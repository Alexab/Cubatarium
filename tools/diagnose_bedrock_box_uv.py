#!/usr/bin/env python3
"""Print Bedrock box UV face regions for a cube (matches BedrockCubeMeshBuilder)."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def face_pixels(face: int, u: int, v: int, w: int, h: int, d: int) -> tuple[int, int, int, int]:
    if face == 0:  # +Z south (back)
        return u + d + w + d, v + d, u + d + w + d + w, v + d + h
    if face == 1:  # +X east (mob right)
        return u + d + w, v + d, u + d + w + d, v + d + h
    if face == 2:  # -Z north (front)
        return u + d, v + d, u + d + w, v + d + h
    if face == 3:  # -X west (mob left)
        return u, v + d, u + d, v + d + h
    if face == 4:  # +Y up
        return u + d, v, u + d + w, v + d
    if face == 5:  # -Y down
        return u + d + w, v, u + d + w + w, v + d
    raise ValueError(face)


FACE_NAMES = ["+Z south", "+X east", "-Z north", "-X west", "+Y up", "-Y down"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("geo", type=Path, help="geometry.geo.json path")
    parser.add_argument("--bone", default="body")
    parser.add_argument("--cube", type=int, default=0)
    args = parser.parse_args()

    root = json.loads(args.geo.read_text(encoding="utf-8"))
    if "minecraft:geometry" in root:
        geo = root["minecraft:geometry"][0]
        desc = geo["description"]
        tw = desc.get("texture_width", 64)
        th = desc.get("texture_height", 64)
        bones = geo["bones"]
    else:
        key = next(k for k in root if k.startswith("geometry."))
        geo = root[key]
        tw = geo.get("texturewidth", geo.get("texture_width", 64))
        th = geo.get("textureheight", geo.get("texture_height", 32))
        bones = geo["bones"]

    bone = next(b for b in bones if b["name"] == args.bone)
    cube = bone["cubes"][args.cube]
    origin = cube["origin"]
    size = [abs(x) for x in cube["size"]]
    u, v = cube["uv"]
    w, h, d = size

    print(f"bone={args.bone} cube={args.cube}")
    print(f"origin={origin} size={size} uv=[{u},{v}] atlas={tw}x{th}")
    print()
    for fi, name in enumerate(FACE_NAMES):
        u0, v0, u1, v1 = face_pixels(fi, u, v, w, h, d)
        print(f"face {fi} {name:12} pixels ({u0},{v0})-({u1},{v1})  "
              f"norm ({u0/tw:.4f},{v0/th:.4f})-({u1/tw:.4f},{v1/th:.4f})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
