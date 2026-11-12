"""Write a bone_skeleton geometry.geo.json from authored cubes."""

from __future__ import annotations

from creaturegen.anatomy import Model


def export_geometry(model: Model, identifier: str) -> dict:
    bones = []
    for bone in model.bones:
        entry = {
            "name": bone.name,
            "pivot": [float(v) for v in bone.pivot],
            "cubes": [
                {
                    "origin": [float(v) for v in cube.origin],
                    "size": [int(v) for v in cube.size],
                    "uv": [int(cube.uv[0]), int(cube.uv[1])],
                }
                for cube in bone.cubes
            ],
        }
        if bone.parent:
            entry["parent"] = bone.parent
        if bone.mirror:
            entry["mirror"] = True
        if bone.bind_rotation:
            entry["bind_pose_rotation"] = [float(v) for v in bone.bind_rotation]
        bones.append(entry)
    width, height = model.texture_size
    return {
        # Keyed geometry.<id>, texturewidth and box UVs use the legacy 1.8
        # schema. 1.12 declares minecraft:geometry[]/description instead.
        "format_version": "1.8.0",
        identifier: {
            "texturewidth": width,
            "textureheight": height,
            "visible_bounds_width": 2,
            "visible_bounds_height": 2,
            "visible_bounds_offset": [0, 1, 0],
            "bones": bones,
        },
    }
