#!/usr/bin/env python3
"""Export Luanti .b3d skeleton mesh to skinned glTF 2.0."""

from __future__ import annotations

import json
import math
import struct
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

from b3d_read import B3DBone, B3DKeyframe, iter_b3d_bones, load_b3d_document

# Blitz3D mob meshes are ~10-20 units tall; Cubatarium uses blocks.
DEFAULT_B3D_UNITS_PER_BLOCK = 10.0


@dataclass
class ExportVertex:
  x: float
  y: float
  z: float
  u: float
  v: float
  joints: tuple[int, int, int, int] = (0, 0, 0, 0)
  weights: tuple[float, float, float, float] = (1.0, 0.0, 0.0, 0.0)


@dataclass
class ExportMesh:
  vertices: list[ExportVertex] = field(default_factory=list)
  indices: list[int] = field(default_factory=list)


@dataclass
class ExportJoint:
  name: str
  parent: int = -1
  translation: tuple[float, float, float] = (0.0, 0.0, 0.0)
  rotation: tuple[float, float, float, float] = (1.0, 0.0, 0.0, 0.0)
  scale: tuple[float, float, float] = (1.0, 1.0, 1.0)


def quat_normalize(q: tuple[float, float, float, float]) -> tuple[float, float, float, float]:
  w, x, y, z = q
  length = math.sqrt(w * w + x * x + y * y + z * z)
  if length <= 1e-8:
    return 1.0, 0.0, 0.0, 0.0
  inv = 1.0 / length
  return w * inv, x * inv, y * inv, z * inv


def mat4_from_trs(
    translation: tuple[float, float, float],
    rotation_wxyz: tuple[float, float, float, float],
    scale: tuple[float, float, float],
) -> list[float]:
  w, x, y, z = quat_normalize(rotation_wxyz)
  xx, yy, zz = x * x, y * y, z * z
  xy, xz, yz = x * y, x * z, y * z
  wx, wy, wz = w * x, w * y, w * z
  sx, sy, sz = scale
  r00 = (1 - 2 * (yy + zz)) * sx
  r01 = (2 * (xy - wz)) * sx
  r02 = (2 * (xz + wy)) * sx
  r10 = (2 * (xy + wz)) * sy
  r11 = (1 - 2 * (xx + zz)) * sy
  r12 = (2 * (yz - wx)) * sy
  r20 = (2 * (xz - wy)) * sz
  r21 = (2 * (yz + wx)) * sz
  r22 = (1 - 2 * (xx + yy)) * sz
  tx, ty, tz = translation
  return [
      r00, r10, r20, 0.0,
      r01, r11, r21, 0.0,
      r02, r12, r22, 0.0,
      tx, ty, tz, 1.0,
  ]


def mat4_mul(a: list[float], b: list[float]) -> list[float]:
  out = [0.0] * 16
  for col in range(4):
    for row in range(4):
      out[col * 4 + row] = sum(a[k * 4 + row] * b[col * 4 + k] for k in range(4))
  return out


def mat4_inverse(m: list[float]) -> list[float]:
  inv = [0.0] * 16
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10]
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10]
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9]
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9]
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10]
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10]
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9]
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9]
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6]
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6]
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5]
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5]
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6]
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6]
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5]
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5]
  det = sum(m[i] * inv[i] for i in range(0, 16, 4))
  if abs(det) < 1e-12:
    return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
  inv_det = 1.0 / det
  return [v * inv_det for v in inv]


def transform_vec3(m: list[float], x: float, y: float, z: float) -> tuple[float, float, float]:
  return (
      m[0] * x + m[4] * y + m[8] * z + m[12],
      m[1] * x + m[5] * y + m[9] * z + m[13],
      m[2] * x + m[6] * y + m[10] * z + m[14],
  )


def joint_global_bind_matrices(joints: list[ExportJoint]) -> list[list[float]]:
  globals_m: list[list[float]] = []
  for joint in joints:
    local = mat4_from_trs(joint.translation, joint.rotation, joint.scale)
    if joint.parent >= 0:
      global_m = mat4_mul(globals_m[joint.parent], local)
    else:
      global_m = local
    globals_m.append(global_m)
  return globals_m


