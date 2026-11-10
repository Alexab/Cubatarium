"""Rigid voxel parts in blocks, with the limb ids the presenters already turn."""

from __future__ import annotations

from creaturegen.anatomy import Model


def _limb_for(name: str) -> str | None:
    if name in ("leg0", "leg_fl", "frontLegLeft", "haunchLeft"):
        return "leg"
    if name.startswith("leg") or name in ("rightLeg", "leftLeg", "rightArm", "leftArm"):
        return "leg" if "Leg" in name or name.startswith("leg") else "arm"
    return None


def _part_id(name: str, index: int) -> str:
    mapping = {
        "leg0": "leg_fl",
        "leg1": "leg_fr",
        "leg2": "leg_bl",
        "leg3": "leg_br",
        "rightLeg": "leg_r",
        "leftLeg": "leg_l",
        "rightArm": "arm_r",
        "leftArm": "arm_l",
        "body": "torso",
    }
    return mapping.get(name, name if index == 0 else f"{name}_{index}")


def export_rigid(model: Model) -> dict:
    parts = []
    for bone in model.bones:
        for index, cube in enumerate(bone.cubes):
            ox, oy, oz = (component / 16.0 for component in cube.origin)
            sx, sy, sz = (max(0.05, component / 16.0) for component in cube.size)
            center = [ox + sx / 2.0, oy + sy / 2.0, oz + sz / 2.0]
            part = {
                "id": _part_id(bone.name, index),
                "offset": [round(v, 4) for v in center],
                "size": [round(sx, 4), round(sy, 4), round(sz, 4)],
                "texture": "face" if cube.role in ("face", "muzzle") else ("leg" if cube.role == "limb" else "body"),
            }
            limb = _limb_for(bone.name)
            if limb:
                part["limb"] = limb
                part["pivot"] = [round(component / 16.0, 4) for component in bone.pivot]
            parts.append(part)
    if not parts:
        parts.append({
            "id": "torso",
            "offset": [0.0, 0.4, 0.0],
            "size": [0.4, 0.4, 0.6],
            "texture": "body",
        })
    return {"parts": parts}
