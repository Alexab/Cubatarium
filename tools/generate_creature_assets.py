#!/usr/bin/env python3
"""Generate solid-color 64x64 PNGs for creature/skin ship set."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# (R, G, B) 0-255
ASSETS = [
    ("models/creatures/human/textures/default.png", (77, 179, 255)),
    ("models/creatures/scout/textures/body.png", (255, 140, 26)),
    ("models/creatures/brute/textures/body.png", (255, 51, 51)),
    ("models/creatures/drifter/textures/body.png", (51, 217, 89)),
    ("models/skins/human_adventurer/textures/diffuse.png", (242, 191, 51)),
    ("models/skins/human_guard/textures/diffuse.png", (140, 140, 153)),
    ("models/skins/scout_golden/textures/diffuse.png", (255, 230, 51)),
    ("models/skins/brute_rust/textures/diffuse.png", (179, 89, 38)),
    ("models/skins/drifter_ice/textures/diffuse.png", (128, 217, 255)),
]


def png_chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def write_rgb_png(path: Path, rgb: tuple[int, int, int], size: int = 64) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    row = b"\x00" + bytes(rgb) * size
    raw = row * size
    compressed = zlib.compress(raw, 9)

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", ihdr)
    png += png_chunk(b"IDAT", compressed)
    png += png_chunk(b"IEND", b"")
    path.write_bytes(png)


def main() -> None:
    for rel, color in ASSETS:
        write_rgb_png(ROOT / rel, color)
        print(f"wrote {rel}")


if __name__ == "__main__":
    main()
