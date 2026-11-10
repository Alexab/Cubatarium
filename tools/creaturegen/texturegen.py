"""Bake anatomical atlases. Patterns live in part space, then in the box UV."""

from __future__ import annotations

from PIL import Image, ImageDraw

from creaturegen.anatomy import Cube, Model
from creaturegen.hashutil import stable_seed


ROLE_KEY = {
    "body": "body",
    "belly": "belly",
    "face": "face",
    "muzzle": "muzzle",
    "limb": "limb",
    "accent": "accent",
    "horn": "horn",
    "wool": "wool",
    "wing": "wing",
    "shell": "shell",
    "eye": "eye",
}


def _rgb(palette: dict, role: str) -> tuple[int, int, int]:
    key = ROLE_KEY.get(role, "body")
    value = palette.get(key) or palette["body"]
    return int(value[0]), int(value[1]), int(value[2])


def _shade(color, amount: int) -> tuple[int, int, int]:
    return tuple(max(0, min(255, channel + amount)) for channel in color)


def _paint_box(draw: ImageDraw.ImageDraw, cube: Cube, palette: dict, pattern: str, seed: int) -> None:
    u, v = cube.uv
    w, h, d = cube.size
    role = _rgb(palette, cube.role)
    mark = _rgb(palette, "mark")
    eye = _rgb(palette, "eye")
    # Regions follow BoneSkeletonCubeMeshBuilder.FacePixels.
    regions = {
        "west": (u, v + d, d, h),
        "north": (u + d, v + d, w, h),
        "east": (u + d + w, v + d, d, h),
        "south": (u + d + w + d, v + d, w, h),
        "up": (u + d, v, w, d),
        "down": (u + d + w, v, w, d),
    }
    for name, (x, y, rw, rh) in regions.items():
        fill = role
        if name == "down":
            fill = _shade(role, -28)
        elif name == "up":
            fill = _shade(role, 18)
        elif name in ("north", "south"):
            fill = _shade(role, 8 if name == "north" else -8)
        draw.rectangle((x, y, x + rw - 1, y + rh - 1), fill=fill + (255,))
        if pattern == "stripes" and rw > 2 and rh > 1:
            step = 3 if stable_seed(seed, cube.role) % 2 == 0 else 4
            for stripe in range(0, rh, step):
                draw.line((x, y + stripe, x + rw - 1, y + stripe), fill=mark + (255,))
        elif pattern == "bands" and rh > 3 and rw > 1:
            y1 = y + rh // 3 + max(1, rh // 5)
            if y1 > y + rh // 3:
                draw.rectangle((x, y + rh // 3, x + rw - 1, y1), fill=mark + (255,))
        elif pattern == "spots" and rw > 3 and rh > 3:
            draw.ellipse((x + 1, y + 1, x + max(2, rw // 2), y + max(2, rh // 2)), fill=mark + (255,))
        elif pattern == "saddle" and name == "up" and rw > 4 and d > 3:
            draw.rectangle((x + rw // 5, y + 1, x + max(rw // 5 + 1, (4 * rw) // 5), y + d - 2), fill=mark + (255,))
        elif pattern == "plates" and name in ("up", "north") and rw > 3 and rh > 3:
            draw.rectangle((x + 1, y + 1, x + rw - 2, y + max(2, rh // 4)), outline=mark + (255,))
        elif pattern == "mask" and name == "north" and cube.role == "face" and rw > 1 and rh > 2:
            draw.rectangle((x, y, x + rw - 1, y + max(1, rh // 3)), fill=mark + (255,))
    north = regions["north"]
    if cube.role in ("face", "muzzle") and north[2] >= 3 and north[3] >= 3:
        eye_size = max(1, north[2] // 5)
        gap = max(1, north[2] // 6)
        ey = north[1] + north[3] // 3
        left = north[0] + gap
        right = north[0] + north[2] - gap - eye_size
        if right > left + eye_size:
            draw.rectangle((left, ey, left + eye_size, ey + eye_size), fill=eye + (255,))
            draw.rectangle((right, ey, right + eye_size, ey + eye_size), fill=eye + (255,))
            mouth_y = ey + eye_size + max(1, north[3] // 5)
            if mouth_y < north[1] + north[3] - 1:
                draw.line((left, mouth_y, right + eye_size, mouth_y), fill=eye + (255,))


def bake_atlas(model: Model, palette: dict, pattern: str, species: str, version: int) -> Image.Image:
    image = Image.new("RGBA", model.texture_size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    for index, cube in enumerate(model.cubes()):
        _paint_box(draw, cube, palette, pattern, stable_seed(species, cube.role, index, version))
    return image


def bake_icon(atlas: Image.Image) -> Image.Image:
    side = min(atlas.size)
    crop = atlas.crop((0, 0, side, side))
    return crop.resize((32, 32), Image.Resampling.NEAREST)


def bake_sprite(palette: dict, species: str, version: int) -> Image.Image:
    image = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    core = _rgb(palette, "belly")
    edge = _rgb(palette, "body")
    dark = _rgb(palette, "mark")
    seed = stable_seed(species, "sprite", version)
    for y in range(4, 30):
        width = 4 + ((28 - y) * (6 + seed % 5)) // 24
        x0 = 16 - width // 2
        color = core if 12 < y < 22 else edge
        if y % 5 == seed % 5:
            color = dark
        draw.line((x0, y, x0 + width, y), fill=color + (255,))
    draw.rectangle((14, 16, 17, 20), fill=dark + (255,))
    return image


def bake_flat(palette: dict, role: str, pattern: str, species: str, size: tuple[int, int]) -> Image.Image:
    image = Image.new("RGBA", size, _rgb(palette, role) + (255,))
    draw = ImageDraw.Draw(image)
    mark = _rgb(palette, "mark") + (255,)
    if pattern == "stripes":
        for y in range(0, size[1], 4):
            draw.line((0, y, size[0], y), fill=mark)
    elif pattern == "spots":
        draw.ellipse((2, 2, size[0] // 2, size[1] // 2), fill=mark)
    elif pattern == "bands":
        draw.rectangle((0, size[1] // 3, size[0], size[1] // 3 + 3), fill=mark)
    else:
        draw.rectangle((1, 1, size[0] - 2, max(2, size[1] // 5)), fill=mark)
    return image
