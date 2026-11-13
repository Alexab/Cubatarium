"""Offline triangle/UV contact sheets; native first-person QA is separate."""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
MODELS = ROOT / "models" / "items"
OUT = ROOT / "docs" / "research" / "item-previews-v2"


# Compatibility names retained for the quality-report caller. Payloads are indexed
# triangles and textures now, never primitive AABBs.
from itemgen.raster import load_primitives as _load_parts_from_gltf
from itemgen.raster import render as _render


def make_contact_sheet(item_id: str, out_dir: Path | None = None) -> Path:
    item_dir = MODELS / item_id
    parts = _load_parts_from_gltf(item_dir)
    modes = [
        ("silhouette", 35.0),
        ("color", 35.0),
        ("gray", 35.0),
        ("inspection", 15.0),  # second asset inspection angle, not native FP
    ]
    cell = 160
    sheet = Image.new("RGBA", (cell * 4, cell + 28), (18, 18, 22, 255))
    draw = ImageDraw.Draw(sheet)
    labels = ["silhouette", "color", "gray", "inspection"]
    for i, (mode, yaw) in enumerate(modes):
        tile = _render(parts, mode="color" if mode == "inspection" else mode, size=cell, yaw=yaw)
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