def infer_units_per_block(
    roots: list[B3DBone],
    bounds_blocks: float | tuple[float, float, float] = 1.0,
    visual_scale_avg: float = 1.0,
) -> float:
  """B3D mob mesh → blocks using Luanti rules (BS=10, visual_size on scene node).

  Collisionbox is for physics only; mesh scale must not be derived from it when
  the mob's b3d aspect ratio differs from the AABB (e.g. kitten).
  """
  del roots, bounds_blocks
  scale = max(float(visual_scale_avg), 1e-3)
  return DEFAULT_B3D_UNITS_PER_BLOCK / scale


def flatten_joints(roots: list[B3DBone], units_per_block: float = DEFAULT_B3D_UNITS_PER_BLOCK) -> tuple[list[ExportJoint], list[B3DBone]]:
  joints: list[ExportJoint] = []
  bones: list[B3DBone] = []

  def walk(bone: B3DBone, parent_index: int) -> int:
    index = len(joints)
    bones.append(bone)
    joints.append(
        ExportJoint(
            name=bone.name or f"bone_{index}",
            parent=parent_index,
            translation=(
                bone.position[0] / units_per_block,
                bone.position[1] / units_per_block,
                bone.position[2] / units_per_block,
            ),
            rotation=quat_normalize(bone.rotation),
            scale=bone.scale,
        )
    )
    for child in bone.children:
      walk(child, index)
    return index

  for root in roots:
    walk(root, -1)
  return joints, bones


def normalize_vertex_weights(
    pairs: list[tuple[int, float]],
) -> tuple[tuple[int, int, int, int], tuple[float, float, float, float]]:
  if not pairs:
    return (0, 0, 0, 0), (1.0, 0.0, 0.0, 0.0)
  pairs = sorted(pairs, key=lambda item: item[1], reverse=True)[:4]
  total = sum(weight for _, weight in pairs)
  if total <= 1e-8:
    return (0, 0, 0, 0), (1.0, 0.0, 0.0, 0.0)
  joints = [joint for joint, _ in pairs]
  weights = [weight / total for _, weight in pairs]
  while len(joints) < 4:
    joints.append(0)
    weights.append(0.0)
  return tuple(joints), tuple(weights)


def build_mesh(joints: list[ExportJoint], bones: list[B3DBone], scale: float) -> ExportMesh:
    """Merge every bone mesh into one skinned mesh (animalworld uses per-bone cubes)."""
    mesh = ExportMesh()
    bind_globals = joint_global_bind_matrices(joints)
    vertex_offset = 0

    for bone_index, bone in enumerate(bones):
        if not bone.mesh.vertices:
            continue

        local_weights: dict[int, list[tuple[int, float]]] = {}
        for entry in bone.vertex_weights:
            if entry.weight <= 0.0:
                continue
            local_weights.setdefault(entry.vertex_id, []).append(
                (bone_index, entry.weight)
            )

        for vertex_id, vert in enumerate(bone.mesh.vertices):
            lx, ly, lz = vert.x / scale, vert.y / scale, vert.z / scale
            gx, gy, gz = transform_vec3(bind_globals[bone_index], lx, ly, lz)
            joints_tuple, weights_tuple = normalize_vertex_weights(
                local_weights.get(vertex_id, [(bone_index, 1.0)])
            )
            mesh.vertices.append(
                ExportVertex(
                    x=gx,
                    y=gy,
                    z=gz,
                    u=vert.u,
                    v=1.0 - vert.v,
                    joints=joints_tuple,
                    weights=weights_tuple,
                )
            )

        for idx in bone.mesh.indices:
            mesh.indices.append(vertex_offset + idx)
        vertex_offset += len(bone.mesh.vertices)
    return mesh


def align_mesh_to_feet(mesh: ExportMesh) -> float:
    """Shift bind-pose vertices so the lowest point sits on Y=0 (feet on ground)."""
    if not mesh.vertices:
        return 0.0
    min_y = min(vert.y for vert in mesh.vertices)
    if min_y >= -1e-6:
        return 0.0
    lift = -min_y
    for vert in mesh.vertices:
        vert.y += lift
    return lift


