"""Write a skinned glTF 2.0 + external bin the runtime loader accepts."""

from __future__ import annotations

import math
import struct

from creaturegen.anatomy import Model


def _pad(blob: bytearray, alignment: int = 4) -> None:
    while len(blob) % alignment:
        blob.append(0)


def _quat_x(degrees: float) -> tuple[float, float, float, float]:
    half = math.radians(degrees) * 0.5
    return (math.sin(half), 0.0, 0.0, math.cos(half))


def _quaternion_euler(deg):
    x, y, z = [math.radians(a)*.5 for a in deg]
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    return (sx*cy*cz-cx*sy*sz, cx*sy*cz+sx*cy*sz,
            cx*cy*sz-sx*sy*cz, cx*cy*cz+sx*sy*sz)


def _box_uvs(cube, texture_size: tuple[int, int]) -> list[tuple[float, float]]:
    """Four UVs per face, same order as the position corners. V grows down, matching the PNG."""
    tw, th = texture_size
    w, h, d = [max(int(component), 1) for component in cube.size]
    u, v = [int(component) for component in cube.uv]
    rects = (
        (u + d + w + d, v + d, u + d + w + d + w, v + d + h),  # +Z back
        (u + d, v + d, u + d + w, v + d + h),  # -Z front, eyes live here
        (u, v + d, u + d, v + d + h),  # -X: corners advance from -Z to +Z
        (u + d + w, v + d, u + d + w + d, v + d + h),  # +X: corners advance from +Z to -Z
        (u + d, v, u + d + w, v + d),  # +Y
        (u + d + w, v, u + d + w + w, v + d),  # -Y
    )
    uvs = []
    for u0, v0, u1, v1 in rects:
        u0 = (u0 + 0.5) / tw
        v0 = (v0 + 0.5) / th
        u1 = (u1 - 0.5) / tw
        v1 = (v1 - 0.5) / th
        # Vertex order on each face is bottom-left, bottom-right, top-left, top-right.
        uvs.extend(((u0, v1), (u1, v1), (u0, v0), (u1, v0)))
    return uvs


def _material_for(role: str) -> str:
    # One atlas. Per-face UVs already select the painted island, including eyes.
    del role
    return "body"


