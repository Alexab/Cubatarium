#!/usr/bin/env python3
"""Summarize connected exposed water in saved worlds; no flow routing is inferred."""

from __future__ import annotations

import argparse
import json
import struct
from collections import Counter, deque
from pathlib import Path
from statistics import mean, median

from worldgen_metrics_lib import (
    MAGIC,
    VEGETATION_BLOCK_NAMES,
    build_id_to_name_from_world,
    ground_cover_block_names_for_world,
)

CHUNK_SIZE = 16
CHUNK_VOLUME = CHUNK_SIZE**3
FLUID_NAMES = {"water", "mtg_water_flowing", "mtg_river_water_flowing"}
SURFACE_WATER_PLANTS = {"lily_pad"}


def read_chunk_header(data: bytes) -> tuple[int, int, int, list[int], int, int]:
    if len(data) < 20 or data[:4] != MAGIC:
        raise ValueError("invalid chunk header")
    off = 5  # magic and format byte
    cx, cy, cz = struct.unpack_from("<iii", data, off)
    off += 12
    palette_count = struct.unpack_from("<H", data, off)[0]
    off += 2
    palette = list(struct.unpack_from(f"<{palette_count}H", data, off))
    off += 2 * palette_count
    run_count = struct.unpack_from("<I", data, off)[0]
    off += 4
    return cx, cy, cz, palette, run_count, off


