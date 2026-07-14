#!/usr/bin/env python3
"""Deep column profile analysis for world chunks."""

from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path

from analyze_world_chunks import CHUNK_SIZE, decode_chunk, load_world_columns

REPO = Path(__file__).resolve().parents[1]


def load_all_blocks(world_dir: Path) -> dict[tuple[int, int, int], int]:
    columns: dict[tuple[int, int, int], int] = {}
    chunk_dir = world_dir / "chunks"
    for path in chunk_dir.glob("*.cchunk"):
        columns.update(decode_chunk(path))
    return columns


def column_profile(blocks: dict, wx: int, wz: int, max_y: int = 140) -> list[tuple[int, int]]:
    prof: list[tuple[int, int]] = []
    for y in range(0, max_y + 1):
        bid = blocks.get((wx, y, wz))
        if bid is not None:
            prof.append((y, bid))
    return prof


def surface_y(blocks: dict, wx: int, wz: int) -> int | None:
    ys = [y for (x, y, z), _ in blocks.items() if x == wx and z == wz]
    return max(ys) if ys else None


def main() -> int:
    world = REPO / "bin" / "worlds" / "World_007"
    blocks = load_all_blocks(world)
    surface = load_world_columns(world)

    # Find worst cliffs
    cliffs: list[tuple[int, int, int, int]] = []
    for (x, z), (y, _) in surface.items():
        for dx, dz in ((1, 0), (0, 1)):
            n = surface.get((x + dx, z + dz))
            if not n:
                continue
            d = abs(y - n[0])
            if d >= 8:
                cliffs.append((d, x, z, y))
    cliffs.sort(reverse=True)
    print("=== Top 10 cliffs in World_007 ===")
    for d, x, z, y in cliffs[:10]:
        print(f"  delta={d} at ({x},{z}) surfaceY={y}")

    # Spawn area surface composition
    print("\n=== Spawn 0,0 profile ===")
    prof = column_profile(blocks, 0, 0)
    print(f"  blocks up to y={prof[-1][0] if prof else 'none'}")
    if prof:
        top5 = prof[-8:]
        print(f"  top layers: {top5}")

    # Check for chunk-aligned height steps
    step16 = 0
    total = 0
    for (x, z), (y, _) in surface.items():
        for dx, dz in ((1, 0), (0, 1)):
            n = surface.get((x + dx, z + dz))
            if not n:
                continue
            total += 1
            d = abs(y - n[0])
            if d >= 15 and d <= 17:
                step16 += 1
    print(f"\n=== Height steps 15-17 blocks: {step16}/{total} ({100*step16/max(1,total):.2f}%) ===")

    # Stone plateau detector: long runs of same subsurface with grass/stone top
    stone_like = Counter()
    grass_like = Counter()
    for (x, z), (sy, top_bid) in surface.items():
        prof = column_profile(blocks, x, z, sy)
        if len(prof) < 3:
            continue
        filler = prof[-2][1] if prof[-1][0] == sy else prof[-1][1]
        stone_like[filler] += 1
        grass_like[top_bid] += 1
    print("\n=== Subsurface block below top (spawn |x|,|z|<=48) ===")
    sub = Counter()
    top = Counter()
    for (x, z), (sy, top_bid) in surface.items():
        if abs(x) > 48 or abs(z) > 48:
            continue
        prof = [(y, b) for y, b in column_profile(blocks, x, z, sy) if y <= sy]
        if len(prof) < 2:
            continue
        sub[prof[-2][1]] += 1
        top[top_bid] += 1
    print(f"  subsurface: {sub.most_common(6)}")
    print(f"  surface: {top.most_common(6)}")

    # Compare chunk file counts and if 006==007 byte identical at spawn chunk
    for name in ("World_006", "World_007"):
        p = REPO / "bin" / "worlds" / name / "chunks" / "0_0_0.cchunk"
        print(f"\n{name} 0_0_0.cchunk size={p.stat().st_size if p.exists() else 'missing'}")

    p6 = (REPO / "bin/worlds/World_006/chunks/0_0_0.cchunk").read_bytes()
    p7 = (REPO / "bin/worlds/World_007/chunks/0_0_0.cchunk").read_bytes()
    print(f"World_006 vs World_007 chunk 0_0_0 identical: {p6 == p7}")

    # World creation times
    for name in ("World_005", "World_006", "World_007"):
        wd = REPO / "bin" / "worlds" / name / "world_data.json"
        mtime = wd.stat().st_mtime
        meta = json.loads(wd.read_text(encoding="utf-8"))
        print(f"{name}: seed={meta.get('world_seed')} mtime={mtime}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
