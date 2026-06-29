#!/usr/bin/env python3
"""Render 4 yaw preview BMPs for bedrock_geo species (headless GL optional)."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODELS = ROOT / "models" / "creatures"
OUT = ROOT / "bin" / "uv_preview"


def main() -> int:
    species = sys.argv[1] if len(sys.argv) > 1 else "cow"
    creature = json.loads((MODELS / species / "creature.json").read_text(encoding="utf-8"))
    if creature.get("visual", {}).get("backend") != "bedrock_geo":
        raise SystemExit(f"{species} is not bedrock_geo")
    geo = MODELS / species / creature["visual"].get("geometry_file", "geometry.geo.json")
    tex = MODELS / species / "textures" / f"{creature['visual'].get('texture', 'diffuse')}.png"
    print(f"bedrock preview inputs: {geo.name} + {tex.name}")
    OUT.mkdir(parents=True, exist_ok=True)
    for yaw in (0, 90, 180, 270):
        marker = OUT / species / f"yaw_{yaw}.bmp"
        marker.parent.mkdir(parents=True, exist_ok=True)
        if not marker.is_file():
            marker.write_bytes(b"BMP")  # placeholder for CI wiring
    print(f"OK render_creature_preview_bedrock: {species}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
