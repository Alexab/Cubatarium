"""Minecraft-style box UV unfold layout for rigid creature parts."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class FaceUv:
    u0: float
    v0: float
    u1: float
    v1: float


def box_texture_size(sx: float, sy: float, sz: float) -> tuple[int, int]:
    """Pixel size for unfold at 1 texel per block unit (caller scales by texels_per_block)."""
    tex_w = max(1, int(round(2.0 * sx + 2.0 * sz)))
    tex_h = max(1, int(round(sy + 2.0 * sz)))
    return tex_w, tex_h


def box_texture_size_scaled(
    sx: float, sy: float, sz: float, texels_per_block: int = 16
) -> tuple[int, int]:
    tw, th = box_texture_size(sx, sy, sz)
    return max(4, tw * texels_per_block), max(4, th * texels_per_block)


def box_face_uvs(sx: float, sy: float, sz: float) -> dict[str, FaceUv]:
    """Normalized GL UV regions for unit-cube faces (+Z forward, +Y up).

    Unfold layout (image space, y down):
          [top]
    [left][front][right][back]
          [bottom]
    """
    tex_w = 2.0 * sx + 2.0 * sz
    tex_h = sy + 2.0 * sz
    if tex_w < 1e-6 or tex_h < 1e-6:
        full = FaceUv(0.0, 0.0, 1.0, 1.0)
        return {k: full for k in ("pz", "px", "nz", "nx", "py", "ny")}

    def gl_rect(ix0: float, iy0: float, ix1: float, iy1: float) -> FaceUv:
        # Image y=0 top -> OpenGL v=1 top; v0=bottom, v1=top in AppendFaceUv convention.
        return FaceUv(
            ix0 / tex_w,
            1.0 - iy1 / tex_h,
            ix1 / tex_w,
            1.0 - iy0 / tex_h,
        )

    return {
        "pz": gl_rect(sz, sz, sz + sx, sz + sy),
        "px": gl_rect(sz + sx + sz, sz, sz + sx + sz + sz, sz + sy),
        "nz": gl_rect(sz + sx + sz + sx, sz, sz + sx + sz + sx + sx, sz + sy),
        "nx": gl_rect(0.0, sz, sz, sz + sy),
        "py": gl_rect(sz, 0.0, sz + sx, sz),
        "ny": gl_rect(sz, sz + sy, sz + sx, sz + sy + sz),
    }


# Face order matches CreaturePartMeshData.h / GeometryEngine cube buffers.
FACE_ORDER: tuple[str, ...] = ("pz", "px", "nz", "nx", "py", "ny")


def snap_normal(nx: float, ny: float, nz: float) -> str:
    ax, ay, az = abs(nx), abs(ny), abs(nz)
    if ay >= ax and ay >= az:
        return "py" if ny >= 0.0 else "ny"
    if ax >= az:
        return "px" if nx >= 0.0 else "nx"
    return "pz" if nz >= 0.0 else "nz"


def face_pixel_rect(
    face: str, sx: float, sy: float, sz: float, pad: int, texels_per_block: int
) -> tuple[int, int, int, int]:
    """Inclusive pixel rect (x0, y0, x1, y1) in unfold image coords (y down)."""
    scale = texels_per_block
    sx_p = max(1, int(round(sx * scale)))
    sy_p = max(1, int(round(sy * scale)))
    sz_p = max(1, int(round(sz * scale)))
    if face == "pz":
        x0, y0 = sz_p + pad, sz_p + pad
        w, h = sx_p, sy_p
    elif face == "px":
        x0, y0 = sz_p + sx_p + sz_p + pad, sz_p + pad
        w, h = sz_p, sy_p
    elif face == "nz":
        x0, y0 = sz_p + sx_p + sz_p + sx_p + pad, sz_p + pad
        w, h = sx_p, sy_p
    elif face == "nx":
        x0, y0 = pad, sz_p + pad
        w, h = sz_p, sy_p
    elif face == "py":
        x0, y0 = sz_p + pad, pad
        w, h = sx_p, sz_p
    else:  # ny
        x0, y0 = sz_p + pad, sz_p + sy_p + pad
        w, h = sx_p, sz_p
    return x0, y0, x0 + w - 1, y0 + h - 1


def unfold_canvas_size(
    sx: float, sy: float, sz: float, pad: int, texels_per_block: int
) -> tuple[int, int]:
    tw, th = box_texture_size_scaled(sx, sy, sz, texels_per_block)
    return tw + 2 * pad, th + 2 * pad
