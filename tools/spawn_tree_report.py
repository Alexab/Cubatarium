#!/usr/bin/env python3
"""Report standing trees near spawn (fast: only chunks overlapping radius)."""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))
from worldgen_metrics_lib import CHUNK_SIZE, decode_chunk  # noqa: E402


def load_pack_id_map(pack_dir: Path) -> dict[int, str]:
    id_to_name: dict[int, str] = {}
    for path in (pack_dir / "blocks").glob("*.json"):
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
        except json.JSONDecodeError:
            continue
        if "id" in data:
            id_to_name[data["id"]] = data.get("name") or path.stem
    return id_to_name


def chunk_coords_in_radius(radius: int) -> set[tuple[int, int, int]]:
    out: set[tuple[int, int, int]] = set()
    for wx in range(-radius, radius + 1):
        for wz in range(-radius, radius + 1):
            if wx * wx + wz * wz > radius * radius:
                continue
            cx = math.floor(wx / CHUNK_SIZE)
            cy = 0
            cz = math.floor(wz / CHUNK_SIZE)
            if wx < 0 and wx % CHUNK_SIZE == 0:
                cx -= 1
            if wz < 0 and wz % CHUNK_SIZE == 0:
                cz -= 1
            for cy in range(-1, 9):
                out.add((cx, cy, cz))
    return out


def surface_y(columns: dict, x: int, z: int) -> int | None:
    ys = [y for (cx, y, cz), bid in columns.items() if cx == x and cz == z and bid != 0]
    if not ys:
        return None
    for y in sorted(ys, reverse=True):
        if columns.get((x, y + 1, z), 0) == 0:
            return y
    return max(ys)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("world_dir", type=Path)
    parser.add_argument("--radius", type=int, default=48)
    parser.add_argument("-o", "--output", type=Path)
    args = parser.parse_args()

    pack = REPO / "bin" / "resource_packs" / "minetest_default_16"
    id_to_name = load_pack_id_map(pack)
    name_to_id = {v: k for k, v in id_to_name.items()}

    spawn = (0.0, 53.0, 0.0)
    wd = args.world_dir / "world_data.json"
    if wd.is_file():
        data = json.loads(wd.read_text(encoding="utf-8"))
        if sp := data.get("spawn_point"):
            spawn = tuple(sp)

    needed = chunk_coords_in_radius(args.radius)
    columns: dict[tuple[int, int, int], int] = {}
    chunk_dir = args.world_dir / "chunks"
    for path in chunk_dir.glob("*.cchunk"):
        parts = path.stem.split("_")
        if len(parts) != 3:
            continue
        coord = tuple(int(p) for p in parts)
        if coord not in needed:
            continue
        columns.update(decode_chunk(path))

    log_by_col: dict[tuple[int, int], list[int]] = defaultdict(list)
    leaves = 0
    bark = 0
    bark_id = name_to_id.get("tree_bark", 0)
    for (x, y, z), bid in columns.items():
        if abs(x) > args.radius or abs(z) > args.radius:
            continue
        name = id_to_name.get(bid, "")
        if name == "tree_log":
            log_by_col[(x, z)].append(y)
        elif name == "tree_leaves":
            leaves += 1
        elif bark_id and bid == bark_id:
            bark += 1

    trees: list[tuple[float, int, int, int | None, int, int]] = []
    for (x, z), ys in log_by_col.items():
        ys = sorted(ys)
        if len(ys) >= 2 and ys[-1] - ys[0] >= 2:
            dist = math.hypot(x, z)
            sy = surface_y(columns, x, z)
            trees.append((dist, x, z, sy, ys[0], ys[-1]))
    trees.sort()

    lines = [
        f"World: {args.world_dir.name}",
        f"Spawn: {spawn}",
        f"Radius: {args.radius} blocks",
        f"tree_leaves voxels: {leaves}",
        f"tree_bark voxels: {bark}",
        f"Standing trees (multi-block trunk): {len(trees)}",
        "",
        "Nearest trees (dist, x, z, surface_y, log_y_min..log_y_max):",
    ]
    for row in trees[:20]:
        dist, x, z, sy, y0, y1 = row
        lines.append(
            f"  dist={dist:5.1f}  ({x:4d}, {z:4d})  surface={sy}  trunk y={y0}..{y1}"
        )

    text = "\n".join(lines) + "\n"
    print(text)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