def load_species_stem_rects(species_id: str) -> dict[str, tuple[float, float, float, float]]:
    """manual_uv atlas regions from creature_luanti_sources.yaml (normalized 0-1)."""
    yaml_path = Path(__file__).resolve().parent / "creature_luanti_sources.yaml"
    if not yaml_path.is_file():
        return {}
    try:
        import yaml
    except ImportError:
        return {}
    data = yaml.safe_load(yaml_path.read_text(encoding="utf-8")) or {}
    spec = (data.get("species") or {}).get(species_id) or {}
    manual = spec.get("manual_uv") or {}
    out: dict[str, tuple[float, float, float, float]] = {}
    for stem, rect in manual.items():
        if isinstance(rect, list) and len(rect) == 4:
            out[str(stem)] = tuple(float(v) for v in rect)  # type: ignore[assignment]
    return out


def stem_for_atlas_uv(
    u: float,
    v: float,
    stem_rects: dict[str, tuple[float, float, float, float]],
) -> str:
    for stem, (u0, v0, u1, v1) in stem_rects.items():
        if u0 - 1e-4 <= u <= u1 + 1e-4 and v0 - 1e-4 <= v <= v1 + 1e-4:
            return stem
    if "body" in stem_rects:
        return "body"
    return next(iter(stem_rects))


def remap_atlas_uv(
    u: float,
    v: float,
    rect: tuple[float, float, float, float],
) -> tuple[float, float]:
    u0, v0, u1, v1 = rect
    du = max(u1 - u0, 1e-6)
    dv = max(v1 - v0, 1e-6)
    return (u - u0) / du, (v - v0) / dv


def atlas_uv_from_vertex(vert: ExportVertex) -> tuple[float, float]:
    return vert.u, 1.0 - vert.v


def split_mesh_by_stem(
    mesh: ExportMesh,
    stem_rects: dict[str, tuple[float, float, float, float]],
) -> list[tuple[str, ExportMesh]]:
    """Split mesh into per-texture-stem submeshes with rigid_crop UV remapping."""
    if not mesh.vertices or not mesh.indices or not stem_rects:
        return [("body", mesh)]

    groups: dict[str, list[tuple[int, int, int]]] = defaultdict(list)
    for t in range(0, len(mesh.indices), 3):
        i0, i1, i2 = mesh.indices[t : t + 3]
        u = v = 0.0
        for idx in (i0, i1, i2):
            au, av = atlas_uv_from_vertex(mesh.vertices[idx])
            u += au
            v += av
        u /= 3.0
        v /= 3.0
        groups[stem_for_atlas_uv(u, v, stem_rects)].append((i0, i1, i2))

    out: list[tuple[str, ExportMesh]] = []
    for stem, tris in sorted(groups.items()):
        rect = stem_rects.get(stem) or stem_rects.get("body")
        if rect is None:
            rect = next(iter(stem_rects.values()))
        sub = ExportMesh()
        index_map: dict[int, int] = {}
        for i0, i1, i2 in tris:
            for old_i in (i0, i1, i2):
                if old_i in index_map:
                    continue
                src = mesh.vertices[old_i]
                au, av = atlas_uv_from_vertex(src)
                ru, rv = remap_atlas_uv(au, av, rect)
                index_map[old_i] = len(sub.vertices)
                sub.vertices.append(
                    ExportVertex(
                        x=src.x,
                        y=src.y,
                        z=src.z,
                        u=ru,
                        v=1.0 - rv,
                        joints=src.joints,
                        weights=src.weights,
                    )
                )
            sub.indices.extend([index_map[i0], index_map[i1], index_map[i2]])
        if sub.vertices and sub.indices:
            out.append((stem, sub))
    return out or [("body", mesh)]


def prepare_textured_meshes(
    mesh: ExportMesh,
    species_id: str,
) -> list[tuple[str, ExportMesh]]:
    stem_rects = load_species_stem_rects(species_id)
    if not stem_rects:
        return [("body", mesh)]
    return split_mesh_by_stem(mesh, stem_rects)


def sample_keyframes(
    keyframes: list[B3DKeyframe], frame: int
) -> tuple[tuple[float, float, float] | None, tuple[float, float, float, float] | None]:
  if not keyframes:
    return None, None
  chosen = keyframes[0]
  for kf in keyframes:
    if kf.frame <= frame:
      chosen = kf
    else:
      break
  return chosen.position, chosen.rotation


