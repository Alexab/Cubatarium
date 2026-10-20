#!/usr/bin/env python3
"""Export natural terrain heights from a saved world for biome diagnostics."""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))
from worldgen_metrics_lib import (  # noqa: E402
    CHUNK_SIZE,
    build_id_to_name_from_world,
    decode_chunk,
    non_terrain_block_names_for_world,
    spawn_center_from_meta,
)


def ignored_block_names(world_dir: Path) -> set[str]:
    # Surface CSVs describe terrain, not the topmost decoration. In particular,
    # lily pads sit one block above water and otherwise look like terrain spikes.
    return non_terrain_block_names_for_world(world_dir, REPO)


def extract_surface(
    world_dir: Path, radius: int, center_x: int, center_z: int
) -> tuple[dict[tuple[int, int], int], int, int]:
    meta_path = world_dir / "world_data.json"
    if not meta_path.is_file():
        raise ValueError(f"world metadata not found: {meta_path}")
    id_to_name = build_id_to_name_from_world(world_dir, REPO)
    ignored_names = ignored_block_names(world_dir)
    ignored_ids = {
        block_id for block_id, name in id_to_name.items() if name in ignored_names
    }

    min_x, max_x = center_x - radius, center_x + radius
    min_z, max_z = center_z - radius, center_z + radius
    min_cx, max_cx = min_x // CHUNK_SIZE, max_x // CHUNK_SIZE
    min_cz, max_cz = min_z // CHUNK_SIZE, max_z // CHUNK_SIZE
    chunks_seen = 0
    surface: dict[tuple[int, int], int] = {}
    chunk_dir = world_dir / "chunks"
    if not chunk_dir.is_dir():
        raise ValueError(f"chunk directory not found: {chunk_dir}")

    for path in chunk_dir.glob("*.cchunk"):
        try:
            cx_text, _cy_text, cz_text = path.stem.split("_")
            cx, cz = int(cx_text), int(cz_text)
        except ValueError:
            continue
        if not (min_cx <= cx <= max_cx and min_cz <= cz <= max_cz):
            continue
        chunks_seen += 1
        for (x, y, z), block_id in decode_chunk(path).items():
            if (
                not min_x <= x <= max_x
                or not min_z <= z <= max_z
                or block_id == 0
                or block_id in ignored_ids
            ):
                continue
            surface[(x, z)] = max(y, surface.get((x, z), y))

    expected = {
        (x, z)
        for z in range(min_z, max_z + 1)
        for x in range(min_x, max_x + 1)
    }
    missing = expected.difference(surface)
    return surface, chunks_seen, len(missing)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("world", type=Path, help="saved world directory")
    parser.add_argument("output", type=Path, help="output CSV path")
    parser.add_argument("--radius", type=int, default=48)
    parser.add_argument("--center-x", type=int)
    parser.add_argument("--center-z", type=int)
    args = parser.parse_args()
    if args.radius < 16 or args.radius > 4096:
        parser.error("--radius must be in [16,4096] to include biome blend margin")

    world = args.world.resolve()
    world_data = json.loads((world / "world_data.json").read_text(encoding="utf-8"))
    spawn_x, spawn_z = spawn_center_from_meta(world_data)
    center_x = spawn_x if args.center_x is None else args.center_x
    center_z = spawn_z if args.center_z is None else args.center_z
    surface, chunks_seen, missing_count = extract_surface(
        world, args.radius, center_x, center_z
    )
    expected_count = (2 * args.radius + 1) ** 2
    if missing_count:
        print(
            f"surface extraction incomplete: {missing_count}/{expected_count} "
            f"columns missing; {chunks_seen} intersecting chunks read",
            file=sys.stderr,
        )
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("x", "z", "surface_y"))
        for z in range(center_z - args.radius, center_z + args.radius + 1):
            for x in range(center_x - args.radius, center_x + args.radius + 1):
                writer.writerow((x, z, surface[(x, z)]))
    print(
        f"wrote {len(surface)} terrain columns from {chunks_seen} chunks to "
        f"{args.output}; center=({center_x},{center_z})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
