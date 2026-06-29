"""Assign b3d vertices to rigid parts and cube faces for box-unfold texture bake."""

from __future__ import annotations

from pathlib import Path

from PIL import Image

from b3d_read import B3DVertex
from box_uv_layout import (
    FACE_ORDER,
    face_pixel_rect,
    snap_normal,
    unfold_canvas_size,
)

def part_contains(
    bx: float, by: float, bz: float, offset: list[float], size: list[float], margin: float
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
    vertex_to_block_fn=None,
) -> dict[int, str]:
    if vertex_to_block_fn is None:
        from bake_rigid_creature_textures import vertex_to_block

        vertex_to_block_fn = vertex_to_block
    leg_margin = match_margin if leg_margin is None else leg_margin
    assignments: dict[int, str] = {}
    for i, v in enumerate(verts):
        bx, by, bz = vertex_to_block_fn(v, bounds, rest)
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


def local_face_uv_on_part(
    bx: float,
    by: float,
    bz: float,
    offset: list[float],
    size: list[float],
    face: str,
) -> tuple[float, float]:
    """Map block-space point on a part face to 0..1 within that face cell."""
    ox, oy, oz = offset
    sx, sy, sz = size
    lx = (bx - (ox - sx / 2)) / sx
    ly = (by - (oy - sy / 2)) / sy
    lz = (bz - (oz - sz / 2)) / sz
    lx = min(1.0, max(0.0, lx))
    ly = min(1.0, max(0.0, ly))
    lz = min(1.0, max(0.0, lz))
    if face == "pz":
        return lx, 1.0 - ly
    if face == "nz":
        return 1.0 - lx, 1.0 - ly
    if face == "px":
        return 1.0 - lz, 1.0 - ly
    if face == "nx":
        return lz, 1.0 - ly
    if face == "py":
        return lx, lz
    return lx, 1.0 - lz


def estimate_vertex_normal(
    vi: int,
    verts: list[B3DVertex],
    assignments: dict[int, str],
    part_id: str,
    part: dict,
    bounds,
    rest: list[float],
) -> str:
    from bake_rigid_creature_textures import vertex_to_block

    v = verts[vi]
    bx, by, bz = vertex_to_block(v, bounds, rest)
    ox, oy, oz = part["offset"]
    sx, sy, sz = part["size"]
    dx = (bx - ox) / max(sx, 1e-5)
    dy = (by - oy) / max(sy, 1e-5)
    dz = (bz - oz) / max(sz, 1e-5)
    return snap_normal(dx, dy, dz)


def bake_stem_box_unfold(    atlas: Image.Image,
    verts: list[B3DVertex],
    bounds: tuple[float, float, float, float, float, float],
    rest: list[float],
    part_defs: list[dict],
    part_ids: list[str],
    assignments: dict[int, str],
    texels_per_block: int = 16,
    pad: int = 1,
) -> Image.Image:
    """Merge unfold canvases for all parts sharing a texture stem."""
    from bake_rigid_creature_textures import vertex_to_block

    parts = [p for p in part_defs if p["id"] in part_ids]
    if not parts:
        raise ValueError("no parts for stem")
    pad = max(0, pad)
    max_cw = 0
    max_ch = 0
    for part in parts:
        cw, ch = unfold_canvas_size(
            part["size"][0], part["size"][1], part["size"][2], pad, texels_per_block
        )
        max_cw = max(max_cw, cw)
        max_ch = max(max_ch, ch)
    out = Image.new("RGBA", (max_cw, max_ch), (0, 0, 0, 0))
    aw, ah = atlas.size
    atlas_px = atlas.load()
    px = out.load()
    cw, ch = max_cw, max_ch

    id_set = set(part_ids)
    for i, v in enumerate(verts):
        pid = assignments.get(i)
        if pid not in id_set:
            continue
        part = next(p for p in part_defs if p["id"] == pid)
        face = estimate_vertex_normal(i, verts, assignments, pid, part, bounds, rest)
        bx, by, bz = vertex_to_block(v, bounds, rest)
        fu, fv = local_face_uv_on_part(bx, by, bz, part["offset"], part["size"], face)
        x0, y0, x1, y1 = face_pixel_rect(
            face, part["size"][0], part["size"][1], part["size"][2], pad, texels_per_block
        )
        pw = max(1, x1 - x0 + 1)
        ph = max(1, y1 - y0 + 1)
        tx = min(cw - 1, max(0, x0 + int(fu * (pw - 1))))
        ty = min(ch - 1, max(0, y0 + int(fv * (ph - 1))))
        au = min(aw - 1, max(0, int(v.u * aw)))
        av = min(ah - 1, max(0, int((1.0 - v.v) * ah)))
        color = atlas_px[au, av]
        if color[3] > 0:
            px[tx, ty] = color

    for _ in range(4):
        nxt = out.copy()
        npx = nxt.load()
        changed = False
        for y in range(ch):
            for x in range(cw):
                if px[x, y][3] >= 250:
                    continue
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < cw and 0 <= ny < ch and px[nx, ny][3] >= 250:
                        npx[x, y] = px[nx, ny]
                        changed = True
                        break
        out = nxt
        px = out.load()
        if not changed:
            break
    return out


def write_uv_sidecar(path: Path, sx: float, sy: float, sz: float) -> None:
    import json

    from box_uv_layout import box_face_uvs

    faces = box_face_uvs(sx, sy, sz)
    data = {
        "layout": "box_uv",
        "size_blocks": [sx, sy, sz],
        "faces": {
            name: [faces[name].u0, faces[name].v0, faces[name].u1, faces[name].v1]
            for name in FACE_ORDER
        },
    }
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