def build_gltf_animations(
    joints: list[ExportJoint],
    bones: list[B3DBone],
    clip_frames: dict[str, tuple[int, int]],
    fps: float,
    scale: float,
) -> tuple[list[dict], bytes, list[dict], list[dict]]:
    """Build glTF animations and binary chunk (times + outputs)."""
    animations: list[dict] = []
    anim_blob = bytearray()
    extra_buffer_views: list[dict] = []
    extra_accessors: list[dict] = []

    def append_channel(
        clip: dict,
        node_index: int,
        path: str,
        times: list[float],
        values: list[float],
        value_type: str,
    ) -> None:
        if len(times) < 2 or not values:
            return
        time_off = len(anim_blob)
        for t in times:
            anim_blob.extend(struct.pack("<f", t))
        val_off = len(anim_blob)
        for v in values:
            anim_blob.extend(struct.pack("<f", v))
        time_bv = len(extra_buffer_views)
        extra_buffer_views.append(
            {
                "buffer": 0,
                "byteOffset": time_off,
                "byteLength": val_off - time_off,
            }
        )
        time_acc = len(extra_accessors)
        extra_accessors.append(
            {
                "bufferView": time_bv,
                "componentType": 5126,
                "count": len(times),
                "type": "SCALAR",
                "min": [min(times)],
                "max": [max(times)],
            }
        )
        val_bv = len(extra_buffer_views)
        extra_buffer_views.append(
            {
                "buffer": 0,
                "byteOffset": val_off,
                "byteLength": len(anim_blob) - val_off,
            }
        )
        val_acc = len(extra_accessors)
        component_count = 3 if value_type == "VEC3" else 4
        extra_accessors.append(
            {
                "bufferView": val_bv,
                "componentType": 5126,
                "count": len(times),
                "type": value_type,
            }
        )
        sampler_index = len(clip["samplers"])
        clip["samplers"].append(
            {
                "input": time_acc,
                "interpolation": "LINEAR",
                "output": val_acc,
            }
        )
        clip["channels"].append(
            {
                "sampler": sampler_index,
                "target": {"node": node_index, "path": path},
            }
        )

    for clip_name, (frame_start, frame_end) in clip_frames.items():
        clip: dict = {"name": clip_name, "channels": [], "samplers": []}
        for joint_index, bone in enumerate(bones):
            if not bone.keyframes:
                continue
            node_index = joint_index + 1
            times: list[float] = []
            trans: list[float] = []
            rots: list[float] = []
            for frame in range(frame_start, frame_end + 1):
                pos, rot = sample_keyframes(bone.keyframes, frame)
                t = (frame - frame_start) / fps
                times.append(t)
                if pos is not None:
                    trans.extend([pos[0] / scale, pos[1] / scale, pos[2] / scale])
                else:
                    trans.extend(list(joints[joint_index].translation))
                if rot is not None:
                    w, x, y, z = quat_normalize(rot)
                    rots.extend([x, y, z, w])
                else:
                    w, x, y, z = joints[joint_index].rotation
                    rots.extend([x, y, z, w])
            append_channel(clip, node_index, "rotation", times, rots, "VEC4")
            if any(kf.position is not None for kf in bone.keyframes):
                append_channel(clip, node_index, "translation", times, trans, "VEC3")
        if clip["channels"]:
            animations.append(clip)
    return animations, bytes(anim_blob), extra_buffer_views, extra_accessors


def bones_have_keyframes(bones: list[B3DBone]) -> bool:
  return any(bone.keyframes for bone in bones)


def _texture_uri_for_stem(stem: str, fallback: str = "textures/body.png") -> str:
  if stem == "body":
    return fallback
  return f"textures/{stem}.png"


