#!/usr/bin/env python3
"""Export models/creatures/<species>/model.gltf from visual.parts[] or Luanti .b3d."""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = Path(__file__).resolve().parent

try:
    import yaml
except ImportError:
    yaml = None  # type: ignore

# Cube face winding (CCW when viewed from outside), UV per face full 0-1
FACE_VERTS = [
    # +Z front
    [(-0.5, -0.5, 0.5), (0.5, -0.5, 0.5), (0.5, 0.5, 0.5), (-0.5, 0.5, 0.5)],
    # -Z back
    [(0.5, -0.5, -0.5), (-0.5, -0.5, -0.5), (-0.5, 0.5, -0.5), (0.5, 0.5, -0.5)],
    # +X right
    [(0.5, -0.5, 0.5), (0.5, -0.5, -0.5), (0.5, 0.5, -0.5), (0.5, 0.5, 0.5)],
    # -X left
    [(-0.5, -0.5, -0.5), (-0.5, -0.5, 0.5), (-0.5, 0.5, 0.5), (-0.5, 0.5, -0.5)],
    # +Y top
    [(-0.5, 0.5, 0.5), (0.5, 0.5, 0.5), (0.5, 0.5, -0.5), (-0.5, 0.5, -0.5)],
    # -Y bottom
    [(-0.5, -0.5, -0.5), (0.5, -0.5, -0.5), (0.5, -0.5, 0.5), (-0.5, -0.5, 0.5)],
]
FACE_UVS = [(0, 1), (1, 1), (1, 0), (0, 0)]


def load_creature_json(species_dir: Path) -> dict:
    path = species_dir / "creature.json"
    if not path.is_file():
        raise FileNotFoundError(path)
    return json.loads(path.read_text(encoding="utf-8"))


def box_primitive(offset: list[float], size: list[float]) -> tuple[list[float], list[float], list[int]]:
    ox, oy, oz = offset
    sx, sy, sz = size
    positions: list[float] = []
    uvs: list[float] = []
    indices: list[int] = []
    base = 0
    for face in FACE_VERTS:
        for vi, (lx, ly, lz) in enumerate(face):
            positions.extend([ox + lx * sx, oy + ly * sy, oz + lz * sz])
            u, v = FACE_UVS[vi]
            uvs.extend([u, v])
        indices.extend([base, base + 1, base + 2, base, base + 2, base + 3])
        base += 4
    return positions, uvs, indices


def pack_buffer(primitives: list[dict]) -> bytes:
  """Pack POSITION, TEXCOORD, indices for each primitive into one .bin blob."""
  blob = bytearray()
  for prim in primitives:
    pos = prim["positions"]
    uv = prim["uvs"]
    idx = prim["indices"]
    prim["pos_byte_offset"] = len(blob)
    for i in range(0, len(pos), 3):
      blob.extend(struct.pack("<fff", pos[i], pos[i + 1], pos[i + 2]))
    prim["pos_byte_length"] = len(blob) - prim["pos_byte_offset"]

    prim["uv_byte_offset"] = len(blob)
    for i in range(0, len(uv), 2):
      blob.extend(struct.pack("<ff", uv[i], uv[i + 1]))
    prim["uv_byte_length"] = len(blob) - prim["uv_byte_offset"]

    prim["idx_byte_offset"] = len(blob)
    for i in idx:
      blob.extend(struct.pack("<H", i))
    prim["idx_byte_length"] = len(blob) - prim["idx_byte_offset"]
  return bytes(blob)


