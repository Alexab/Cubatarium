#!/usr/bin/env python3
"""Bake per-part rigid_voxels textures from Luanti mesh atlases."""

from __future__ import annotations

import argparse
import json
import re
import struct
import zlib
from pathlib import Path

import yaml

from b3d_read import B3DVertex, load_b3d_vertices

ROOT = Path(__file__).resolve().parent.parent
TOOLS = Path(__file__).resolve().parent
RESEARCH_DEFAULT = Path(r"E:/Work/Home/CubatariumTextureResearch")

try:
    from PIL import Image
except ImportError:
    raise SystemExit("Pillow required: pip install pillow")


def load_yaml(path: Path) -> dict:
    return yaml.safe_load(path.read_text(encoding="utf-8"))


def hex_to_rgb(value: str) -> tuple[int, int, int]:
    value = value.lstrip("#")
    if len(value) == 6:
        return int(value[0:2], 16), int(value[2:4], 16), int(value[4:6], 16)
    raise ValueError(f"bad color {value}")


def multiply_blend(base: Image.Image, overlay: Image.Image, rgb: tuple[int, int, int] | None) -> Image.Image:
    out = base.convert("RGBA")
    top = overlay.convert("RGBA")
    if rgb:
        tint = Image.new("RGBA", top.size, (*rgb, 255))
        top = Image.blend(top, tint, 0.65)
    return Image.alpha_composite(out, top)


def load_atlas(spec: dict, research: Path) -> Image.Image:
    if "composite" in spec:
        layers = spec["composite"]
        result: Image.Image | None = None
        for layer in layers:
            mod = research / layer["mod"]
            path = mod / layer["file"]
            if not path.is_file():
                raise FileNotFoundError(path)
            img = Image.open(path).convert("RGBA")
            tint = hex_to_rgb(layer["multiply"]) if "multiply" in layer else None
            if result is None:
                result = img
            else:
                result = multiply_blend(result, img, tint)
        assert result is not None
        return result
    tex = spec.get("texture")
    if not tex:
        raise ValueError("species needs texture or composite")
    path = research / tex
    if not path.is_file():
        raise FileNotFoundError(path)
    return Image.open(path).convert("RGBA")


def mesh_bounds(verts: list[B3DVertex]) -> tuple[float, float, float, float, float, float]:
    xs = [v.x for v in verts]
    ys = [v.y for v in verts]
    zs = [v.z for v in verts]
    return min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)


def vertex_to_block(
    v: B3DVertex,
    bounds: tuple[float, float, float, float, float, float],
    rest: list[float],
) -> tuple[float, float, float]:
    x0, x1, y0, y1, z0, z1 = bounds
    bx = (v.x - (x0 + x1) / 2) / (x1 - x0) * rest[0] if x1 != x0 else 0.0
    by = (v.y - y0) / (y1 - y0) * rest[1] if y1 != y0 else 0.0
    bz = (v.z - (z0 + z1) / 2) / (z1 - z0) * rest[2] if z1 != z0 else 0.0
    return bx, by, bz


def part_contains(
    bx: float, by: float, bz: float, offset: list[float], size: list[float], margin: float = 0.0
) -> bool:
    ox, oy, oz = offset
    sx, sy, sz = size
    return (
        abs(bx - ox) <= sx / 2 + margin
        and abs(by - oy) <= sy / 2 + margin
        and abs(bz - oz) <= sz / 2 + margin
    )


