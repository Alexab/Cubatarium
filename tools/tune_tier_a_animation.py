#!/usr/bin/env python3
"""Apply b3d walk-cycle leg swing hints to Tier A creature.json animation blocks."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from creature_tier_a import TIER_A_MOBS
from creature_uv_common import load_creature, load_sources
from sample_b3d_pose_curves import axis_swing, sample_bone_clips

RESEARCH_DEFAULT = Path(r"E:/Work/Home/CubatariumTextureResearch")

# Luanti mob clip ranges (1-based frames); walk band only for quadruped/biped legs.
TIER_A_B3D: dict[str, dict] = {
    "sheep": {
        "model": "mobs_animal/models/mobs_sheep.b3d",
        "clips": {"walk": (1, 40)},
        "bones": ("Right_Leg", "Left_Leg"),
        "leg_axis": "pitch",
    },
    "cow": {
        "model": "mobs_animal/models/mobs_cow.b3d",
        "clips": {"walk": (1, 40)},
        "bones": ("Right_Leg", "Left_Leg"),
        "leg_axis": "pitch",
    },
    "chicken": {
        "model": "mobs_animal/models/mobs_chicken.b3d",
        "clips": {"walk": (71, 90), "peck": (31, 70)},
        "bones": ("Right_Leg", "Left_Leg", "Right_Wing", "Left_Wing", "Spine_Head"),
        "leg_axis": "pitch",
        "wing_axis": "roll",
        "peck_bone": "Spine_Head",
        "peck_axis": "roll",
    },
    "oerkki": {
        "model": "mobs_monster/models/mobs_oerkki.b3d",
        "clips": {"walk": (1, 40)},
        "bones": ("Right_Leg", "Left_Leg", "Right_Arm", "Left_Arm"),
        "leg_axis": "pitch",
        "arm_axis": "pitch",
    },
    "skeleton": {
        "model": "dmobs/models/skeleton.b3d",
        "clips": {"walk": (1, 40)},
        "bones": ("Right_Leg", "Left_Leg", "Right_Arm", "Left_Arm"),
        "leg_axis": "pitch",
        "arm_axis": "pitch",
    },
    "sand_monster": {
        "model": "mobs_monster/models/mobs_sand_monster.b3d",
        "clips": {"walk": (1, 40)},
        "bones": ("Right_Leg", "Left_Leg", "Right_Arm", "Left_Arm"),
        "leg_axis": "pitch",
        "arm_axis": "pitch",
    },
}


def leg_swing_from_sample(sample: dict, bone_names: tuple[str, ...], axis: str) -> float | None:
    swings: list[float] = []
    for bone in bone_names:
        if "Leg" not in bone and "leg" not in bone.lower():
            continue
        walk = sample.get("bones", {}).get(bone, {}).get("clips", {}).get("walk")
        if not walk:
            continue
        swing = axis_swing(walk, axis)
        if swing is not None:
            swings.append(swing)
    return max(swings) if swings else None


def arm_swing_from_sample(sample: dict, bone_names: tuple[str, ...], axis: str) -> float | None:
    swings: list[float] = []
    for bone in bone_names:
        if "Arm" not in bone and "arm" not in bone.lower():
            continue
        walk = sample.get("bones", {}).get(bone, {}).get("clips", {}).get("walk")
        if not walk:
            continue
        swing = axis_swing(walk, axis)
        if swing is not None:
            swings.append(swing)
    return max(swings) if swings else None


def tune_species(species_id: str, research: Path, write: bool) -> dict[str, float]:
    cfg = TIER_A_B3D.get(species_id)
    if not cfg:
        return {}
    path = research / cfg["model"]
    if not path.is_file():
        print(f"skip {species_id}: missing {path}")
        return {}
    sample = sample_bone_clips(path, cfg["clips"], cfg["bones"])
    rec: dict[str, float] = {}
    leg = leg_swing_from_sample(sample, cfg["bones"], cfg.get("leg_axis", "pitch"))
    if leg is not None:
        rec["leg_swing_deg"] = round(leg, 1)
    arm = arm_swing_from_sample(sample, cfg["bones"], cfg.get("arm_axis", "pitch"))
    if arm is not None:
        rec["arm_swing_deg"] = round(arm, 1)
    if species_id == "chicken":
        from sample_b3d_pose_curves import chicken_recommended_tuning

        rec.update(chicken_recommended_tuning(sample))
    if not rec:
        print(f"skip {species_id}: no keyed walk bones")
        return {}
    creature_path = ROOT / "models" / "creatures" / species_id / "creature.json"
    creature = json.loads(creature_path.read_text(encoding="utf-8"))
    anim = creature.setdefault("visual", {}).setdefault("animation", {})
    changed = False
    for key, val in rec.items():
        if anim.get(key) != val:
            anim[key] = val
            changed = True
    if write and changed:
        creature_path.write_text(json.dumps(creature, indent=2) + "\n", encoding="utf-8")
        print(f"updated {species_id}: {rec}")
    elif changed:
        print(f"would update {species_id}: {rec}")
    else:
        print(f"unchanged {species_id}")
    return rec


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--species", nargs="+")
    parser.add_argument("--tier-a", action="store_true")
    parser.add_argument("--research", type=Path, default=RESEARCH_DEFAULT)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    if args.tier_a:
        species = [s for s in TIER_A_MOBS if s in TIER_A_B3D]
    elif args.species:
        species = args.species
    else:
        species = list(TIER_A_B3D.keys())
    for sid in species:
        tune_species(sid, args.research, args.write)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