def export_gltf(model: Model, clip_names: list[str]) -> tuple[dict, bytes]:
    """Vertices are in blocks; zero-weight skin slots use joint index zero."""
    names = [bone.name for bone in model.bones]
    joint_by_name = {name:index for index,name in enumerate(names)}
    by_material: dict[str, list] = {}
    for bone_index, bone in enumerate(model.bones):
        px, py, pz = (component / 16.0 for component in bone.pivot)
        for cube in bone.cubes:
            material = _material_for(cube.role)
            ox, oy, oz = (component / 16.0 for component in cube.origin)
            sx, sy, sz = (component / 16.0 for component in cube.size)
            # One box, 6 faces, model space. UVs land inside the painted atlas island.
            corners = [
                (ox, oy, oz + sz),
                (ox + sx, oy, oz + sz),
                (ox, oy + sy, oz + sz),
                (ox + sx, oy + sy, oz + sz),
                (ox + sx, oy, oz),
                (ox, oy, oz),
                (ox + sx, oy + sy, oz),
                (ox, oy + sy, oz),
                (ox, oy, oz),
                (ox, oy, oz + sz),
                (ox, oy + sy, oz),
                (ox, oy + sy, oz + sz),
                (ox + sx, oy, oz + sz),
                (ox + sx, oy, oz),
                (ox + sx, oy + sy, oz + sz),
                (ox + sx, oy + sy, oz),
                (ox, oy + sy, oz),
                (ox + sx, oy + sy, oz),
                (ox, oy + sy, oz + sz),
                (ox + sx, oy + sy, oz + sz),
                (ox, oy, oz + sz),
                (ox + sx, oy, oz + sz),
                (ox, oy, oz),
                (ox + sx, oy, oz),
            ]
            if cube.shape:
                shaped = []
                for x, y, z in corners:
                    if "side_profile" in cube.shape:
                        # Four Y/Z corners, ordered lower-front, upper-front,
                        # lower-back, upper-back; extruded through thickness X.
                        a, b = (y-oy)/sy, (z-oz)/sz
                        q = cube.shape["side_profile"]
                        yy = (1-a)*(1-b)*q[0][0]+a*(1-b)*q[1][0]+(1-a)*b*q[2][0]+a*b*q[3][0]
                        zz = (1-a)*(1-b)*q[0][1]+a*(1-b)*q[1][1]+(1-a)*b*q[2][1]+a*b*q[3][1]
                        bottom, top = cube.shape.get("side_width", [1, 1])
                        xx = ox+sx*.5+(x-ox-sx*.5)*(bottom*(1-a)+top*a)
                        shaped.append((xx, oy+sy*yy, oz+sz*zz))
                        continue
                    if "planform" in cube.shape:
                        a, b = (x-ox)/sx, (z-oz)/sz
                        q = cube.shape["planform"]
                        xx = (1-a)*(1-b)*q[0][0]+a*(1-b)*q[1][0]+(1-a)*b*q[2][0]+a*b*q[3][0]
                        zz = (1-a)*(1-b)*q[0][1]+a*(1-b)*q[1][1]+(1-a)*b*q[2][1]+a*b*q[3][1]
                        yy = y
                        if "planform_y" in cube.shape:
                            lo,hi = cube.shape["planform_y"]
                            iy=(y-oy)/sy
                            heights=[(1-iy)*low+iy*high for low,high in zip(lo,hi)]
                            yy=oy+sy*((1-a)*(1-b)*heights[0]+a*(1-b)*heights[1]+(1-a)*b*heights[2]+a*b*heights[3])
                        shaped.append((ox+sx*xx, yy, oz+sz*zz))
                        continue
                    t = (z-oz)/sz if sz else 0
                    front = cube.shape.get("front_scale", [1, 1])
                    back = cube.shape.get("back_scale", [1, 1])
                    wx = back[0]*(1-t)+front[0]*t
                    hy = back[1]*(1-t)+front[1]*t
                    xx = ox+sx*.5+(x-ox-sx*.5)*wx
                    yy = oy+sy*.5+(y-oy-sy*.5)*hy
                    dx,dy = cube.shape.get("front_offset", [0,0])
                    xx += dx/16*t
                    yy += dy/16*t
                    if "top_scale" in cube.shape:
                        # Cross-section taper along Y: pointed ears, quills,
                        # horns. It composes with the existing longitudinal taper.
                        fy = (y-oy)/sy if sy else 0
                        tx,tz = cube.shape["top_scale"]
                        xx = ox+sx*.5+(xx-ox-sx*.5)*(1+fy*(tx-1))
                        z = oz+sz*.5+(z-oz-sz*.5)*(1+fy*(tz-1))
                    shaped.append((xx, yy, z))
                corners = shaped
            uvs = _box_uvs(cube, model.texture_size)
            by_material.setdefault(material, []).append((corners, uvs, bone_index, cube.blend))

    blob = bytearray()
    buffer_views = []
    accessors = []

    def add_bytes(data: bytes, target: int | None = None) -> int:
        _pad(blob)
        start = len(blob)
        blob.extend(data)
        view = {"buffer": 0, "byteOffset": start, "byteLength": len(data)}
        if target:
            view["target"] = target
        buffer_views.append(view)
        return len(buffer_views) - 1

    def add_accessor(view: int, count: int, component: int, type_name: str, mins=None, maxs=None) -> int:
        entry = {
            "bufferView": view,
            "byteOffset": 0,
            "componentType": component,
            "count": count,
            "type": type_name,
        }
        if mins is not None:
            entry["min"] = mins
            entry["max"] = maxs
        accessors.append(entry)
        return len(accessors) - 1

    primitives = []
    for material, chunks in by_material.items():
        positions = []
        uvs = []
        joints = []
        weights = []
        for corners, corner_uvs, bone_index, blend in chunks:
            positions.extend(corners)
            uvs.extend(corner_uvs)
            for vertex in corners:
                if blend:
                    other = joint_by_name[blend["bone"]]
                    axis = {"x":0,"y":1,"z":2}[blend.get("axis", "z")]
                    lo, hi = [float(v)/16 for v in blend["range"]]
                    t = max(0,min(1,(vertex[axis]-lo)/max(hi-lo,1e-8)))
                    a,b = blend["weights"]
                    w = max(0,min(1,a*(1-t)+b*t))
                    joints.append((bone_index if 1-w else 0, other if w else 0, 0, 0))
                    weights.append((1-w, w, 0.0, 0.0))
                else:
                    joints.append((bone_index, 0, 0, 0))
                    weights.append((1.0, 0.0, 0.0, 0.0))
        indices = []
        for face in range(len(positions) // 4):
            base = face * 4
            indices.extend((base, base + 1, base + 2, base + 2, base + 1, base + 3))
        pos_data = b"".join(struct.pack("<3f", *p) for p in positions)
        uv_data = b"".join(struct.pack("<2f", *uv) for uv in uvs)
        joint_data = b"".join(struct.pack("<4B", *j) for j in joints)
        weight_data = b"".join(struct.pack("<4f", *w) for w in weights)
        index_data = b"".join(struct.pack("<H", i) for i in indices)
        xs = [p[0] for p in positions]
        ys = [p[1] for p in positions]
        zs = [p[2] for p in positions]
        primitives.append({
            "attributes": {
                "POSITION": add_accessor(add_bytes(pos_data, 34962), len(positions), 5126, "VEC3",
                                         [min(xs), min(ys), min(zs)], [max(xs), max(ys), max(zs)]),
                "TEXCOORD_0": add_accessor(add_bytes(uv_data, 34962), len(uvs), 5126, "VEC2"),
                "JOINTS_0": add_accessor(add_bytes(joint_data, 34962), len(joints), 5121, "VEC4"),
                "WEIGHTS_0": add_accessor(add_bytes(weight_data, 34962), len(weights), 5126, "VEC4"),
            },
            "indices": add_accessor(add_bytes(index_data, 34963), len(indices), 5123, "SCALAR"),
            "material": material,
        })

    nodes = []
    joint_nodes = []
    index_by_name = {bone.name: index for index, bone in enumerate(model.bones)}
    child_lists: list[list[int]] = [[] for _ in model.bones]
    for index, bone in enumerate(model.bones):
        absolute = [component / 16.0 for component in bone.pivot]
        parent = index_by_name.get(bone.parent) if bone.parent else None
        if parent is None:
            translation = absolute
        else:
            parent_pivot = [component / 16.0 for component in model.bones[parent].pivot]
            translation = [absolute[axis] - parent_pivot[axis] for axis in range(3)]
            child_lists[parent].append(index)
        node = {
            "name": bone.name,
            "translation": translation,
            "rotation": [0.0, 0.0, 0.0, 1.0],
            "scale": [1.0, 1.0, 1.0],
        }
        if child_lists[index]:
            node["children"] = child_lists[index]
        nodes.append(node)
        joint_nodes.append(index)
    # Children are recorded while later siblings are still unknown, so write them once more.
    for index, kids in enumerate(child_lists):
        if kids:
            nodes[index]["children"] = kids
    nodes.append({"name": "mesh", "mesh": 0, "skin": 0})
    # A glTF scene contains hierarchy roots, never a joint and its children
    # again as independent roots (glTF 2.0 node tree constraint).
    scene_nodes = [i for i, bone in enumerate(model.bones) if not bone.parent] + [len(nodes) - 1]

    ibm = b""
    for bone in model.bones:
        px, py, pz = (component / 16.0 for component in bone.pivot)
        # Column-major inverse translation.
        matrix = [
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            -px, -py, -pz, 1,
        ]
        ibm += struct.pack("<16f", *matrix)
    ibm_accessor = add_accessor(add_bytes(ibm), len(model.bones), 5126, "MAT4")

    materials = []
    material_index = {}
    for primitive in primitives:
        name = primitive["material"]
        if name not in material_index:
            material_index[name] = len(materials)
            materials.append({
                "name": name,
                "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1],
                    "baseColorTexture":{"index":len(materials)},
                    "metallicFactor":0,"roughnessFactor":1},
                "alphaMode":"MASK","alphaCutoff":0.05,
            })
        primitive["material"] = material_index[name]

    animations = []
    clip_list = list(dict.fromkeys(clip_names or ["idle", "walk"]))
    clip_list.extend(name for name in model.motion.get("clips", {}) if name not in clip_list)
    if "idle" not in clip_list:
        clip_list.insert(0, "idle")
    leg_nodes = [index for index, bone in enumerate(model.bones) if bone.name.startswith("leg") or bone.name in ("rightLeg", "leftLeg", "rightArm", "leftArm", "wing0", "wing1", "tail")]
    head_nodes = [index for index, bone in enumerate(model.bones) if bone.name == "head"]
    for clip in clip_list:
        channels = []
        samplers = []
        authored = model.motion.get("clips", {}).get(clip)
        if model.motion and authored is None:
            raise ValueError(f"Missing authored clip {clip!r}; refusing a generic replacement")
        if authored is not None:
            for track in authored["tracks"]:
                target_index = index_by_name[track["bone"]]
                times = track["times"]
                path = track.get("path", "rotation")
                values = ([_quaternion_euler(v) for v in track["values"]]
                          if path == "rotation" else track["values"])
                if len(times) != len(values) or len(times) < 2 or any(a >= b for a,b in zip(times,times[1:])):
                    raise ValueError(f"Invalid authored key times: {clip}/{track['bone']}")
                components = 4 if path == "rotation" else 3
                time_data = struct.pack(f"<{len(times)}f", *times)
                value_data = b"".join(struct.pack(f"<{components}f", *v) for v in values)
                time_acc = add_accessor(add_bytes(time_data), len(times), 5126, "SCALAR", [times[0]], [times[-1]])
                value_acc = add_accessor(add_bytes(value_data), len(values), 5126, f"VEC{components}")
                samplers.append({"input": time_acc, "output": value_acc, "interpolation": "LINEAR"})
                channels.append({"sampler":len(samplers)-1,"target":{"node":target_index,"path":path}})
            if not channels:
                raise ValueError(f"Authored clip {clip!r} is empty")
            animations.append({"name":clip,"samplers":samplers,"channels":channels})
            continue
        targets = head_nodes if clip == "idle" else (leg_nodes or head_nodes)
        angle = 8.0 if clip == "idle" else (28.0 if clip in ("run", "fly", "punch") else 18.0)
        for offset, node_index in enumerate(targets[:8]):
            sign = -1.0 if offset % 2 else 1.0
            times = struct.pack("<3f", 0.0, 0.5, 1.0)
            keys = struct.pack("<12f", *_quat_x(0), *_quat_x(sign * angle), *_quat_x(0))
            time_acc = add_accessor(add_bytes(times), 3, 5126, "SCALAR", [0.0], [1.0])
            rot_acc = add_accessor(add_bytes(keys), 3, 5126, "VEC4")
            samplers.append({"input": time_acc, "output": rot_acc, "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": node_index, "path": "rotation"}})
        if not channels:
            times = struct.pack("<2f", 0.0, 1.0)
            keys = struct.pack("<8f", *_quat_x(0), *_quat_x(4))
            time_acc = add_accessor(add_bytes(times), 2, 5126, "SCALAR", [0.0], [1.0])
            rot_acc = add_accessor(add_bytes(keys), 2, 5126, "VEC4")
            samplers.append({"input": time_acc, "output": rot_acc, "interpolation": "LINEAR"})
            channels.append({"sampler": 0, "target": {"node": 0, "path": "rotation"}})
        animations.append({"name": clip, "samplers": samplers, "channels": channels})

    gltf = {
        "asset": {"version": "2.0", "generator": "cubatarium-creaturegen"},
        "scene": 0,
        "scenes": [{"nodes": scene_nodes}],
        "nodes": nodes,
        "skins": [{
            "joints": joint_nodes,
            "inverseBindMatrices": ibm_accessor,
        }],
        "meshes": [{"primitives": primitives}],
        "materials": materials,
        "images":[{"uri":"textures/"+m["name"]+".png"} for m in materials],
        "textures":[{"source":i,"sampler":0} for i in range(len(materials))],
        "samplers":[{"magFilter":9728,"minFilter":9728,"wrapS":33071,"wrapT":33071}],
        "animations": animations,
        "buffers": [{"uri": "model.bin", "byteLength": len(blob)}],
        "bufferViews": buffer_views,
        "accessors": accessors,
    }
    return gltf, bytes(blob)
