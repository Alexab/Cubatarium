#!/usr/bin/env python3
"""Fast terrain shape metrics for a local chunk window."""

from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from worldgen_metrics_lib import (
    analyze_world,
    build_id_to_name_from_world,
    count_spawn_vegetation_blocks,
    decode_chunk,
    terrain_shape_stats,
)

REPO = Path(__file__).resolve().parents[1]


def load_columns(world_dir: Path, center_x: int, center_z: int, radius: int):
    chunk_dir = world_dir / "chunks"
    columns: dict[tuple[int, int, int], int] = {}
    for path in chunk_dir.glob("*.cchunk"):
        for (wx, wy, wz), bid in decode_chunk(path).items():
            if abs(wx - center_x) <= radius and abs(wz - center_z) <= radius:
                columns[(wx, wy, wz)] = bid
    return columns


def build_surface(
    columns: dict[tuple[int, int, int], int], *, land_only: bool
) -> dict[tuple[int, int], tuple[int, int]]:
    by_xz: dict[tuple[int, int], list[tuple[int, int]]] = {}
    for (wx, wy, wz), bid in columns.items():
        by_xz.setdefault((wx, wz), []).append((wy, bid))

    water_ids: set[int] = set()
    if land_only:
        sea_counts: Counter[int] = Counter()
        for (wx, wy, wz), bid in columns.items():
            if wy == 48:
                sea_counts[bid] += 1
        if sea_counts:
            water_ids.add(sea_counts.most_common(1)[0][0])

    surface: dict[tuple[int, int], tuple[int, int]] = {}
    for xz, entries in by_xz.items():
        if land_only:
            land_entries = [(wy, bid) for wy, bid in entries if bid not in water_ids]
            if not land_entries:
                continue
            wy, bid = max(land_entries, key=lambda t: t[0])
        else:
            wy, bid = max(entries, key=lambda t: t[0])
        surface[xz] = (wy, bid)
    return surface


def main() -> int:
    if len(sys.argv) < 2:
        print(
            "usage: quick_terrain_metrics.py WORLD_NAME [radius=32] "
            "[center_x=0] [center_z=0] [--land] [--count-objects]"
        )
        return 1
    land_only = "--land" in sys.argv
    count_objects = "--count-objects" in sys.argv
    args = [a for a in sys.argv[1:] if a not in ("--land", "--count-objects")]
    world = REPO / "bin" / "worlds" / args[0]
    radius = int(args[1]) if len(args) > 1 else 32
    center_x = int(args[2]) if len(args) > 2 else 0
    center_z = int(args[3]) if len(args) > 3 else 0

    columns = load_columns(world, center_x, center_z, radius)
    surface = build_surface(columns, land_only=land_only)
    if not surface:
        print(f"no surface data for {world} center=({center_x},{center_z})")
        return 1

    ys = [y for y, _ in surface.values()]
    shape = terrain_shape_stats(surface)
    mode = " land" if land_only else ""
    print(
        f"world={args[0]} center=({center_x},{center_z}) "
        f"radius={radius} columns={len(surface)}{mode}"
    )
    print(f"  height min/mean/max: {min(ys)} {sum(ys)/len(ys):.1f} {max(ys)}")
    print(f"  unique_y: {len(set(ys))}")
    print(f"  flatness_pct: {shape['flatness_pct']:.1f}")
    print(f"  rolling_hill_pct: {shape['rolling_hill_pct']:.1f}")
    print(f"  plateau_edge_pct: {shape['plateau_edge_pct']:.1f}")

    if count_objects:
        refs = REPO / "content" / "worldgen_refs.json"
        id_to_name = build_id_to_name_from_world(world, REPO)
        veg = count_spawn_vegetation_blocks(world, id_to_name, radius)
        metrics = analyze_world(
            world,
            refs,
            repo_root=REPO,
            spawn_radius=radius,
            max_height=128,
        )
        print("  placement (blocks in world files, not render):")
        print(f"    spawn_tree_blocks: {metrics.get('spawn_tree_blocks', 0)}")
        print(f"    spawn_bush_common_footprints: {metrics.get('spawn_bush_common_footprints', 0)}")
        print(f"    spawn_ground_logs: {metrics.get('spawn_ground_logs', 0)}")
        print(f"    spawn_fire_blocks: {metrics.get('spawn_fire_blocks', 0)}")
        if veg:
            print(f"    vegetation_breakdown: {veg}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