def scan_chunk(
    path: Path,
    *,
    fluid_ids: set[int],
    ignored_terrain_ids: set[int],
    top: dict[tuple[int, int], tuple[int, int]],
    terrain: dict[tuple[int, int], int],
    fluid_top: dict[tuple[int, int], tuple[int, int]],
) -> None:
    data = path.read_bytes()
    cx, cy, cz, palette, run_count, off = read_chunk_header(data)
    filled = 0
    for _ in range(run_count):
        if off + 4 > len(data):
            raise ValueError(f"truncated RLE stream: {path}")
        length, palette_index = struct.unpack_from("<HH", data, off)
        off += 4
        if length == 0 or palette_index >= len(palette):
            raise ValueError(f"invalid RLE entry: {path}")
        block_id = palette[palette_index]
        end = filled + length
        if end > CHUNK_VOLUME:
            raise ValueError(f"RLE exceeds chunk volume: {path}")

        if block_id != 0:
            cursor = filled
            while cursor < end:
                lx = cursor % CHUNK_SIZE
                ly = (cursor // CHUNK_SIZE) % CHUNK_SIZE
                lz = cursor // (CHUNK_SIZE * CHUNK_SIZE)
                row_count = min(end - cursor, CHUNK_SIZE - lx)
                world_y = cy * CHUNK_SIZE + ly
                world_z = cz * CHUNK_SIZE + lz
                for dx in range(row_count):
                    key = (cx * CHUNK_SIZE + lx + dx, world_z)
                    previous = top.get(key)
                    if previous is None or world_y > previous[0]:
                        top[key] = (world_y, block_id)
                    if block_id not in ignored_terrain_ids:
                        terrain[key] = max(world_y, terrain.get(key, world_y))
                    if block_id in fluid_ids:
                        previous_fluid = fluid_top.get(key)
                        if previous_fluid is None or world_y > previous_fluid[0]:
                            fluid_top[key] = (world_y, block_id)
                cursor += row_count
        filled = end

    if filled != CHUNK_VOLUME:
        raise ValueError(f"incomplete RLE chunk: {path} ({filled}/{CHUNK_VOLUME})")


def connected_water_bodies(
    surface_water: dict[tuple[int, int], tuple[int, int]],
    terrain: dict[tuple[int, int], int],
    id_to_name: dict[int, str],
    *,
    sea_level: int,
    bounds: tuple[int, int, int, int],
) -> list[dict]:
    remaining = set(surface_water)
    min_x, max_x, min_z, max_z = bounds
    bodies: list[dict] = []
    while remaining:
        start = remaining.pop()
        queue = deque([start])
        cells = [start]
        while queue:
            x, z = queue.popleft()
            for neighbor in ((x - 1, z), (x + 1, z), (x, z - 1), (x, z + 1)):
                if neighbor in remaining:
                    remaining.remove(neighbor)
                    queue.append(neighbor)
                    cells.append(neighbor)

        water_heights = [surface_water[p][0] for p in cells]
        surface_levels = Counter(water_heights)
        dominant_level, dominant_level_columns = surface_levels.most_common(1)[0]
        dominant_level_band_columns = sum(
            count
            for level, count in surface_levels.items()
            if abs(level - dominant_level) <= 1
        )
        depths = [
            surface_water[p][0] - terrain[p]
            for p in cells
            if p in terrain and terrain[p] < surface_water[p][0]
        ]
        fluid_counts = Counter(id_to_name.get(surface_water[p][1], "unknown") for p in cells)
        body_min_x = min(x for x, _ in cells)
        body_max_x = max(x for x, _ in cells)
        body_min_z = min(z for _, z in cells)
        body_max_z = max(z for _, z in cells)
        touches_boundary = (
            body_min_x <= min_x
            or body_max_x >= max_x
            or body_min_z <= min_z
            or body_max_z >= max_z
        )
        contains_river_block = any("river_water" in name for name in fluid_counts)
        median_surface = median(water_heights)
        if touches_boundary:
            classification = "boundary_connected_water"
        elif contains_river_block:
            classification = "river_coded_water"
        elif median_surface > sea_level:
            classification = "enclosed_above_sea_water"
        else:
            classification = "sea_level_water"
        bodies.append(
            {
                "columns": len(cells),
                "bounds_xz": [body_min_x, body_max_x, body_min_z, body_max_z],
                "surface_y_min": min(water_heights),
                "surface_y_median": median(water_heights),
                "surface_y_max": max(water_heights),
                "surface_columns_by_y": dict(sorted(surface_levels.items())),
                "dominant_surface_y": dominant_level,
                "dominant_surface_level_share": round(
                    dominant_level_columns / len(cells), 4
                ),
                "dominant_surface_level_band_columns": dominant_level_band_columns,
                "depth_median": median(depths) if depths else None,
                "depth_max": max(depths) if depths else None,
                "touches_generated_boundary": touches_boundary,
                "contains_river_block": contains_river_block,
                "flat_lake_candidate": (
                    not touches_boundary
                    and not contains_river_block
                    and dominant_level > sea_level
                    and dominant_level_band_columns >= 64
                    and dominant_level_band_columns / len(cells) >= 0.55
                ),
                "classification": classification,
                "fluid_blocks_by_name": dict(sorted(fluid_counts.items())),
            }
        )
    return sorted(bodies, key=lambda body: (-body["columns"], body["bounds_xz"]))


def analyze_world(world_dir: Path, repo_root: Path) -> dict:
    meta_path = world_dir / "world_data.json"
    if not meta_path.is_file():
        raise FileNotFoundError(f"missing world metadata: {meta_path}")
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    procedural = meta.get("procedural", {})
    sea_level = int(procedural.get("sea_level", meta.get("sea_level", 63)))
    id_to_name = build_id_to_name_from_world(world_dir, repo_root)
    fluid_ids = {bid for bid, name in id_to_name.items() if name in FLUID_NAMES}
    if not fluid_ids:
        raise ValueError(f"no recognized water blocks in {world_dir}")
    ignored_names = (
        {"water", "mtg_water_flowing", "mtg_river_water_flowing", "lava", "fire"}
        | set(VEGETATION_BLOCK_NAMES)
        | SURFACE_WATER_PLANTS
        | ground_cover_block_names_for_world(world_dir, repo_root)
    )
    ignored_terrain_ids = {
        bid for bid, name in id_to_name.items() if name in ignored_names
    }
    top: dict[tuple[int, int], tuple[int, int]] = {}
    terrain: dict[tuple[int, int], int] = {}
    fluid_top: dict[tuple[int, int], tuple[int, int]] = {}
    chunk_dir = world_dir / "chunks"
    chunk_files = sorted(chunk_dir.glob("*.cchunk"))
    for path in chunk_files:
        scan_chunk(
            path,
            fluid_ids=fluid_ids,
            ignored_terrain_ids=ignored_terrain_ids,
            top=top,
            terrain=terrain,
            fluid_top=fluid_top,
        )
    if not top:
        raise ValueError(f"no blocks found in {world_dir}")

    bounds = (
        min(x for x, _ in top),
        max(x for x, _ in top),
        min(z for _, z in top),
        max(z for _, z in top),
    )
    water_surface_columns: dict[tuple[int, int], tuple[int, int]] = {}
    water_columns_under_surface_plant = 0
    for pos, fluid in fluid_top.items():
        top_y, top_id = top.get(pos, (0, 0))
        if top_id == fluid[1] and top_y == fluid[0]:
            water_surface_columns[pos] = fluid
        elif (
            top_y == fluid[0] + 1
            and id_to_name.get(top_id, "") in SURFACE_WATER_PLANTS
        ):
            water_surface_columns[pos] = fluid
            water_columns_under_surface_plant += 1
    water_surface_columns = {
        pos: fluid
        for pos, fluid in water_surface_columns.items()
        if fluid[0] > terrain.get(pos, fluid[0] - 1)
    }
    bodies = connected_water_bodies(
        water_surface_columns,
        terrain,
        id_to_name,
        sea_level=sea_level,
        bounds=bounds,
    )
    enclosed = [
        body for body in bodies if body["classification"] == "enclosed_above_sea_water"
    ]
    lake_candidates = [body for body in bodies if body["flat_lake_candidate"]]
    rivers = [body for body in bodies if body["contains_river_block"]]
    depths = [
        body["depth_median"]
        for body in lake_candidates
        if body["depth_median"] is not None
    ]
    return {
        "world": world_dir.name,
        "seed": meta.get("world_seed"),
        "worldgen_profile_version": meta.get("worldgen_profile_version"),
        "terrain_backend": procedural.get("terrain_backend", meta.get("terrain_backend")),
        "sea_level": sea_level,
        "chunk_files": len(chunk_files),
        "generated_bounds_xz": list(bounds),
        "surface_water_columns": len(water_surface_columns),
        "water_columns_under_surface_plant": water_columns_under_surface_plant,
        "water_body_count": len(bodies),
        "enclosed_above_sea_water_body_count": len(enclosed),
        "flat_lake_candidate_count": len(lake_candidates),
        "river_coded_body_count": len(rivers),
        "flat_lake_candidate_depth_median_mean": mean(depths) if depths else None,
        "water_bodies": bodies,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--world",
        action="append",
        required=True,
        help="world directory (repeat this option for a multi-seed sample)",
    )
    parser.add_argument(
        "--world-root",
        type=Path,
        default=Path("bin/worlds"),
        help="base directory for names passed to --world",
    )
    parser.add_argument("--output", type=Path, help="optional JSON report path")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    world_root = args.world_root if args.world_root.is_absolute() else root / args.world_root
    reports = [
        analyze_world(world_root / name, root)
        for name in args.world
    ]
    payload = {"worlds": reports}
    encoded = json.dumps(payload, indent=2, ensure_ascii=False)
    if args.output:
        output = args.output if args.output.is_absolute() else root / args.output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(encoded + "\n", encoding="utf-8")
        print(output)
    else:
        print(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