def _append_rigid_primitive(
    blob: bytearray,
    mesh: ExportMesh,
    buffer_views: list[dict],
    accessors: list[dict],
) -> dict:
  pos_off = len(blob)
  xs: list[float] = []
  ys: list[float] = []
  zs: list[float] = []
  for vert in mesh.vertices:
    blob.extend(struct.pack("<fff", vert.x, vert.y, vert.z))
    xs.append(vert.x)
    ys.append(vert.y)
    zs.append(vert.z)
  uv_off = len(blob)
  for vert in mesh.vertices:
    blob.extend(struct.pack("<ff", vert.u, vert.v))
  idx_off = len(blob)
  for idx in mesh.indices:
    blob.extend(struct.pack("<H", idx))
  bv_base = len(buffer_views)
  buffer_views.extend(
      [
          {
              "buffer": 0,
              "byteOffset": pos_off,
              "byteLength": uv_off - pos_off,
              "target": 34962,
          },
          {
              "buffer": 0,
              "byteOffset": uv_off,
              "byteLength": idx_off - uv_off,
              "target": 34962,
          },
          {
              "buffer": 0,
              "byteOffset": idx_off,
              "byteLength": len(blob) - idx_off,
              "target": 34963,
          },
      ]
  )
  acc_base = len(accessors)
  accessors.extend(
      [
          {
              "bufferView": bv_base,
              "componentType": 5126,
              "count": len(mesh.vertices),
              "type": "VEC3",
              "min": [min(xs), min(ys), min(zs)],
              "max": [max(xs), max(ys), max(zs)],
          },
          {
              "bufferView": bv_base + 1,
              "componentType": 5126,
              "count": len(mesh.vertices),
              "type": "VEC2",
          },
          {
              "bufferView": bv_base + 2,
              "componentType": 5123,
              "count": len(mesh.indices),
              "type": "SCALAR",
          },
      ]
  )
  return {
      "attributes": {"POSITION": acc_base, "TEXCOORD_0": acc_base + 1},
      "indices": acc_base + 2,
  }


def _append_skinned_primitive(
    blob: bytearray,
    mesh: ExportMesh,
    buffer_views: list[dict],
    accessors: list[dict],
) -> dict:
  pos_off = len(blob)
  xs: list[float] = []
  ys: list[float] = []
  zs: list[float] = []
  for vert in mesh.vertices:
    blob.extend(struct.pack("<fff", vert.x, vert.y, vert.z))
    xs.append(vert.x)
    ys.append(vert.y)
    zs.append(vert.z)
  uv_off = len(blob)
  for vert in mesh.vertices:
    blob.extend(struct.pack("<ff", vert.u, vert.v))
  joint_off = len(blob)
  for vert in mesh.vertices:
    blob.extend(struct.pack("<BBBB", *vert.joints))
  weight_off = len(blob)
  for vert in mesh.vertices:
    blob.extend(struct.pack("<ffff", *vert.weights))
  idx_off = len(blob)
  for idx in mesh.indices:
    blob.extend(struct.pack("<H", idx))
  bv_base = len(buffer_views)
  buffer_views.extend(
      [
          {
              "buffer": 0,
              "byteOffset": pos_off,
              "byteLength": uv_off - pos_off,
              "target": 34962,
          },
          {
              "buffer": 0,
              "byteOffset": uv_off,
              "byteLength": joint_off - uv_off,
              "target": 34962,
          },
          {
              "buffer": 0,
              "byteOffset": joint_off,
              "byteLength": weight_off - joint_off,
              "target": 34962,
          },
          {
              "buffer": 0,
              "byteOffset": weight_off,
              "byteLength": idx_off - weight_off,
              "target": 34962,
          },
          {
              "buffer": 0,
              "byteOffset": idx_off,
              "byteLength": len(blob) - idx_off,
              "target": 34963,
          },
      ]
  )
  acc_base = len(accessors)
  accessors.extend(
      [
          {
              "bufferView": bv_base,
              "componentType": 5126,
              "count": len(mesh.vertices),
              "type": "VEC3",
              "min": [min(xs), min(ys), min(zs)],
              "max": [max(xs), max(ys), max(zs)],
          },
          {
              "bufferView": bv_base + 1,
              "componentType": 5126,
              "count": len(mesh.vertices),
              "type": "VEC2",
          },
          {
              "bufferView": bv_base + 2,
              "componentType": 5121,
              "count": len(mesh.vertices),
              "type": "VEC4",
          },
          {
              "bufferView": bv_base + 3,
              "componentType": 5126,
              "count": len(mesh.vertices),
              "type": "VEC4",
          },
          {
              "bufferView": bv_base + 4,
              "componentType": 5123,
              "count": len(mesh.indices),
              "type": "SCALAR",
          },
      ]
  )
  return {
      "attributes": {
          "POSITION": acc_base,
          "TEXCOORD_0": acc_base + 1,
          "JOINTS_0": acc_base + 2,
          "WEIGHTS_0": acc_base + 3,
      },
      "indices": acc_base + 4,
  }