def assign_vertex_parts(
    verts: list[B3DVertex],
    bounds: tuple[float, float, float, float, float, float],
    rest: list[float],
    part_defs: list[dict],
    match_margin: float,
    leg_margin: float | None = None,
) -> dict[int, str]:
    leg_margin = match_margin if leg_margin is None else leg_margin
    assignments: dict[int, str] = {}
    for i, v in enumerate(verts):
        bx, by, bz = vertex_to_block(v, bounds, rest)
        best_pid: str | None = None
        best_dist = float("inf")
        for p in part_defs:
            margin = leg_margin if "leg" in p["id"] else match_margin
            if not part_contains(bx, by, bz, p["offset"], p["size"], margin):
                continue
            ox, oy, oz = p["offset"]
            dist = (bx - ox) ** 2 + (by - oy) ** 2 + (bz - oz) ** 2
            if dist < best_dist:
                best_dist = dist
                best_pid = p["id"]
        if best_pid:
            assignments[i] = best_pid
    return assignments


def uv_bbox_from_parts(
    verts: list[B3DVertex],
    bounds: tuple[float, float, float, float, float, float],
    rest: list[float],
    part_defs: list[dict],
    part_ids: list[str],
    pad: float,
    match_margin: float,
    assignments: dict[int, str] | None = None,
) -> tuple[float, float, float, float] | None:
    id_set = set(part_ids)
    if assignments is None:
        assignments = assign_vertex_parts(
            verts, bounds, rest, part_defs, match_margin, leg_margin
        )
    uvs: list[tuple[float, float]] = []
    for i, v in enumerate(verts):
        if assignments.get(i) in id_set:
            uvs.append((v.u, v.v))
    if not uvs:
        return None
    u0 = max(0.0, min(u for u, _ in uvs) - pad)
    u1 = min(1.0, max(u for u, _ in uvs) + pad)
    v0 = max(0.0, min(v for _, v in uvs) - pad)
    v1 = min(1.0, max(v for _, v in uvs) + pad)
    if u1 <= u0 or v1 <= v0:
        return None
    return u0, v0, u1, v1


def crop_uv(atlas: Image.Image, rect: tuple[float, float, float, float], size: int) -> Image.Image:
    w, h = atlas.size
    u0, v0, u1, v1 = rect
    box = (int(u0 * w), int(v0 * h), max(int(u1 * w), int(u0 * w) + 1), max(int(v1 * h), int(v0 * h) + 1))
    return atlas.crop(box).resize((size, size), Image.NEAREST)


