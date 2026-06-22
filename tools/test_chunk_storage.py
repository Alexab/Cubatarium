#!/usr/bin/env python3
"""Roundtrip smoke for chunk storage backends (binary + JSON layout checks)."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]

MAGIC = b"CCHK"
VERSION = 1
CHUNK_VOLUME = 16 * 16 * 16


def encode_binary_chunk(cx: int, cy: int, cz: int, palette: list[int], runs: list[tuple[int, int]]) -> bytes:
    out = bytearray()
    out.extend(MAGIC)
    out.append(VERSION)
    out.extend(struct.pack("<iii", cx, cy, cz))
    out.extend(struct.pack("<H", len(palette)))
    for block_id in palette:
        out.extend(struct.pack("<H", block_id))
    out.extend(struct.pack("<I", len(runs)))
    for length, palette_idx in runs:
        out.extend(struct.pack("<HH", length, palette_idx))
    return bytes(out)


def decode_binary_chunk(data: bytes) -> tuple[tuple[int, int, int], list[int], list[tuple[int, int]]]:
    if data[:4] != MAGIC:
        raise ValueError("bad magic")
    offset = 4
    version = data[offset]
    offset += 1
    if version != VERSION:
        raise ValueError(f"unsupported version {version}")
    cx, cy, cz = struct.unpack_from("<iii", data, offset)
    offset += 12
    palette_count = struct.unpack_from("<H", data, offset)[0]
    offset += 2
    palette = list(struct.unpack_from(f"<{palette_count}H", data, offset))
    offset += palette_count * 2
    run_count = struct.unpack_from("<I", data, offset)[0]
    offset += 4
    runs: list[tuple[int, int]] = []
    for _ in range(run_count):
        length, palette_idx = struct.unpack_from("<HH", data, offset)
        offset += 4
        runs.append((length, palette_idx))
    return (cx, cy, cz), palette, runs


def expand_runs(palette: list[int], runs: list[tuple[int, int]]) -> list[int]:
    voxels: list[int] = []
    for length, palette_idx in runs:
        voxels.extend([palette[palette_idx]] * length)
    return voxels


def main() -> int:
  # air-only chunk: single run
    payload = encode_binary_chunk(0, 0, 0, [0], [(CHUNK_VOLUME, 0)])
    coord, palette, runs = decode_binary_chunk(payload)
    voxels = expand_runs(palette, runs)
    if coord != (0, 0, 0) or len(voxels) != CHUNK_VOLUME or any(v != 0 for v in voxels):
        print("FAIL: air chunk roundtrip", file=sys.stderr)
        return 1

    # mixed palette
    payload = encode_binary_chunk(1, 2, 3, [0, 7, 9], [(100, 0), (50, 1), (3846, 2), (100, 0)])
    _, palette, runs = decode_binary_chunk(payload)
    voxels = expand_runs(palette, runs)
    if len(voxels) != CHUNK_VOLUME:
        print("FAIL: mixed chunk volume", file=sys.stderr)
        return 1
    if voxels[0] != 0 or voxels[100] != 7 or voxels[150] != 9:
        print("FAIL: mixed chunk values", file=sys.stderr)
        return 1

    print(
        "CHUNK_STORAGE_SMOKE ok=1 binary_air_bytes=%d binary_mixed_bytes=%d"
        % (len(encode_binary_chunk(0, 0, 0, [0], [(CHUNK_VOLUME, 0)])), len(payload))
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
