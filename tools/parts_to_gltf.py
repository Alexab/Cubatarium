#!/usr/bin/env python3
"""Convert models/items/<id>.json parts_v1 into models/items/<id>/model.gltf.

Educational CC0 stand-in meshes so glTF icon/FP/wear paths work before (or without)
upstream pack import. After pack import, real glTF replaces these files.

Usage:
  python tools/parts_to_gltf.py
  python tools/parts_to_gltf.py --item-id iron_pickaxe
  python tools/parts_to_gltf.py --retarget-content
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODELS = ROOT / "models" / "items"
CONTENT = ROOT / "content" / "items"


def box_mesh(ox: float, oy: float, oz: float, sx: float, sy: float, sz: float):
    """Axis-aligned box centered at offset with size; returns pos+uv floats and indices."""
    hx, hy, hz = sx * 0.5, sy * 0.5, sz * 0.5
    # 8 corners
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
    # faces: -Z +Z -Y +Y -X +X with UVs
    faces = [
        (0, 1, 2, 3),  # -Z
        (5, 4, 7, 6),  # +Z
        (4, 5, 1, 0),  # -Y
        (3, 2, 6, 7),  # +Y
        (4, 0, 3, 7),  # -X
        (1, 5, 6, 2),  # +X
    ]
    uvs = [(0, 0), (1, 0), (1, 1), (0, 1)]
    pos_uv: list[float] = []
    indices: list[int] = []
    base = 0
    for face in faces:
        for i, ci in enumerate(face):
            x, y, z = corners[ci]
            u, v = uvs[i]
            pos_uv.extend([x, y, z, u, v])
        indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])
        base += 4
    return pos_uv, indices


def write_gltf(dest_dir: Path, item_id: str, parts: list[dict], license_note: str) -> None:
    dest_dir.mkdir(parents=True, exist_ok=True)
    all_pos: list[float] = []
    all_idx: list[int] = []
    vert_base = 0
    for part in parts:
        off = part.get("offset", [0, 0, 0])
        size = part.get("size", [0.2, 0.2, 0.2])
        ox, oy, oz = float(off[0]), float(off[1]), float(off[2])
        sx, sy, sz = float(size[0]), float(size[1]), float(size[2])
        pos_uv, indices = box_mesh(ox, oy, oz, max(0.02, sx), max(0.02, sy), max(0.02, sz))
        # pos_uv is xyzuv interleaved — split for glTF accessors
        nverts = len(pos_uv) // 5
        for i in range(nverts):
            all_pos.extend(pos_uv[i * 5 : i * 5 + 3])
        # store uv separately in same buffer after positions... keep interleaved in one blob
        # Simpler: rebuild as separate POSITION and TEXCOORD buffers
        all_idx.extend([i + vert_base for i in indices])
        vert_base += nverts

    # Rebuild cleanly with separate attributes
    positions: list[float] = []
    texcoords: list[float] = []
    indices2: list[int] = []
    vbase = 0
    for part in parts:
        off = part.get("offset", [0, 0, 0])
        size = part.get("size", [0.2, 0.2, 0.2])
        ox, oy, oz = float(off[0]), float(off[1]), float(off[2])
        sx, sy, sz = float(size[0]), float(size[1]), float(size[2])
        pos_uv, indices = box_mesh(ox, oy, oz, max(0.02, sx), max(0.02, sy), max(0.02, sz))
        nverts = len(pos_uv) // 5
        for i in range(nverts):
            positions.extend(pos_uv[i * 5 : i * 5 + 3])
            texcoords.extend(pos_uv[i * 5 + 3 : i * 5 + 5])
        indices2.extend([i + vbase for i in indices])
        vbase += nverts

    if not positions:
        positions = [-0.05, 0, -0.05, 0.05, 0, -0.05, 0.05, 0.2, -0.05, -0.05, 0.2, -0.05]
        texcoords = [0, 0, 1, 0, 1, 1, 0, 1]
        indices2 = [0, 1, 2, 0, 2, 3]

    pos_bytes = b"".join(struct.pack("<fff", positions[i], positions[i + 1], positions[i + 2]) for i in range(0, len(positions), 3))
    uv_bytes = b"".join(struct.pack("<ff", texcoords[i], texcoords[i + 1]) for i in range(0, len(texcoords), 2))
    # align uv to 4
    pad = (4 - (len(pos_bytes) % 4)) % 4
    pos_bytes += b"\x00" * pad
    idx_bytes = b"".join(struct.pack("<H", i) for i in indices2)
    pad2 = (4 - (len(uv_bytes) % 4)) % 4
    uv_bytes += b"\x00" * pad2

    bin_blob = pos_bytes + uv_bytes + idx_bytes
    bin_name = "model.bin"
    (dest_dir / bin_name).write_bytes(bin_blob)

    nverts = len(positions) // 3
    min_v = [min(positions[i::3]) for i in range(3)]
    max_v = [max(positions[i::3]) for i in range(3)]
    pos_len = len(pos_bytes)
    uv_len = len(uv_bytes)
    idx_len = len(idx_bytes)
    uv_ofs = pos_len
    idx_ofs = pos_len + uv_len

    gltf = {
        "asset": {"version": "2.0", "generator": "cubatarium parts_to_gltf"},
        "buffers": [{"uri": bin_name, "byteLength": len(bin_blob)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": pos_len - pad, "target": 34962},
            {"buffer": 0, "byteOffset": uv_ofs, "byteLength": uv_len - pad2, "target": 34962},
            {"buffer": 0, "byteOffset": idx_ofs, "byteLength": idx_len, "target": 34963},
        ],
        "accessors": [
            {
                "bufferView": 0,
                "componentType": 5126,
                "count": nverts,
                "type": "VEC3",
                "min": min_v,
                "max": max_v,
            },
            {"bufferView": 1, "componentType": 5126, "count": nverts, "type": "VEC2"},
            {"bufferView": 2, "componentType": 5123, "count": len(indices2), "type": "SCALAR"},
        ],
        "meshes": [
            {
                "name": item_id,
                "primitives": [
                    {
                        "attributes": {"POSITION": 0, "TEXCOORD_0": 1},
                        "indices": 2,
                        "mode": 4,
                        "material": 0,
                    }
                ],
            }
        ],
        "materials": [
            {
                "name": "body",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.72, 0.72, 0.76, 1.0],
                    "metallicFactor": 0.0,
                    "roughnessFactor": 1.0,
                },
            }
        ],
        "nodes": [{"mesh": 0, "name": item_id}],
        "scenes": [{"nodes": [0]}],
        "scene": 0,
    }
    (dest_dir / "model.gltf").write_text(json.dumps(gltf, indent=2) + "\n", encoding="utf-8")
    meta = {
        "id": item_id,
        "format": "gltf",
        "license": "CC0-1.0",
        "source": "cubatarium_parts_v1",
        "notes": license_note,
    }
    (dest_dir / "ATTRIBUTION.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    (dest_dir / "LICENSE.txt").write_text(
        "CC0-1.0\nGenerated from Cubatarium parts_v1 educational meshes.\n"
        "Replace with upstream CC0 pack import when available.\n",
        encoding="utf-8",
    )


def retarget(item_id: str) -> None:
    path = CONTENT / f"{item_id}.json"
    if not path.is_file():
        return
    data = json.loads(path.read_text(encoding="utf-8"))
    data["model"] = f"models/items/{item_id}/model.gltf"
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--item-id")
    ap.add_argument("--retarget-content", action="store_true")
    args = ap.parse_args()

    jsons = sorted(MODELS.glob("*.json"))
    if args.item_id:
        jsons = [MODELS / f"{args.item_id}.json"]
    n = 0
    for jp in jsons:
        if not jp.is_file():
            print(f"SKIP missing {jp}")
            continue
        data = json.loads(jp.read_text(encoding="utf-8"))
        parts = data.get("parts") or []
        item_id = data.get("id") or jp.stem
        dest = MODELS / item_id
        write_gltf(
            dest,
            item_id,
            parts,
            "parts_v1 box extrusion; optional upstream pack override",
        )
        if args.retarget_content:
            retarget(item_id)
        print(f"WROTE {dest / 'model.gltf'}")
        n += 1
    print(f"Done. {n} glTF model(s).")


if __name__ == "__main__":
    main()