def opaque_fill(img: Image.Image) -> Image.Image:
    """Fill transparent pixels from nearest opaque neighbors; force alpha=255."""
    out = img.convert("RGBA").copy()
    w, h = out.size
    if w == 0 or h == 0:
        return out
    px = out.load()
    for _ in range(max(w, h)):
        changed = False
        nxt = out.copy()
        npx = nxt.load()
        for y in range(h):
            for x in range(w):
                if px[x, y][3] >= 250:
                    continue
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and px[nx, ny][3] >= 250:
                        npx[x, y] = (px[nx, ny][0], px[nx, ny][1], px[nx, ny][2], 255)
                        changed = True
                        break
        out = nxt
        px = out.load()
        if not changed:
            break
    opaque_rgb: list[tuple[int, int, int]] = []
    for y in range(h):
        for x in range(w):
            if px[x, y][3] >= 250:
                opaque_rgb.append(px[x, y][:3])
    if opaque_rgb:
        fill = opaque_rgb[len(opaque_rgb) // 2]
        for y in range(h):
            for x in range(w):
                if px[x, y][3] < 250:
                    px[x, y] = (*fill, 255)
    return out


def prune_orphan_stems(tex_dir: Path, stem_rects: dict[str, tuple]) -> None:
    keep = set(stem_rects.keys()) | {"icon"}
    for path in tex_dir.glob("*.png"):
        if path.stem not in keep:
            path.unlink()
            print(f"  removed orphan {path.name}")


def bake_species(
    species_id: str,
    sources: dict,
    maps: dict,
    research: Path,
    models_root: Path,
    out_size: int,
    icon_size: int,
    pad: float,
    match_margin: float,
    leg_margin: float,
) -> None:
    spec = sources["species"][species_id]
    creature_path = models_root / "creatures" / species_id / "creature.json"
    creature = json.loads(creature_path.read_text(encoding="utf-8"))
    part_defs = creature["visual"]["parts"]
    rest = creature["bounds"]["rest"]

    archetype = maps["species_archetype"][species_id]
    groups = maps["default_part_groups"][archetype]

    atlas = load_atlas(spec, research)
    manual = spec.get("manual_uv", {})

    stem_rects: dict[str, tuple[float, float, float, float]] = {}
    for stem, rect in manual.items():
        if len(rect) == 4:
            stem_rects[stem] = tuple(rect)

    model_rel = spec.get("model")
    if model_rel and any(g["stem"] not in stem_rects for g in groups.values()):
        verts = load_b3d_vertices(research / model_rel)
        bounds = mesh_bounds(verts)
        assignments = assign_vertex_parts(
            verts, bounds, rest, part_defs, match_margin, leg_margin
        )
        for group in groups.values():
            stem = group["stem"]
            if stem in stem_rects:
                continue
            rect = uv_bbox_from_parts(
                verts, bounds, rest, part_defs, group["part_ids"], pad, match_margin, assignments
            )
            if rect:
                stem_rects[stem] = rect
    elif not model_rel and not stem_rects:
        raise ValueError(f"{species_id}: no model or manual_uv")

    tex_dir = models_root / "creatures" / species_id / "textures"
    tex_dir.mkdir(parents=True, exist_ok=True)
    for stem, rect in stem_rects.items():
        baked = opaque_fill(crop_uv(atlas, rect, out_size))
        baked.save(tex_dir / f"{stem}.png")
        print(f"  {species_id}/{stem}.png <- uv {rect}")
    prune_orphan_stems(tex_dir, stem_rects)

    icon_path = spec.get("icon")
    if icon_path:
        src = research / icon_path
        if src.is_file():
            icon_img = Image.open(src).convert("RGBA").resize(
                (icon_size, icon_size), Image.NEAREST
            )
            icon_img.transpose(Image.FLIP_TOP_BOTTOM).save(tex_dir / "icon.png")
    elif stem_rects:
        u0 = min(r[0] for r in stem_rects.values())
        v0 = min(r[1] for r in stem_rects.values())
        u1 = max(r[2] for r in stem_rects.values())
        v1 = max(r[3] for r in stem_rects.values())
        crop_uv(atlas, (u0, v0, u1, v1), icon_size).save(tex_dir / "icon.png")

    patch_creature_icon_mode(models_root, species_id)

    for stem in ("body", "leg", "arm", "face"):
        if stem not in stem_rects and (tex_dir / f"{stem}.png").exists():
            pass


def bake_skin(
    skin_id: str,
    skin_spec: dict,
    sources: dict,
    maps: dict,
    research: Path,
    models_root: Path,
    out_size: int,
    match_margin: float,
    leg_margin: float,
) -> None:
    skin_json_path = models_root / "skins" / skin_id / "skin.json"
    skin_json = json.loads(skin_json_path.read_text(encoding="utf-8"))
    creature_id = skin_json["creature_id"]

    creature_spec: dict
    if "texture" in skin_spec:
        creature_spec = {"texture": skin_spec["texture"]}
    else:
        creature_spec = dict(sources["species"][creature_id])
        if "multiply" in skin_spec and "composite" in creature_spec:
            composite = []
            for i, layer in enumerate(creature_spec["composite"]):
                entry = dict(layer)
                if i == len(creature_spec["composite"]) - 1:
                    entry["multiply"] = skin_spec["multiply"]
                composite.append(entry)
            creature_spec["composite"] = composite

    atlas = load_atlas(creature_spec, research)

    archetype = maps["species_archetype"][creature_id]
    groups = maps["default_part_groups"][archetype]
    creature = json.loads(
        (models_root / "creatures" / creature_id / "creature.json").read_text(encoding="utf-8")
    )
    part_defs = creature["visual"]["parts"]
    rest = creature["bounds"]["rest"]

    manual = sources["species"].get(creature_id, {}).get("manual_uv", {})
    stem_rects: dict[str, tuple[float, float, float, float]] = {}
    for stem, rect in manual.items():
        if len(rect) == 4:
            stem_rects[stem] = tuple(rect)

    model_rel = sources["species"][creature_id].get("model")
    if model_rel and any(g["stem"] not in stem_rects for g in groups.values()):
        verts = load_b3d_vertices(research / model_rel)
        bounds = mesh_bounds(verts)
        pad = float(maps.get("uv_pad", 0.02))
        assignments = assign_vertex_parts(
            verts, bounds, rest, part_defs, match_margin, leg_margin
        )
        for group in groups.values():
            stem = group["stem"]
            if stem in stem_rects:
                continue
            rect = uv_bbox_from_parts(
                verts, bounds, rest, part_defs, group["part_ids"], pad, match_margin, assignments
            )
            if rect:
                stem_rects[stem] = rect

    tex_dir = models_root / "skins" / skin_id / "textures"
    tex_dir.mkdir(parents=True, exist_ok=True)
    texture_map = skin_json.get("visual", {}).get("texture_map", {})
    for part_stem, skin_stem in texture_map.items():
        rect = stem_rects.get(part_stem)
        if not rect:
            continue
        opaque_fill(crop_uv(atlas, rect, out_size)).save(tex_dir / f"{skin_stem}.png")
    if "body" in stem_rects:
        opaque_fill(crop_uv(atlas, stem_rects["body"], out_size)).save(
            tex_dir / "diffuse.png"
        )
    print(f"  skin {skin_id}")


def patch_creature_json_layout(models_root: Path, species_id: str, layout: str) -> None:
    path = models_root / "creatures" / species_id / "creature.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    vis = data.setdefault("visual", {})
    if vis.get("texture_layout") != layout:
        vis["texture_layout"] = layout
        path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def patch_creature_icon_mode(models_root: Path, species_id: str) -> None:
    path = models_root / "creatures" / species_id / "creature.json"
    if not path.is_file():
        return
    data = json.loads(path.read_text(encoding="utf-8"))
    vis = data.setdefault("visual", {})
    icon = vis.setdefault("icon", {})
    if icon.get("mode") != "species_texture":
        icon["mode"] = "species_texture"
        path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--research", type=Path, default=RESEARCH_DEFAULT)
    parser.add_argument("--species", action="append")
    parser.add_argument("--skin", action="append")
    args = parser.parse_args()

    sources = load_yaml(TOOLS / "creature_luanti_sources.yaml")
    maps = load_yaml(TOOLS / "creature_rigid_uv_maps.yaml")
    out_size = int(maps.get("output_size", 64))
    icon_size = int(maps.get("icon_size", 32))
    pad = float(maps.get("uv_pad", 0.02))
    match_margin = float(maps.get("uv_match_margin", 0.15))
    leg_margin = float(maps.get("uv_leg_match_margin", 0.35))

    species_list = args.species or list(sources["species"].keys())
    for species_id in species_list:
        print(f"bake {species_id}")
        layout = "player_skin_atlas" if species_id == "human" else "rigid_crop"
        patch_creature_json_layout(ROOT / "models", species_id, layout)
        if species_id == "human":
            continue
        try:
            bake_species(
                species_id, sources, maps, args.research.resolve(),
                ROOT / "models", out_size, icon_size, pad, match_margin, leg_margin,
            )
        except FileNotFoundError as exc:
            print(f"SKIP {species_id}: {exc}")

    skin_list = args.skin or list(sources.get("skins", {}).keys())
    for skin_id in skin_list:
        bake_skin(
            skin_id, sources["skins"][skin_id], sources, maps,
            args.research.resolve(), ROOT / "models", out_size, match_margin, leg_margin,
        )

    patch_creature_json_layout(ROOT / "models", "human", "player_skin_atlas")
    print("done")


if __name__ == "__main__":
    main()
