"""Offline contact sheets: silhouette / color / grayscale / hand framing."""

from __future__ import annotations

import json
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
MODELS = ROOT / "models" / "items"
OUT = ROOT / "docs" / "research" / "item-previews"


def _load_parts_from_gltf(item_dir: Path) -> list[dict]:
    """Approximate boxes from glTF accessors for preview (AABB per primitive)."""
    gltf = json.loads((item_dir / "model.gltf").read_text(encoding="utf-8"))
    blob = (item_dir / "model.bin").read_bytes()
    accessors = gltf["accessors"]
    views = gltf["bufferViews"]
    mats = gltf.get("materials") or []
    tex_path = item_dir / "textures" / "diffuse.png"
    atlas = Image.open(tex_path).convert("RGBA") if tex_path.is_file() else None
    parts = []
    for prim in gltf["meshes"][0]["primitives"]:
        pos_acc = accessors[prim["attributes"]["POSITION"]]
        view = views[pos_acc["bufferView"]]
        off = view.get("byteOffset", 0)
        count = pos_acc["count"]
        xs, ys, zs = [], [], []
        for i in range(count):
            base = off + i * 12
            x, y, z = struct.unpack_from("<fff", blob, base)
            xs.append(x)
            ys.append(y)
            zs.append(z)
        mat_i = prim.get("material", 0)
        mat_name = mats[mat_i]["name"] if mat_i < len(mats) else "head"
        color = (180, 180, 180, 255)
        if atlas is not None:
            # Sample center of role tile via UV0 average.
            uv_acc = accessors[prim["attributes"]["TEXCOORD_0"]]
            uv_view = views[uv_acc["bufferView"]]
            uoff = uv_view.get("byteOffset", 0)
            u, v = struct.unpack_from("<ff", blob, uoff)
            px = min(atlas.width - 1, max(0, int(u * atlas.width)))
            py = min(atlas.height - 1, max(0, int(v * atlas.height)))
            color = atlas.getpixel((px, py))
        parts.append(
            {
                "min": (min(xs), min(ys), min(zs)),
                "max": (max(xs), max(ys), max(zs)),
                "color": color,
                "role": mat_name,
            }
        )
    return parts


def _project(x, y, z, yaw_deg: float, pitch_deg: float = -25.0):
    yaw = math.radians(yaw_deg)
    pitch = math.radians(pitch_deg)
    # Yaw around Y, then pitch around X.
    xz = x * math.cos(yaw) - z * math.sin(yaw)
    zz = x * math.sin(yaw) + z * math.cos(yaw)
    yy = y * math.cos(pitch) - zz * math.sin(pitch)
    depth = y * math.sin(pitch) + zz * math.cos(pitch)
    return xz, yy, depth


def _render(parts: list[dict], *, mode: str, size: int = 160, yaw: float = 35.0) -> Image.Image:
    img = Image.new("RGBA", (size, size), (24, 24, 28, 255) if mode != "silhouette" else (255, 255, 255, 255))
    draw = ImageDraw.Draw(img)
    projected = []
    for part in parts:
        corners = []
        mn, mx = part["min"], part["max"]
        for x in (mn[0], mx[0]):
            for y in (mn[1], mx[1]):
                for z in (mn[2], mx[2]):
                    corners.append(_project(x, y, z, yaw))
        depth = sum(c[2] for c in corners) / len(corners)
        projected.append((depth, corners, part))
    projected.sort(key=lambda t: t[0])  # far first

    xs = [c[0] for _, corners, _ in projected for c in corners]
    ys = [c[1] for _, corners, _ in projected for c in corners]
    if not xs:
        return img
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    span = max(max_x - min_x, max_y - min_y, 0.05)
    pad = size * 0.12
    scale = (size - 2 * pad) / span

    def to_px(x, y):
        cx = (min_x + max_x) * 0.5
        cy = (min_y + max_y) * 0.5
        px = size * 0.5 + (x - cx) * scale
        py = size * 0.5 - (y - cy) * scale
        return px, py

    # Cube face index sets for filled quads (better silhouette than AABB).
    faces = (
        (0, 1, 3, 2),
        (4, 5, 7, 6),
        (0, 1, 5, 4),
        (2, 3, 7, 6),
        (0, 2, 6, 4),
        (1, 3, 7, 5),
    )
    for _, corners, part in projected:
        pxs = [to_px(c[0], c[1]) for c in corners]
        if mode == "silhouette":
            fill = (0, 0, 0, 255)
        elif mode == "gray":
            r, g, b = part["color"][:3]
            lum = int(0.3 * r + 0.59 * g + 0.11 * b)
            fill = (lum, lum, lum, 255)
        else:
            fill = part["color"] if len(part["color"]) == 4 else tuple(part["color"]) + (255,)
        for face in faces:
            poly = [pxs[i] for i in face]
            draw.polygon(poly, fill=fill)
    return img


def make_contact_sheet(item_id: str, out_dir: Path | None = None) -> Path:
    item_dir = MODELS / item_id
    parts = _load_parts_from_gltf(item_dir)
    modes = [
        ("silhouette", 35.0),
        ("color", 35.0),
        ("gray", 35.0),
        ("hand", 15.0),  # closer / flatter framing for FP feel
    ]
    cell = 160
    sheet = Image.new("RGBA", (cell * 4, cell + 28), (18, 18, 22, 255))
    draw = ImageDraw.Draw(sheet)
    labels = ["silhouette", "color", "gray", "hand"]
    for i, (mode, yaw) in enumerate(modes):
        tile = _render(parts, mode="color" if mode == "hand" else mode, size=cell, yaw=yaw)
        if mode == "hand":
            # Dim vignette suggesting viewmodel framing.
            overlay = Image.new("RGBA", (cell, cell), (0, 0, 0, 0))
            od = ImageDraw.Draw(overlay)
            od.rectangle((0, 0, cell, 28), fill=(0, 0, 0, 90))
            od.rectangle((0, cell - 36, cell, cell), fill=(0, 0, 0, 110))
            tile = Image.alpha_composite(tile, overlay)
        sheet.paste(tile, (i * cell, 28))
        draw.text((i * cell + 8, 6), labels[i], fill=(220, 220, 220, 255))
    draw.text((8, cell + 8), item_id, fill=(180, 180, 190, 255))
    dest_root = out_dir or OUT
    dest_root.mkdir(parents=True, exist_ok=True)
    path = dest_root / f"{item_id}_contact.png"
    sheet.save(path)
    return path


def make_family_sheet(family: str, materials: list[str], out_dir: Path | None = None) -> Path:
    cell = 128
    cols = len(materials)
    rows = 3  # silhouette, color, gray
    sheet = Image.new("RGBA", (cell * cols, cell * rows + 24), (18, 18, 22, 255))
    draw = ImageDraw.Draw(sheet)
    for ci, mat in enumerate(materials):
        item_id = f"{mat}_{family}"
        item_dir = MODELS / item_id
        if not (item_dir / "model.gltf").is_file():
            continue
        parts = _load_parts_from_gltf(item_dir)
        for ri, mode in enumerate(("silhouette", "color", "gray")):
            tile = _render(parts, mode=mode, size=cell, yaw=35.0)
            sheet.paste(tile, (ci * cell, 24 + ri * cell))
        draw.text((ci * cell + 6, 4), mat, fill=(220, 220, 220, 255))
    dest_root = out_dir or OUT
    dest_root.mkdir(parents=True, exist_ok=True)
    path = dest_root / f"family_{family}_sheet.png"
    sheet.save(path)
    return path
