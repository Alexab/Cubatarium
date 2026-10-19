#!/usr/bin/env python3
"""Regression checks for PNG alpha scans used by the resource-pack audit."""

from __future__ import annotations

import json
import struct
import sys
import tempfile
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

from audit_resource_packs import scan_png_alpha_profile


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + kind
        + data
        + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    )


def paeth(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    distances = (
        abs(estimate - left),
        abs(estimate - above),
        abs(estimate - upper_left),
    )
    return (left, above, upper_left)[distances.index(min(distances))]


def make_rgba_png(path: Path, alphas: list[list[int]]) -> None:
    width = 3
    bytes_per_pixel = 4
    scanlines = bytearray()
    previous = bytearray(width * bytes_per_pixel)
    for y, row_alphas in enumerate(alphas):
        source = bytearray()
        for x, alpha in enumerate(row_alphas):
            source.extend((31 + x * 37, 67 + y * 23, 109 + x * 11, alpha))

        filter_type = y  # Exercise None, Sub, Up, Average, and Paeth.
        filtered = bytearray()
        for index, value in enumerate(source):
            left = source[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
            above = previous[index]
            upper_left = (
                previous[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
            )
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            else:
                predictor = paeth(left, above, upper_left)
            filtered.append((value - predictor) & 0xFF)
        scanlines.append(filter_type)
        scanlines.extend(filtered)
        previous = source

    header = struct.pack(">IIBBBBB", width, len(alphas), 8, 6, 0, 0, 0)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(scanlines))
        + png_chunk(b"IEND", b"")
    )


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        transparent = tmp_path / "transparent.png"
        opaque = tmp_path / "opaque.png"
        alpha_rows = [[0, 128, 255], [0, 128, 255], [0, 128, 255], [0, 128, 255], [0, 128, 255]]
        make_rgba_png(transparent, alpha_rows)
        make_rgba_png(opaque, [[255, 255, 255]] * 5)
        if scan_png_alpha_profile(transparent) != (True, True):
            print("filtered PNG scan failed to detect transparent and semitransparent pixels")
            return 1
        if scan_png_alpha_profile(opaque) != (False, False):
            print("filtered PNG scan reported alpha where all pixels are opaque")
            return 1

    solid_worldgen_blocks = {
        "cubatarium_cc0_base": ("hellrock",),
        "cubatarium_geology_cc0": ("tuff", "moss_block"),
        "kenney_voxel_16": ("hellrock",),
        "kenney_voxel_128": ("hellrock",),
    }
    for pack_id, block_names in solid_worldgen_blocks.items():
        blocks_dir = REPO / "resource_packs" / pack_id / "blocks"
        textures_dir = REPO / "resource_packs" / pack_id / "textures" / "blocks"
        for block_name in block_names:
            block_path = blocks_dir / f"{block_name}.json"
            if not block_path.is_file():
                print(f"missing solid worldgen block definition: {pack_id}/{block_name}")
                return 1
            block = json.loads(block_path.read_text(encoding="utf-8-sig"))
            for stem in block.get("textures", [])[:6]:
                profile = scan_png_alpha_profile(textures_dir / f"{stem}.png")
                if profile is None or profile[0]:
                    print(
                        f"solid worldgen block has alpha holes or unsupported texture: "
                        f"{pack_id}/{block_name} -> {stem} ({profile})"
                    )
                    return 1
    print("resource pack alpha scan: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
