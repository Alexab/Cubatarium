#!/usr/bin/env python3
"""Migrate rigid_voxels species to gltf_skeleton: export glTF + patch creature.json."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = Path(__file__).resolve().parent

sys.path.insert(0, str(TOOLS))
from convert_creature_mesh_to_gltf import export_species, all_rigid_species  # noqa: E402

SKIP = {"rigid_demo_walker", "rigid_demo_flyer", "rigid_demo_swimmer"}


def patch_creature_json(species_dir: Path, creature: dict) -> dict:
    visual = creature.setdefault("visual", {})
    parts = visual.pop("parts", [])
    texture_stems = sorted({p.get("texture", "body") for p in parts})
    default_tex = visual.get("default_texture", "body")

    old_anim = visual.pop("animation", {})
    walk_hz = old_anim.get("walk_cycle_hz", 2.0)

    visual["backend"] = "gltf_skeleton"
    visual["gltf"] = {
        "model": "model.gltf",
        "textures": texture_stems or [default_tex],
        "model_scale": 1.0,
        "model_yaw_offset_deg": visual.pop("model_yaw_offset_deg", 0),
    }
    visual["animation"] = {
        "walk_cycle_hz": walk_hz,
        "clips": {
            "idle": {"start": 0, "end": 1, "loop": True},
            "walk": {"start": 0, "end": 1, "loop": True, "speed": 1.0},
        },
        "state_map": {
            "Idle": "idle",
            "Walk": "walk",
            "Run": "walk",
            "Jump": "walk",
            "Fall": "idle",
            "Swim": "walk",
            "Tread": "idle",
            "Fly": "idle",
            "Hover": "idle",
            "Glide": "idle",
            "Slither": "walk",
            "Coil": "idle",
            "Crouch": "idle",
            "Action": "walk",
        },
    }
    visual["default_texture"] = default_tex
    return creature


def migrate_species(species: str, dry_run: bool = False) -> bool:
    species_dir = ROOT / "models" / "creatures" / species
    cj = species_dir / "creature.json"
    if not cj.is_file():
        print(f"SKIP {species}: no creature.json")
        return False
    creature = json.loads(cj.read_text(encoding="utf-8"))
    if creature.get("visual", {}).get("backend") != "rigid_voxels":
        print(f"SKIP {species}: not rigid_voxels")
        return False
    if species in SKIP:
        print(f"SKIP {species}: demo mob")
        return False

    if not dry_run:
        export_species(species, species_dir)
        creature = patch_creature_json(species_dir, creature)
        cj.write_text(json.dumps(creature, indent=2) + "\n", encoding="utf-8")
    print(f"OK migrate_creature_to_gltf: {species}")
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--species", help="single species")
    ap.add_argument("--all-rigid", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if args.species:
        species_list = [args.species]
    elif args.all_rigid:
        species_list = all_rigid_species()
    else:
        ap.print_help()
        return 1

    ok = 0
    for sp in species_list:
        if migrate_species(sp, dry_run=args.dry_run):
            ok += 1
    print(f"migrated {ok}/{len(species_list)}")
    return 0 if ok == len(species_list) or args.dry_run else 0


if __name__ == "__main__":
    raise SystemExit(main())