def pack_static_gltf(
    species_id: str,
    textured_meshes: list[tuple[str, ExportMesh]],
    texture_uri: str = "textures/body.png",
) -> tuple[dict, bytes]:
  """Rigid glTF mesh (no skin) for b3d mobs without skeletal animation."""
  blob = bytearray()
  buffer_views: list[dict] = []
  accessors: list[dict] = []
  primitives: list[dict] = []
  stems: list[str] = []
  for material_index, (stem, mesh) in enumerate(textured_meshes):
    if not mesh.vertices or not mesh.indices:
      continue
    prim = _append_rigid_primitive(blob, mesh, buffer_views, accessors)
    prim["material"] = material_index
    primitives.append(prim)
    stems.append(stem)
  if not primitives:
    raise ValueError(f"{species_id}: no mesh primitives to export")

  gltf = {
      "asset": {"version": "2.0", "generator": "b3d_export_gltf.py"},
      "scene": 0,
      "scenes": [{"nodes": [0]}],
      "nodes": [{"name": species_id, "mesh": 0}],
      "meshes": [{"name": species_id, "primitives": primitives}],
      "materials": [
          {
              "name": stem,
              "pbrMetallicRoughness": {
                  "baseColorTexture": {"index": index},
                  "metallicFactor": 0.0,
                  "roughnessFactor": 1.0,
              },
              "doubleSided": True,
          }
          for index, stem in enumerate(stems)
      ],
      "textures": [{"source": index} for index in range(len(stems))],
      "images": [{"uri": _texture_uri_for_stem(stem, texture_uri)} for stem in stems],
      "buffers": [{"byteLength": len(blob), "uri": "model.bin"}],
      "bufferViews": buffer_views,
      "accessors": accessors,
  }
  return gltf, bytes(blob)


