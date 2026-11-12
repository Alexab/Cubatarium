#!/usr/bin/env python3
"""Validate bone_skeleton creature assets.

Coverage is every bone_skeleton creature.json under models/creatures, including
bot. Errors are accumulated. A missing textures/<stem>.png stays a failure and
is not repaired by importing Minecraft textures.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODELS = ROOT / "models" / "creatures"
TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
from creature_backend import is_bone_skeleton_backend  # noqa: E402

def bone_species_from_models() -> list[str]:
    found: list[str] = []
    for path in sorted(MODELS.glob("*/creature.json")):
        creature = json.loads(path.read_text(encoding="utf-8"))
        if is_bone_skeleton_backend(creature.get("visual", {}).get("backend")):
            found.append(path.parent.name)
    return found


def validate_species(species_id: str) -> tuple[list[str], int, str]:
    """Return (errors, bone_count, geometry file name)."""
    errors: list[str] = []
    creature_path = MODELS / species_id / "creature.json"
    if not creature_path.is_file():
        return [f"missing creature.json"], 0, ""
    creature = json.loads(creature_path.read_text(encoding="utf-8"))
    visual = creature.get("visual", {})
    if not is_bone_skeleton_backend(visual.get("backend")):
        return [f"backend={visual.get('backend')}"], 0, ""
    geo_file = visual.get("geometry_file", "geometry.geo.json")
    geo_path = MODELS / species_id / geo_file
    if not geo_path.is_file():
        errors.append(f"missing {geo_file}")
    tex_stem = visual.get("texture", "diffuse")
    tex_path = MODELS / species_id / "textures" / f"{tex_stem}.png"
    if not tex_path.is_file():
        errors.append(f"missing textures/{tex_stem}.png")
    bone_count = 0
    if geo_path.is_file():
        geo = json.loads(geo_path.read_text(encoding="utf-8"))
        if "minecraft:geometry" in geo:
            for entry in geo["minecraft:geometry"]:
                bone_count = max(bone_count, len(entry.get("bones", [])))
        else:
            for key, val in geo.items():
                if key.startswith("geometry.") and isinstance(val, dict):
                    bone_count = len(val.get("bones", []))
        if bone_count < 1:
            errors.append("no bones in geo")
    return errors, bone_count, geo_file


def main() -> int:
    species = sys.argv[1:] if len(sys.argv) > 1 else bone_species_from_models()
    failed = 0
    for sid in species:
        errors, bone_count, geo_file = validate_species(sid)
        if errors:
            failed += 1
            for error in errors:
                print(f"FAIL {sid}: {error}", file=sys.stderr)
        else:
            print(f"OK {sid}: bones={bone_count} geo={geo_file}")
    if failed:
        print(f"validate_bedrock_creature: {failed} failed, {len(species) - failed} ok", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
