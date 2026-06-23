#!/usr/bin/env python3
"""Import a single Luanti-style rigid creature from a texture PNG."""

from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Reuse catalog templates from the bulk generator.
from generate_luanti_creature_catalog import (  # noqa: E402
    SPECIES,
    aerial_parts,
    biped_parts,
    quadruped_parts,
)


ARCHETYPE_PARTS = {
    "biped": lambda: biped_parts(pivot=True),
    "quadruped": quadruped_parts,
    "aerial": aerial_parts,
}


def build_creature_json(
    species_id: str,
    display: str,
    archetype: str,
    tags: list[str],
    sort_order: int,
) -> dict:
    meta = SPECIES.get(species_id, {})
    locomotion = meta.get("archetype", "terrestrial_biped")
    if archetype == "biped":
        locomotion = "terrestrial_biped"
    elif archetype == "quadruped":
        locomotion = "terrestrial_quadruped"
    elif archetype == "aerial":
        locomotion = "aerial"
    parts_fn = ARCHETYPE_PARTS[archetype]
    return {
        "id": species_id,
        "display_name": display,
        "catalog": {"tags": tags, "spawnable": True, "sort_order": sort_order},
        "role": "mob",
        "bounds": meta.get(
            "bounds",
            {"rest": [0.8, 1.0, 0.8], "max": [0.8, 1.0, 0.8], "min": [0.8, 0.7, 0.8]},
        ),
        "eye_height": meta.get("eye", 0.9),
        "locomotion_archetype": locomotion,
        "locomotion": {
            "can_fly": False,
            "can_crouch": False,
            "can_jump": True,
            "jump_height": 1.0,
            "walk_speed": meta.get("walk", 2.0),
        },
        "behavior": "wander",
        "behavior_params": {
            "move_speed": meta.get("walk", 2.0),
            "wander_interval_min": 2.0,
            "wander_interval_max": 4.0,
        },
        "visual": {
            "backend": "rigid_voxels",
            "animation": {
                "walk_cycle_hz": 2.0,
                "leg_swing_deg": 25,
                "arm_swing_deg": 15,
                "fly_body_pitch_deg": 10,
            },
            "default_texture": "body",
            "parts": parts_fn(),
            "icon": {
                "mode": "parts_preview",
                "color": meta.get("icon", [0.7, 0.7, 0.7, 1.0]),
            },
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--id", required=True, help="Species id (folder name)")
    parser.add_argument(
        "--archetype",
        required=True,
        choices=sorted(ARCHETYPE_PARTS.keys()),
        help="Rigid part template",
    )
    parser.add_argument("--texture", required=True, type=Path, help="Source PNG")
    parser.add_argument("--license", default="CC-BY-SA 3.0")
    parser.add_argument("--attribution", default="Luanti contributors")
    parser.add_argument("--display", default="")
    parser.add_argument("--tags", default="mobs,passive")
    parser.add_argument("--sort-order", type=int, default=50)
    args = parser.parse_args()

    species_id = args.id
    display = args.display or species_id.replace("_", " ").title()
    tags = [t.strip() for t in args.tags.split(",") if t.strip()]
    base = ROOT / "models" / "creatures" / species_id
    tex_dir = base / "textures"
    tex_dir.mkdir(parents=True, exist_ok=True)

    src = args.texture.resolve()
    if not src.is_file():
        raise SystemExit(f"texture not found: {src}")

    for stem in ("body", "leg", "arm", "face"):
        shutil.copy2(src, tex_dir / f"{stem}.png")
    shutil.copy2(src, tex_dir / "icon.png")

    data = build_creature_json(species_id, display, args.archetype, tags, args.sort_order)
    (base / "creature.json").write_text(
        json.dumps(data, indent=2) + "\n", encoding="utf-8"
    )

    license_path = base / "LICENSE.txt"
    license_path.write_text(
        f"Texture: {src.name}\n"
        f"License: {args.license}\n"
        f"Attribution: {args.attribution}\n",
        encoding="utf-8",
    )
    print(f"wrote {base}")


if __name__ == "__main__":
    main()