def pack_skinned_gltf(
    species_id: str,
    textured_meshes: list[tuple[str, ExportMesh]],
    joints: list[ExportJoint],
    texture_uri: str = "textures/body.png",
    animations: list[dict] | None = None,
    animation_blob: bytes | None = None,
    animation_buffer_views: list[dict] | None = None,
    animation_accessors: list[dict] | None = None,
) -> tuple[dict, bytes]:
  blob = bytearray()
  buffer_views: list[dict] = []
  accessors: list[dict] = []
  primitives: list[dict] = []
  stems: list[str] = []
  for material_index, (stem, mesh) in enumerate(textured_meshes):
    if not mesh.vertices or not mesh.indices:
      continue
    prim = _append_skinned_primitive(blob, mesh, buffer_views, accessors)
    prim["material"] = material_index
    primitives.append(prim)
    stems.append(stem)
  if not primitives:
    raise ValueError(f"{species_id}: no skinned mesh primitives to export")

  nodes = [{"name": "armature"}]
  for joint in joints:
    node: dict = {
        "name": joint.name,
        "translation": list(joint.translation),
        "rotation": [joint.rotation[1], joint.rotation[2], joint.rotation[3], joint.rotation[0]],
        "scale": list(joint.scale),
    }
    nodes.append(node)
  for i, joint in enumerate(joints):
    if joint.parent >= 0:
      parent_node = nodes[joint.parent + 1]
      parent_node.setdefault("children", []).append(i + 1)

  skin_joint_nodes = list(range(1, len(joints) + 1))
  bind_globals = joint_global_bind_matrices(joints)
  ibm: list[float] = []
  for global_m in bind_globals:
    inv_m = mat4_inverse(global_m)
    ibm.extend(inv_m)
  ibm_off = len(blob)
  for value in ibm:
    blob.extend(struct.pack("<f", float(value)))
  buffer_views.append(
      {"buffer": 0, "byteOffset": ibm_off, "byteLength": len(ibm) * 4}
  )
  accessors.append(
      {
          "bufferView": len(buffer_views) - 1,
          "componentType": 5126,
          "count": len(joints),
          "type": "MAT4",
      }
  )
  ibm_accessor_index = len(accessors) - 1

  if animation_blob:
    anim_base = len(blob)
    blob.extend(animation_blob)
    bv_base = len(buffer_views)
    accessor_base = len(accessors)
    for bv in animation_buffer_views or []:
      buffer_views.append(
          {
              "buffer": 0,
              "byteOffset": anim_base + bv["byteOffset"],
              "byteLength": bv["byteLength"],
          }
      )
    for acc in animation_accessors or []:
      remapped = dict(acc)
      remapped["bufferView"] = int(remapped["bufferView"]) + bv_base
      accessors.append(remapped)
    if animations:
      for anim in animations:
        for sampler in anim.get("samplers", []):
          sampler["input"] = int(sampler["input"]) + accessor_base
          sampler["output"] = int(sampler["output"]) + accessor_base

  mesh_node_index = len(nodes)
  nodes.append(
      {
          "name": species_id,
          "mesh": 0,
          "skin": 0,
      }
  )
  nodes[0]["children"] = [mesh_node_index]

  gltf = {
      "asset": {"version": "2.0", "generator": "b3d_export_gltf.py"},
      "scene": 0,
      "scenes": [{"nodes": [0]}],
      "nodes": nodes,
      "meshes": [{"name": species_id, "primitives": primitives}],
      "skins": [
          {
              "inverseBindMatrices": ibm_accessor_index,
              "joints": skin_joint_nodes,
              "skeleton": 1 if joints else 0,
          }
      ],
      "materials": [
          {
              "name": stem,
              "pbrMetallicRoughness": {
                  "baseColorTexture": {"index": index},
                  "metallicFactor": 0.0,
                  "roughnessFactor": 1.0,
              },
              "doubleSided": True,
          }
          for index, stem in enumerate(stems)
      ],
      "textures": [{"source": index} for index in range(len(stems))],
      "images": [{"uri": _texture_uri_for_stem(stem, texture_uri)} for stem in stems],
      "buffers": [{"byteLength": len(blob), "uri": "model.bin"}],
      "bufferViews": buffer_views,
      "accessors": accessors,
  }
  if animations:
    gltf["animations"] = animations
  return gltf, bytes(blob)


def export_b3d_to_gltf(
    b3d_path: Path,
    species_id: str,
    out_dir: Path,
    units_per_block: float = DEFAULT_B3D_UNITS_PER_BLOCK,
    texture_uri: str = "textures/body.png",
    clip_frames: dict[str, tuple[int, int]] | None = None,
    fps: float = 15.0,
    force_static: bool = False,
) -> Path:
  roots = load_b3d_document(b3d_path)
  joints, bones = flatten_joints(roots, units_per_block)
  mesh = build_mesh(joints, bones, units_per_block)
  if not mesh.vertices or not mesh.indices:
    raise ValueError(f"no mesh data in {b3d_path}")
  align_mesh_to_feet(mesh)
  textured_meshes = prepare_textured_meshes(mesh, species_id)

  use_static = force_static or (not clip_frames and not bones_have_keyframes(bones))
  if use_static:
    gltf, blob = pack_static_gltf(species_id, textured_meshes, texture_uri=texture_uri)
  else:
    animations: list[dict] | None = None
    animation_blob: bytes | None = None
    animation_buffer_views: list[dict] | None = None
    animation_accessors: list[dict] | None = None
    if clip_frames:
      animations, animation_blob, animation_buffer_views, animation_accessors = (
          build_gltf_animations(joints, bones, clip_frames, fps, units_per_block)
      )

    gltf, blob = pack_skinned_gltf(
        species_id,
        textured_meshes,
        joints,
        texture_uri=texture_uri,
        animations=animations,
        animation_blob=animation_blob,
        animation_buffer_views=animation_buffer_views,
        animation_accessors=animation_accessors,
    )
  out_dir.mkdir(parents=True, exist_ok=True)
  gltf_path = out_dir / "model.gltf"
  gltf_path.write_text(json.dumps(gltf, indent=2), encoding="utf-8")
  (out_dir / "model.bin").write_bytes(blob)
  return gltf_path
