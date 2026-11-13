"""Bake a 64x64 role atlas for Cubatarium items."""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw

# 4x4 tiles of 16px on a 64x64 atlas.
TILE = 16
ATLAS = 64

ROLE_SLOTS = [
    "handle",
    "binding",
    "head",
    "edge",
    "accent",
    "body",
    "stem",
    "leaf",
    "flame",
    "leather",
    "water",
    "mark",
]


def role_uv(role: str) -> tuple[float, float, float, float]:
    """Return (u0, v0, u1, v1) in 0..1 for the role tile."""
    try:
        index = ROLE_SLOTS.index(role)
    except ValueError:
        index = ROLE_SLOTS.index("head")
    col = index % 4
    row = index // 4
    u0 = col * TILE / ATLAS
    v0 = row * TILE / ATLAS
    u1 = (col + 1) * TILE / ATLAS
    v1 = (row + 1) * TILE / ATLAS
    return u0, v0, u1, v1


def _shade(rgb: list[int] | tuple[int, int, int], delta: int) -> tuple[int, int, int]:
    return tuple(max(0, min(255, c + delta)) for c in rgb)  # type: ignore[return-value]


def bake_atlas(
    palette: dict[str, list[int]],
    *,
    material: str = "iron",
    out_path: Path | None = None,
) -> Image.Image:
    img = Image.new("RGBA", (ATLAS, ATLAS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    for index, role in enumerate(ROLE_SLOTS):
        color = palette.get(role) or palette.get("head") or palette.get("body") or [160, 160, 160]
        col = index % 4
        row = index // 4
        x0 = col * TILE
        y0 = row * TILE
        x1 = x0 + TILE - 1
        y1 = y0 + TILE - 1
        base = tuple(int(c) for c in color)
        draw.rectangle((x0, y0, x1, y1), fill=base + (255,))
        # Face shading for readable blocks in close FP.
        draw.rectangle((x0, y0, x1, y0 + 2), fill=_shade(base, 28) + (255,))
        draw.rectangle((x0, y1 - 2, x1, y1), fill=_shade(base, -32) + (255,))
        if material in ("wood", "torch", "bow", "arrow", "shield_wood", "organic", "bread"):
            for stripe in range(2, TILE - 2, 3):
                draw.line((x0 + 1, y0 + stripe, x1 - 1, y0 + stripe), fill=_shade(base, -18) + (255,))
        elif material in ("stone",):
            for spot in ((3, 4), (8, 9), (11, 3), (5, 12)):
                draw.point((x0 + spot[0], y0 + spot[1]), fill=_shade(base, -40) + (255,))
                draw.point((x0 + spot[0] + 1, y0 + spot[1]), fill=_shade(base, 20) + (255,))
        else:
            # Forged metals: rim + stamped mark, not color-only.
            draw.rectangle((x0 + 2, y0 + 2, x1 - 2, y1 - 2), outline=_shade(base, 35) + (255,))
            draw.rectangle((x0 + 5, y0 + 6, x0 + 10, y0 + 9), fill=_shade(base, -45) + (255,))
        if role == "edge":
            draw.line((x0 + 1, y0 + TILE // 2, x1 - 1, y0 + TILE // 2), fill=(240, 240, 240, 255))
        if role == "flame":
            draw.ellipse((x0 + 3, y0 + 2, x1 - 3, y1 - 2), fill=(255, 200, 80, 255))
    if out_path is not None:
        out_path.parent.mkdir(parents=True, exist_ok=True)
        img.save(out_path)
    return img
