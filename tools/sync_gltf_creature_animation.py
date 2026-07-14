#!/usr/bin/env python3
"""Patch gltf_skeleton creature.json animation clips and state_map from Luanti Lua."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
MODELS = ROOT / "models" / "creatures"

sys.path.insert(0, str(TOOLS))
from luanti_mob_animation import (  # noqa: E402
    build_creature_clip_defs,
    build_gltf_state_map,
    load_luanti_clips,
    load_mob_properties,
)


def patch_species(species: str, dry_run: bool = False) -> bool:
    species_dir = MODELS / species
    cj = species_dir / "creature.json"
    if not cj.is_file():
        return False
    creature = json.loads(cj.read_text(encoding="utf-8"))
    if creature.get("visual", {}).get("backend") != "gltf_skeleton":
        return False

    props, _ = load_mob_properties(species)
    if props.get("visual") == "sprite":
        print(f"SKIP {species}: sprite visual (TD-CRE-028)")
        return False

    clip_frames, _fps = load_luanti_clips(species)
    if not clip_frames:
        print(f"SKIP {species}: no animation clips in Lua")
        return False

    anim_block = props.get("animation", {})
    clips = build_creature_clip_defs(clip_frames, anim_block)
    state_map = build_gltf_state_map(
        creature.get("habitat", "terrestrial"),
        set(clips.keys()),
    )

    visual = creature.setdefault("visual", {})
    anim = visual.setdefault("animation", {})
    old_hz = anim.get("walk_cycle_hz", 2.0)
    anim["walk_cycle_hz"] = old_hz
    anim["clips"] = clips
    anim["state_map"] = state_map

    if not dry_run:
        cj.write_text(json.dumps(creature, indent=2) + "\n", encoding="utf-8")
    print(f"OK {species}: clips={list(clips.keys())}")
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--species")
    ap.add_argument("--all-gltf", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if args.species:
        species_list = [args.species]
    elif args.all_gltf:
        species_list = sorted(
            p.name
            for p in MODELS.iterdir()
            if (p / "creature.json").is_file()
            and json.loads((p / "creature.json").read_text(encoding="utf-8"))
            .get("visual", {})
            .get("backend")
            == "gltf_skeleton"
        )
    else:
        ap.print_help()
        return 1

    ok = sum(1 for sp in species_list if patch_species(sp, dry_run=args.dry_run))
    print(f"patched {ok}/{len(species_list)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