def build_gltf(species_id: str, primitives: list[dict], texture_stems: list[str]) -> tuple[dict, bytes]:
    blob = pack_buffer(primitives)
    buffer_views = []
    accessors = []
    materials = []
    mesh_primitives = []
    images = []
    textures = []

    byte_offset = 0
    for i, prim in enumerate(primitives):
        n_verts = len(prim["positions"]) // 3
        n_idx = len(prim["indices"])

        pos_bv = len(buffer_views)
        buffer_views.append({
            "buffer": 0,
            "byteOffset": prim["pos_byte_offset"],
            "byteLength": prim["pos_byte_length"],
            "target": 34962,
        })
        pos_acc = len(accessors)
        accessors.append({
            "bufferView": pos_bv,
            "componentType": 5126,
            "count": n_verts,
            "type": "VEC3",
            "min": prim["min_pos"],
            "max": prim["max_pos"],
        })

        uv_bv = len(buffer_views)
        buffer_views.append({
            "buffer": 0,
            "byteOffset": prim["uv_byte_offset"],
            "byteLength": prim["uv_byte_length"],
            "target": 34962,
        })
        uv_acc = len(accessors)
        accessors.append({
            "bufferView": uv_bv,
            "componentType": 5126,
            "count": n_verts,
            "type": "VEC2",
        })

        idx_bv = len(buffer_views)
        buffer_views.append({
            "buffer": 0,
            "byteOffset": prim["idx_byte_offset"],
            "byteLength": prim["idx_byte_length"],
            "target": 34963,
        })
        idx_acc = len(accessors)
        accessors.append({
            "bufferView": idx_bv,
            "componentType": 5123,
            "count": n_idx,
            "type": "SCALAR",
        })

        stem = prim.get("texture", texture_stems[0] if texture_stems else "body")
        tex_path = f"textures/{stem}.png"
        if tex_path not in [img["uri"] for img in images]:
            images.append({"uri": tex_path})
        tex_index = next(j for j, img in enumerate(images) if img["uri"] == tex_path)
        if tex_index >= len(textures):
            textures.append({"source": tex_index})

        mat_index = len(materials)
        materials.append({
            "name": stem,
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": tex_index},
                "metallicFactor": 0.0,
                "roughnessFactor": 1.0,
            },
            "doubleSided": True,
        })

        mesh_primitives.append({
            "attributes": {"POSITION": pos_acc, "TEXCOORD_0": uv_acc},
            "indices": idx_acc,
            "material": mat_index,
        })

    animations = [
        {
            "name": "idle",
            "channels": [],
            "samplers": [],
        },
        {
            "name": "walk",
            "channels": [
                {
                    "sampler": 0,
                    "target": {"node": 0, "path": "translation"},
                }
            ],
            "samplers": [
                {
                    "input": 0,
                    "interpolation": "LINEAR",
                    "output": 1,
                }
            ],
        },
    ]
    # Walk bob keyframes (Y translation)
    t_times = [0.0, 0.5, 1.0]
    t_vals = [0.0, 0.04, 0.0]
    t_blob = bytearray()
    for t in t_times:
        t_blob.extend(struct.pack("<f", t))
    v_blob = bytearray()
    for y in t_vals:
        v_blob.extend(struct.pack("<fff", 0.0, y, 0.0))
    walk_anim_blob = bytes(t_blob) + bytes(v_blob)
    # Append walk animation data to main blob
    walk_time_offset = len(blob)
    full_blob = blob + walk_anim_blob
    time_bv = len(buffer_views)
    buffer_views.append({"buffer": 0, "byteOffset": walk_time_offset, "byteLength": len(t_blob)})
    time_acc = len(accessors)
    accessors.append({"bufferView": time_bv, "componentType": 5126, "count": 3, "type": "SCALAR",
                      "min": [0.0], "max": [1.0]})
    val_bv = len(buffer_views)
    buffer_views.append({"buffer": 0, "byteOffset": walk_time_offset + len(t_blob), "byteLength": len(v_blob)})
    val_acc = len(accessors)
    accessors.append({"bufferView": val_bv, "componentType": 5126, "count": 3, "type": "VEC3"})
    animations[1]["channels"][0]["sampler"] = 0
    animations[1]["samplers"][0]["input"] = time_acc
    animations[1]["samplers"][0]["output"] = val_acc

    gltf = {
        "asset": {"version": "2.0", "generator": "convert_creature_mesh_to_gltf.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "root", "mesh": 0}],
        "meshes": [{"name": species_id, "primitives": mesh_primitives}],
        "materials": materials,
        "textures": textures,
        "images": images,
        "buffers": [{"byteLength": len(full_blob), "uri": "model.bin"}],
        "bufferViews": buffer_views,
        "accessors": accessors,
        "animations": animations,
    }
    return gltf, full_blob


def parts_from_creature(creature: dict) -> list[dict]:
    parts = creature.get("visual", {}).get("parts", [])
    if not parts:
        raise ValueError("creature.json has no visual.parts[]")
    primitives = []
    for part in parts:
        offset = part["offset"]
        size = part["size"]
        pos, uv, idx = box_primitive(offset, size)
        xs = pos[0::3]
        ys = pos[1::3]
        zs = pos[2::3]
        primitives.append({
            "positions": pos,
            "uvs": uv,
            "indices": idx,
            "texture": part.get("texture", "body"),
            "min_pos": [min(xs), min(ys), min(zs)],
            "max_pos": [max(xs), max(ys), max(zs)],
        })
    return primitives


RESEARCH_DEFAULT = Path(r"E:/Work/Home/CubatariumTextureResearch")


def resolve_b3d_path(species: str, research: Path | None = None) -> Path | None:
    if yaml is None:
        return None
    src = yaml.safe_load((TOOLS / "creature_luanti_sources.yaml").read_text(encoding="utf-8"))
    entry = src.get("species", {}).get(species, {})
    model = entry.get("model")
    if not model:
        return None
    root = research or RESEARCH_DEFAULT
    path = root / model.replace("/", "\\") if "\\" in str(root) else root / model
    return path if path.is_file() else None


def sync_runtime_creature(species_dir: Path) -> None:
    """Copy exported glTF into bin/models when the game runs from bin/."""
    runtime_root = ROOT / "bin" / "models" / "creatures" / species_dir.name
    if not (ROOT / "bin" / "models").is_dir():
        return
    runtime_root.parent.mkdir(parents=True, exist_ok=True)
    if runtime_root.exists():
        shutil.rmtree(runtime_root)
    shutil.copytree(species_dir, runtime_root)
    print(f"synced runtime -> {runtime_root}")


def export_species(
    species: str, out_dir: Path | None = None, prefer_b3d: bool = True
) -> Path:
    species_dir = out_dir or (ROOT / "models" / "creatures" / species)
    creature = load_creature_json(species_dir)
    if creature.get("id") != species:
        print(f"warn: folder {species} id={creature.get('id')}", file=sys.stderr)

    if prefer_b3d:
        b3d_path = resolve_b3d_path(species)
        if b3d_path is not None:
            from b3d_export_gltf import export_b3d_to_gltf, infer_units_per_block
            from b3d_read import load_b3d_document
            from luanti_mob_animation import (
                load_luanti_clips,
                load_mob_properties,
                luanti_visual_scale_avg,
            )

            props, _ = load_mob_properties(species)
            visual_scale = luanti_visual_scale_avg(props)
            roots = load_b3d_document(b3d_path)
            units_per_block = infer_units_per_block(
                roots, visual_scale_avg=visual_scale
            )
            clip_frames, fps = load_luanti_clips(species)
            force_static = species == "butterfly"
            gltf_path = export_b3d_to_gltf(
                b3d_path,
                species,
                species_dir,
                units_per_block=units_per_block,
                clip_frames=clip_frames or None,
                fps=fps,
                force_static=force_static,
            )
            print(f"OK {species}: b3d -> {gltf_path}")
            sync_runtime_creature(species_dir)
            return gltf_path

    parts = creature.get("visual", {}).get("parts")
    if not parts:
        raise ValueError(f"{species}: no visual.parts[] and no b3d source")
    primitives = parts_from_creature(creature)
    stems = list({p["texture"] for p in primitives})
    gltf, blob = build_gltf(species, primitives, stems)
    gltf_path = species_dir / "model.gltf"
    bin_path = species_dir / "model.bin"
    gltf_path.write_text(json.dumps(gltf, indent=2), encoding="utf-8")
    bin_path.write_bytes(blob)
    print(f"OK {species}: {len(primitives)} primitives -> {gltf_path}")
    sync_runtime_creature(species_dir)
    return gltf_path


def all_rigid_species() -> list[str]:
    out = []
    for p in sorted((ROOT / "models" / "creatures").iterdir()):
        cj = p / "creature.json"
        if not cj.is_file():
            continue
        d = json.loads(cj.read_text(encoding="utf-8"))
        if d.get("visual", {}).get("backend") == "rigid_voxels":
            out.append(d["id"])
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--species", help="species id")
    ap.add_argument("--all-rigid", action="store_true", help="export all rigid_voxels species")
    ap.add_argument("--all-with-b3d", action="store_true", help="export gltf species with b3d in yaml")
    ap.add_argument("--parts-only", action="store_true", help="skip b3d, export from parts[] only")
    args = ap.parse_args()

    if args.all_rigid:
        species_list = all_rigid_species()
    elif args.all_with_b3d:
        if yaml is None:
            print("PyYAML required for --all-with-b3d", file=sys.stderr)
            return 1
        src = yaml.safe_load((TOOLS / "creature_luanti_sources.yaml").read_text(encoding="utf-8"))
        with_b3d = {k for k, v in src.get("species", {}).items() if v.get("model")}
        species_list = sorted(
            p.name
            for p in (ROOT / "models" / "creatures").iterdir()
            if (p / "creature.json").is_file()
            and json.loads((p / "creature.json").read_text(encoding="utf-8"))
            .get("visual", {})
            .get("backend")
            == "gltf_skeleton"
            and p.name in with_b3d
        )
    elif args.species:
        species_list = [args.species]
    else:
        ap.print_help()
        return 1

    err = 0
    for sp in species_list:
        try:
            export_species(sp, prefer_b3d=not args.parts_only)
        except Exception as exc:
            print(f"FAIL {sp}: {exc}", file=sys.stderr)
            err = 1
    return err


if __name__ == "__main__":
    raise SystemExit(main())
