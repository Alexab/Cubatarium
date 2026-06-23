#!/usr/bin/env python3
"""Generate a small PNG biome map for image_demo worldgen pack."""

from __future__ import annotations

import struct
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "content" / "worldgen_packs" / "image_demo" / "biome_map.png"

# RGB colors must match BiomeFromMapColor in WorldGenPack.cpp
COLORS = {
    "plains": (0x7E, 0xC8, 0x50),
    "forest": (0x2D, 0x8A, 0x3F),
    "desert": (0xE8, 0xD4, 0x6A),
    "hills": (0x7A, 0x7A, 0x7A),
    "tundra": (0xD8, 0xE8, 0xF0),
}

LAYOUT = [
    "tttttppppp",
    "ttttpppppf",
    "tttppppfff",
    "hhppppfffd",
    "hhpppfffdd",
    "hhppffdddd",
    "hhhffddddd",
    "hhhffddddd",
]


def write_png(path: Path, width: int, height: int, rgb_rows: list[bytes]) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    raw = b"".join(b"\x00" + row for row in rgb_rows)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def main() -> int:
    height = len(LAYOUT)
    width = len(LAYOUT[0])
    key = {"p": "plains", "f": "forest", "d": "desert", "h": "hills", "t": "tundra"}
    rows: list[bytes] = []
    for row in LAYOUT:
        pixels = bytearray()
        for ch in row:
            pixels.extend(COLORS[key[ch]])
        rows.append(bytes(pixels))
    write_png(OUT, width, height, rows)
    print(f"Wrote {OUT} ({width}x{height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
