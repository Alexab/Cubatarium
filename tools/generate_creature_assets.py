#!/usr/bin/env python3
"""Generate creature/skin PNGs with distinct body, leg, arm, and atlas face textures."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SPECIES_COLORS: dict[str, tuple[int, int, int]] = {
    "human": (77, 179, 255),
    "scout": (255, 140, 26),
    "brute": (255, 51, 51),
    "drifter": (51, 217, 89),
}

SKIN_DIFFUSE_COLORS: list[tuple[str, tuple[int, int, int]]] = [
    ("models/skins/human_adventurer/textures/diffuse.png", (242, 191, 51)),
    ("models/skins/human_guard/textures/diffuse.png", (140, 140, 153)),
    ("models/skins/scout_golden/textures/diffuse.png", (255, 230, 51)),
    ("models/skins/brute_rust/textures/diffuse.png", (179, 89, 38)),
    ("models/skins/drifter_ice/textures/diffuse.png", (128, 217, 255)),
]


def png_chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def write_rgba_png(path: Path, pixels: list[tuple[int, int, int, int]], size: int = 64) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if len(pixels) != size * size:
        raise ValueError(f"{path}: expected {size * size} pixels, got {len(pixels)}")

    raw = bytearray()
    for y in range(size):
        raw.append(0)
        for x in range(size):
            r, g, b, a = pixels[y * size + x]
            raw.extend((r, g, b, a))

    compressed = zlib.compress(bytes(raw), 9)
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", ihdr)
    png += png_chunk(b"IDAT", compressed)
    png += png_chunk(b"IEND", b"")
    path.write_bytes(png)


def blend(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[int, int, int]:
    return (
        max(0, min(255, int(a[0] * (1 - t) + b[0] * t))),
        max(0, min(255, int(a[1] * (1 - t) + b[1] * t))),
        max(0, min(255, int(a[2] * (1 - t) + b[2] * t))),
    )


def darken(rgb: tuple[int, int, int], factor: float) -> tuple[int, int, int]:
    return (
        max(0, min(255, int(rgb[0] * factor))),
        max(0, min(255, int(rgb[1] * factor))),
        max(0, min(255, int(rgb[2] * factor))),
    )


def leg_tint(rgb: tuple[int, int, int]) -> tuple[int, int, int]:
    """Brownish shift so legs read clearly different from torso."""
    return blend(rgb, (55, 42, 38), 0.45)


def arm_tint(rgb: tuple[int, int, int]) -> tuple[int, int, int]:
    return blend(rgb, (200, 160, 120), 0.22)


def face_skin(rgb: tuple[int, int, int]) -> tuple[int, int, int]:
    return blend(rgb, (255, 240, 220), 0.35)


def write_body_png(path: Path, base_rgb: tuple[int, int, int], size: int = 64) -> None:
    """Atlas: +Z front (buttons), -Z back (plain dark), sides use corner patch."""
    side = blend(base_rgb, (255, 255, 255), 0.08)
    front = blend(base_rgb, (255, 255, 255), 0.22)
    back = darken(base_rgb, 0.45)
    button = darken(base_rgb, 0.3)
    pixels = [(side[0], side[1], side[2], 255)] * (size * size)

    # Front panel (+Z) — matches kUvBodyFront.
    x0, x1 = 18, 46
    y0, y1 = 14, 50
    for y in range(y0, y1):
        for x in range(x0, x1):
            pixels[y * size + x] = (*front, 255)

    # Three vertical buttons on chest.
    for bx in (26, 32, 38):
        for y in range(20, 44):
            for x in range(bx, bx + 4):
                pixels[y * size + x] = (*button, 255)

    # Back panel (-Z) — matches kUvBodyBack.
    for y in range(35, 52):
        for x in range(1, 12):
            pixels[y * size + x] = (*back, 255)

    write_rgba_png(path, pixels, size)


def write_leg_png(path: Path, base_rgb: tuple[int, int, int], size: int = 64) -> None:
    base = leg_tint(base_rgb)
    stripe = darken(base, 0.7)
    pixels = [(base[0], base[1], base[2], 255)] * (size * size)
    for y in range(size):
        for x in range(size):
            if (x // 5) % 2 == 0:
                pixels[y * size + x] = (*stripe, 255)
    write_rgba_png(path, pixels, size)


def write_arm_png(path: Path, base_rgb: tuple[int, int, int], size: int = 64) -> None:
    base = arm_tint(base_rgb)
    cuff = darken(base, 0.5)
    shoulder = blend(base, (255, 255, 255), 0.12)
    pixels = [(base[0], base[1], base[2], 255)] * (size * size)
    for y in range(size):
        for x in range(size):
            if y >= 52:
                pixels[y * size + x] = (*cuff, 255)
            elif y <= 10:
                pixels[y * size + x] = (*shoulder, 255)
    write_rgba_png(path, pixels, size)


def write_face_png(path: Path, base_rgb: tuple[int, int, int], size: int = 64) -> None:
    """Atlas: plain/hair in corners; eyes and mouth only on +X panel (UV ~0.28–0.72)."""
    plain = face_skin(base_rgb)
    hair = darken(base_rgb, 0.35)
    panel = blend(plain, base_rgb, 0.15)
    pixels = [(plain[0], plain[1], plain[2], 255)] * (size * size)

    # Hair strip (UV kUvHair).
    for y in range(0, 10):
        for x in range(35, 50):
            pixels[y * size + x] = (*hair, 255)

    # Front panel (+Z forward).
    x0, x1 = 18, 46
    y0, y1 = 14, 50
    for y in range(y0, y1):
        for x in range(x0, x1):
            pixels[y * size + x] = (*panel, 255)

    dark = (25, 25, 30)
    eye_y0, eye_y1 = 24, 32
    for y in range(eye_y0, eye_y1):
        for x in range(22, 30):
            pixels[y * size + x] = (*dark, 255)
        for x in range(36, 44):
            pixels[y * size + x] = (*dark, 255)
    for x in range(26, 40):
        pixels[40 * size + x] = (*dark, 255)
        pixels[41 * size + x] = (*dark, 255)

    write_rgba_png(path, pixels, size)


def write_skin_diffuse(path: Path, base_rgb: tuple[int, int, int], size: int = 64) -> None:
    write_body_png(path, base_rgb, size)


def main() -> None:
    for species_id, color in SPECIES_COLORS.items():
        tex_dir = ROOT / "models" / "creatures" / species_id / "textures"
        write_body_png(tex_dir / "body.png", color)
        write_leg_png(tex_dir / "leg.png", color)
        write_arm_png(tex_dir / "arm.png", color)
        write_face_png(tex_dir / "face.png", color)
        print(f"wrote {species_id}: body, leg, arm, face")

        default_png = tex_dir / "default.png"
        if default_png.exists():
            default_png.unlink()

    for rel, color in SKIN_DIFFUSE_COLORS:
        write_skin_diffuse(ROOT / rel, color)
        print(f"wrote {rel}")


if __name__ == "__main__":
    main()
