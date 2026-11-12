"""Export box parts with a shared 64x64 atlas to glTF 2.0."""

from __future__ import annotations

import json
import struct
from pathlib import Path
from typing import Any

from itemgen.texturegen import bake_atlas, role_uv


def box_mesh(
    ox: float,
    oy: float,
    oz: float,
    sx: float,
    sy: float,
    sz: float,
    uv_rect: tuple[float, float, float, float],
):
    hx, hy, hz = sx * 0.5, sy * 0.5, sz * 0.5
    corners = [
        (ox - hx, oy - hy, oz - hz),
        (ox + hx, oy - hy, oz - hz),
        (ox + hx, oy + hy, oz - hz),
        (ox - hx, oy + hy, oz - hz),
        (ox - hx, oy - hy, oz + hz),
        (ox + hx, oy - hy, oz + hz),
        (ox + hx, oy + hy, oz + hz),
        (ox - hx, oy + hy, oz + hz),
    ]
    faces = [
        (0, 1, 2, 3),
        (5, 4, 7, 6),
        (4, 5, 1, 0),
        (3, 2, 6, 7),
        (4, 0, 3, 7),
        (1, 5, 6, 2),
    ]
    u0, v0, u1, v1 = uv_rect
    # Inset slightly so nearest filtering stays inside the tile.
    pad_u = (u1 - u0) * 0.08
    pad_v = (v1 - v0) * 0.08
    uu0, uu1 = u0 + pad_u, u1 - pad_u
    vv0, vv1 = v0 + pad_v, v1 - pad_v
    uvs = [(uu0, vv0), (uu1, vv0), (uu1, vv1), (uu0, vv1)]
    positions: list[float] = []
    texcoords: list[float] = []
    indices: list[int] = []
    base = 0
    for face in faces:
        for i, ci in enumerate(face):
            x, y, z = corners[ci]
            u, v = uvs[i]
            positions.extend([x, y, z])
            texcoords.extend([u, v])
        indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])
        base += 4
    return positions, texcoords, indices


def write_gltf(
    dest_dir: Path,
    item_id: str,
    parts: list[dict[str, Any]],
    palette: dict[str, list[int]],
    *,
    material: str = "iron",
    generator: str = "cubatarium-itemgen",
) -> Path:
    dest_dir.mkdir(parents=True, exist_ok=True)
    tex_dir = dest_dir / "textures"
    tex_dir.mkdir(parents=True, exist_ok=True)
    atlas_path = tex_dir / "diffuse.png"
    bake_atlas(palette, material=material, out_path=atlas_path)

    roles: list[str] = []
    grouped: dict[str, list[dict[str, Any]]] = {}
    for part in parts:
        role = str(part.get("role", "head"))
        if role not in grouped:
            roles.append(role)
            grouped[role] = []
        grouped[role].append(part)

    buffer_views: list[dict[str, Any]] = []
    accessors: list[dict[str, Any]] = []
    materials: list[dict[str, Any]] = []
    primitives: list[dict[str, Any]] = []
    blob = bytearray()

    def align4() -> None:
        while len(blob) % 4:
            blob.append(0)

    for mat_index, role in enumerate(roles):
        role_parts = grouped[role]
        vbase = 0
        role_pos: list[float] = []
        role_uvs: list[float] = []
        role_idx: list[int] = []
        uv_rect = role_uv(role)
        for part in role_parts:
            off = part.get("offset", [0, 0, 0])
            size = part.get("size", [0.2, 0.2, 0.2])
            pos, uv, idx = box_mesh(
                float(off[0]),
                float(off[1]),
                float(off[2]),
                max(0.02, float(size[0])),
                max(0.02, float(size[1])),
                max(0.02, float(size[2])),
                uv_rect,
            )
            nverts = len(pos) // 3
            role_pos.extend(pos)
            role_uvs.extend(uv)
            role_idx.extend([i + vbase for i in idx])
            vbase += nverts

        pos_ofs = len(blob)
        for i in range(0, len(role_pos), 3):
            blob.extend(struct.pack("<fff", role_pos[i], role_pos[i + 1], role_pos[i + 2]))
        pos_len = len(blob) - pos_ofs
        align4()

        uv_ofs = len(blob)
        for i in range(0, len(role_uvs), 2):
            blob.extend(struct.pack("<ff", role_uvs[i], role_uvs[i + 1]))
        uv_len = len(blob) - uv_ofs
        align4()

        idx_ofs = len(blob)
        for i in role_idx:
            blob.extend(struct.pack("<H", i))
        idx_len = len(blob) - idx_ofs
        align4()

        bv0 = len(buffer_views)
        buffer_views.append(
            {"buffer": 0, "byteOffset": pos_ofs, "byteLength": pos_len, "target": 34962}
        )
        buffer_views.append(
            {"buffer": 0, "byteOffset": uv_ofs, "byteLength": uv_len, "target": 34962}
        )
        buffer_views.append(
            {"buffer": 0, "byteOffset": idx_ofs, "byteLength": idx_len, "target": 34963}
        )

        nverts = len(role_pos) // 3
        min_v = [min(role_pos[i::3]) for i in range(3)]
        max_v = [max(role_pos[i::3]) for i in range(3)]
        acc0 = len(accessors)
        accessors.append(
            {
                "bufferView": bv0,
                "componentType": 5126,
                "count": nverts,
                "type": "VEC3",
                "min": min_v,
                "max": max_v,
            }
        )
        accessors.append(
            {"bufferView": bv0 + 1, "componentType": 5126, "count": nverts, "type": "VEC2"}
        )
        accessors.append(
            {
                "bufferView": bv0 + 2,
                "componentType": 5123,
                "count": len(role_idx),
                "type": "SCALAR",
            }
        )

        materials.append(
            {
                "name": role,
                "pbrMetallicRoughness": {
                    "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                    "baseColorTexture": {"index": 0},
                    "metallicFactor": 0.05 if role in ("head", "edge", "accent") else 0.0,
                    "roughnessFactor": 0.75 if role in ("head", "edge") else 0.95,
                },
            }
        )
        primitives.append(
            {
                "attributes": {"POSITION": acc0, "TEXCOORD_0": acc0 + 1},
                "indices": acc0 + 2,
                "mode": 4,
                "material": mat_index,
            }
        )

    bin_name = "model.bin"
    (dest_dir / bin_name).write_bytes(bytes(blob))

    gltf = {
        "asset": {"version": "2.0", "generator": generator},
        "buffers": [{"uri": bin_name, "byteLength": len(blob)}],
        "bufferViews": buffer_views,
        "accessors": accessors,
        "images": [{"uri": "textures/diffuse.png"}],
        "textures": [{"source": 0}],
        "materials": materials,
        "meshes": [{"name": item_id, "primitives": primitives}],
        "nodes": [{"mesh": 0, "name": item_id}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
        "extras": {
            "cubatarium_grip": {"offset": [0.0, -0.15, 0.0], "note": "handle grasp"},
            "authoring_source": "cubatarium-recipe",
        },
    }
    (dest_dir / "model.gltf").write_text(
        json.dumps(gltf, indent=2) + "\n", encoding="utf-8"
    )
    return atlas_path
