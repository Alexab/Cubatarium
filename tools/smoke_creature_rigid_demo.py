#!/usr/bin/env python3
"""Smoke checks for rigid_demo_walker/flyer/swimmer reference mobs."""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODELS = ROOT / "models" / "creatures"

DEMOS = {
    "rigid_demo_walker": {
        "habitat": "terrestrial",
        "archetype": "terrestrial_quadruped",
        "min_parts": 5,
    },
    "rigid_demo_flyer": {
        "habitat": "aerial",
        "archetype": "aerial",
        "min_parts": 3,
    },
    "rigid_demo_swimmer": {
        "habitat": "aquatic",
        "archetype": "aquatic",
        "min_parts": 4,
    },
}


def check_demo(species: str, spec: dict) -> list[str]:
    errors: list[str] = []
    species_dir = MODELS / species
    cj = species_dir / "creature.json"
    if not cj.is_file():
        return [f"{species}: missing creature.json"]
    creature = json.loads(cj.read_text(encoding="utf-8"))
    if creature.get("id") != species:
        errors.append(f"{species}: id mismatch")
    vis = creature.get("visual", {})
    if vis.get("backend") != "rigid_voxels":
        errors.append(f"{species}: backend={vis.get('backend')!r}")
    if creature.get("habitat") != spec["habitat"]:
        errors.append(f"{species}: habitat={creature.get('habitat')!r}")
    if creature.get("locomotion_archetype") != spec["archetype"]:
        errors.append(
            f"{species}: archetype={creature.get('locomotion_archetype')!r}"
        )
    parts = vis.get("parts", [])
    if len(parts) < spec["min_parts"]:
        errors.append(f"{species}: parts={len(parts)}")
    if not creature.get("catalog", {}).get("spawnable"):
        errors.append(f"{species}: not spawnable")
    stems = {p.get("texture", "body") for p in parts}
    for stem in stems:
        tex = species_dir / "textures" / f"{stem}.png"
        if not tex.is_file():
            errors.append(f"{species}: missing textures/{stem}.png")
    return errors


def main() -> int:
    all_errors: list[str] = []
    for species, spec in DEMOS.items():
        all_errors.extend(check_demo(species, spec))
    if all_errors:
        for err in all_errors:
            print(f"FAIL {err}", file=sys.stderr)
        return 1
    print(f"OK smoke_creature_rigid_demo: {len(DEMOS)} species")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
