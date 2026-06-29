#!/usr/bin/env python3
"""Validate bedrock_geo creature assets."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODELS = ROOT / "models" / "creatures"

try:
    import yaml
except ImportError:
    yaml = None


def load_catalog() -> dict:
    if yaml is None:
        raise SystemExit("PyYAML required")
    data = yaml.safe_load((ROOT / "tools" / "bedrock_geo_catalog.yaml").read_text(encoding="utf-8"))
    return data.get("species", {})


def validate_species(species_id: str) -> None:
    creature_path = MODELS / species_id / "creature.json"
    if not creature_path.is_file():
        raise SystemExit(f"FAIL {species_id}: missing creature.json")
    creature = json.loads(creature_path.read_text(encoding="utf-8"))
    visual = creature.get("visual", {})
    if visual.get("backend") != "bedrock_geo":
        raise SystemExit(f"FAIL {species_id}: backend={visual.get('backend')}")
    geo_file = visual.get("geometry_file", "geometry.geo.json")
    geo_path = MODELS / species_id / geo_file
    if not geo_path.is_file():
        raise SystemExit(f"FAIL {species_id}: missing {geo_file}")
    tex_stem = visual.get("texture", "diffuse")
    tex_path = MODELS / species_id / "textures" / f"{tex_stem}.png"
    if not tex_path.is_file():
        raise SystemExit(f"FAIL {species_id}: missing textures/{tex_stem}.png")
    geo = json.loads(geo_path.read_text(encoding="utf-8"))
    bone_count = 0
    if "minecraft:geometry" in geo:
        for entry in geo["minecraft:geometry"]:
            bone_count = max(bone_count, len(entry.get("bones", [])))
    else:
        for key, val in geo.items():
            if key.startswith("geometry.") and isinstance(val, dict):
                bone_count = len(val.get("bones", []))
    if bone_count < 1:
        raise SystemExit(f"FAIL {species_id}: no bones in geo")
    print(f"OK {species_id}: bones={bone_count} geo={geo_file}")


def main() -> int:
    catalog = load_catalog()
    species = sys.argv[1:] if len(sys.argv) > 1 else list(catalog)
    for sid in species:
        if sid not in catalog:
            print(f"SKIP unknown {sid}")
            continue
        validate_species(sid)
    return 0


if __name__ == "__main__":
    sys.exit(main())
