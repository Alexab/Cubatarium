#!/usr/bin/env python3
"""Estimate saved-world flora density by sampled surface biome."""

from __future__ import annotations

import argparse
import csv
import json
import math
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))
from worldgen_metrics_lib import (  # noqa: E402
    SHRUB_BLOCK_NAMES,
    TREE_BLOCK_NAMES,
    build_id_to_name_from_world,
    spawn_center_from_meta,
)

CCHUNK_MAGIC = b"CCHK"
CHUNK_SIZE = 16
CHUNK_VOLUME = CHUNK_SIZE**3


def read_biome_grid(path: Path, expected_seed: int) -> tuple[dict, int, int]:
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        raise ValueError(f"biome grid is empty: {path}")

    grid: dict[tuple[int, int], dict] = {}
    xs: set[int] = set()
    zs: set[int] = set()
    for row in rows:
        seed = int(row["seed"])
        if seed != expected_seed:
            raise ValueError(
                f"biome grid seed {seed} does not match world seed {expected_seed}"
            )
        x, z = int(row["x"]), int(row["z"])
        weights = {}
        for item in row.get("biome_weights", "").split("|"):
            if not item:
                continue
            key, value = item.rsplit(":", 1)
            weights[key] = float(value)
        grid[(x, z)] = {
            "x": x,
            "z": z,
            "surface_y": int(row["surface_y"]),
            "dominant_biome": row["dominant_biome"],
            "dominant_weight": weights.get(row["dominant_biome"], 0.0),
            "weights": weights,
        }
        xs.add(x)
        zs.add(z)

    x_values, z_values = sorted(xs), sorted(zs)
    if len(x_values) < 2 or len(z_values) < 2:
        raise ValueError("biome grid must contain at least a 2x2 sample grid")
    x_steps = {b - a for a, b in zip(x_values, x_values[1:])}
    z_steps = {b - a for a, b in zip(z_values, z_values[1:])}
    if len(x_steps) != 1 or len(z_steps) != 1 or x_steps != z_steps:
        raise ValueError("biome grid must use a regular square sample spacing")
    step = x_steps.pop()
    if step <= 0:
        raise ValueError("biome grid sample spacing must be positive")
    return grid, step, x_values[0]


def nearest_sample(
    grid: dict[tuple[int, int], dict],
    x_values: list[int],
    z_values: list[int],
    step: int,
    x: int,
    z: int,
) -> dict | None:
    half_step = step / 2
    if (
        x < x_values[0] - half_step
        or x > x_values[-1] + half_step
        or z < z_values[0] - half_step
        or z > z_values[-1] + half_step
    ):
        return None
    ix = math.floor((x - x_values[0] + step / 2) / step)
    iz = math.floor((z - z_values[0] + step / 2) / step)
    ix = max(0, min(len(x_values) - 1, ix))
    iz = max(0, min(len(z_values) - 1, iz))
    return grid.get((x_values[ix], z_values[iz]))


def plant_block_names(world_data: dict, repo_root: Path) -> set[str]:
    names = set(TREE_BLOCK_NAMES) | set(SHRUB_BLOCK_NAMES)
    pack_config = world_data.get("resource_packs", {})
    definitions: dict[str, dict] = {}
    for tier in ("primary", "secondary"):
        pack_ids = pack_config.get(tier, [])
        if not isinstance(pack_ids, list):
            continue
        for pack_id in pack_ids:
            blocks_dir = repo_root / "resource_packs" / str(pack_id) / "blocks"
            for block_path in blocks_dir.glob("*.json"):
                try:
                    definition = json.loads(
                        block_path.read_text(encoding="utf-8-sig")
                    )
                except (OSError, json.JSONDecodeError):
                    continue
                name = definition.get("name") or block_path.stem
                if isinstance(name, str):
                    definitions[name] = definition

    def is_flora(name: str) -> bool:
        definition = definitions.get(name, {})
        types_value = definition.get("types", [])
        types = set(types_value) if isinstance(types_value, list) else set()
        render = definition.get("render", {})
        render_style = render.get("style", "") if isinstance(render, dict) else ""
        return bool(
            types.intersection({"plants", "flora", "vegetation"})
            or render_style in {"cross", "crossed", "plant", "plantlike", "xsprite"}
        )

    object_sets = world_data.get("worldgen_sets", {}).get("objects", {})
    for pool in ("vegetation", "ground_cover"):
        entries = object_sets.get(pool, {}).get("default", {}).get("entries", [])
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            scatter_block = entry.get("block")
            if (
                pool == "ground_cover"
                and entry.get("mode") == "scatter_blocks"
                and isinstance(scatter_block, str)
                and scatter_block
            ):
                names.add(scatter_block)
            object_name = entry.get("object")
            if not isinstance(object_name, str) or not object_name:
                continue
            prefab_path = repo_root / "objects" / f"{object_name}.json"
            if not prefab_path.is_file():
                continue
            try:
                prefab = json.loads(prefab_path.read_text(encoding="utf-8-sig"))
            except (OSError, json.JSONDecodeError):
                continue
            tags = prefab.get("tags", [])
            if "plants" not in tags and "vegetation" not in tags:
                continue
            names.update(
                block["type"]
                for block in prefab.get("blocks", [])
                if isinstance(block, dict)
                and isinstance(block.get("type"), str)
                and block["type"]
                and is_flora(block["type"])
            )
    return names


def iter_feature_cells(path: Path, wanted_ids: set[int]):
    data = path.read_bytes()
    if len(data) < 20 or data[:4] != CCHUNK_MAGIC:
        raise ValueError(f"invalid chunk header: {path}")

    off = 17  # magic/version and three signed chunk coordinates
    palette_count = struct.unpack_from("<H", data, off)[0]
    off += 2
    palette = list(struct.unpack_from("<" + "H" * palette_count, data, off))
    off += 2 * palette_count
    run_count = struct.unpack_from("<I", data, off)[0]
    off += 4
    cx, cy, cz = map(int, path.stem.split("_"))
    filled = 0

    for _ in range(run_count):
        length, palette_index = struct.unpack_from("<HH", data, off)
        off += 4
        if palette_index >= len(palette):
            raise ValueError(f"invalid palette index in {path}")
        if length <= 0 or filled + length > CHUNK_VOLUME:
            raise ValueError(f"invalid run length in {path}")
        block_id = palette[palette_index]
        if block_id in wanted_ids:
            for index in range(filled, filled + length):
                yield (
                    cx * CHUNK_SIZE + index % CHUNK_SIZE,
                    cy * CHUNK_SIZE + (index // CHUNK_SIZE) % CHUNK_SIZE,
                    cz * CHUNK_SIZE + index // CHUNK_VOLUME,
                    block_id,
                )
        filled += length

    if filled != CHUNK_VOLUME:
        raise ValueError(f"incomplete chunk voxel data in {path}: {filled}")


def analyze(
    world_dir: Path,
    grid_path: Path,
    radius: int | None,
    center_x_override: int | None = None,
    center_z_override: int | None = None,
) -> dict:
    world_data_path = world_dir / "world_data.json"
    if not world_data_path.is_file():
        raise ValueError(f"world metadata not found: {world_data_path}")
    world_data = json.loads(world_data_path.read_text(encoding="utf-8"))
    world_seed = int(world_data["world_seed"])
    sea_level = int(world_data.get("procedural", {}).get("sea_level", 63))
    spawn_x, spawn_z = spawn_center_from_meta(world_data)
    center_x = spawn_x if center_x_override is None else center_x_override
    center_z = spawn_z if center_z_override is None else center_z_override
    grid, step, grid_origin = read_biome_grid(grid_path, world_seed)
    x_values = sorted({x for x, _ in grid})
    z_values = sorted({z for _, z in grid})

    id_to_name = build_id_to_name_from_world(world_dir, REPO)
    name_to_id = {name: block_id for block_id, name in id_to_name.items()}
    candidate_names = plant_block_names(world_data, REPO)
    wanted = {
        block_id: name
        for name in candidate_names
        if (block_id := name_to_id.get(name)) is not None
    }
    missing_names = sorted(candidate_names - set(name_to_id))
    tree_names = set(TREE_BLOCK_NAMES)
    shrub_names = set(SHRUB_BLOCK_NAMES)
    wanted_ids = set(wanted)

    def sample_at(x: int, z: int) -> dict | None:
        return nearest_sample(grid, x_values, z_values, step, x, z)

    chunks_dir = world_dir / "chunks"
    chunk_paths = sorted(chunks_dir.glob("*.cchunk"))
    if not chunk_paths:
        raise ValueError(f"no chunk files found: {chunks_dir}")

    xz_chunks = set()
    for path in chunk_paths:
        cx, _cy, cz = map(int, path.stem.split("_"))
        xz_chunks.add((cx, cz))

    land_columns: Counter[str] = Counter()
    confident_land_columns: Counter[str] = Counter()
    land_sample_points: Counter[str] = Counter()
    feature_blocks: dict[str, Counter[str]] = defaultdict(Counter)
    feature_columns: dict[str, dict[str, set[tuple[int, int]]]] = defaultdict(
        lambda: defaultdict(set)
    )
    outside_grid_columns = 0

    for x, z in grid:
        sample = grid[(x, z)]
        if sample["surface_y"] > sea_level:
            land_sample_points[sample["dominant_biome"]] += 1

    for cx, cz in xz_chunks:
        for lz in range(CHUNK_SIZE):
            for lx in range(CHUNK_SIZE):
                x, z = cx * CHUNK_SIZE + lx, cz * CHUNK_SIZE + lz
                if radius is not None and (
                    abs(x - center_x) > radius or abs(z - center_z) > radius
                ):
                    continue
                sample = sample_at(x, z)
                if sample is None:
                    outside_grid_columns += 1
                    continue
                if sample["surface_y"] <= sea_level:
                    continue
                biome = sample["dominant_biome"]
                land_columns[biome] += 1
                if sample["dominant_weight"] >= 0.65:
                    confident_land_columns[biome] += 1

    for path in chunk_paths:
        for x, _y, z, block_id in iter_feature_cells(path, wanted_ids):
            if radius is not None and (
                abs(x - center_x) > radius or abs(z - center_z) > radius
            ):
                continue
            sample = sample_at(x, z)
            if sample is None or sample["surface_y"] <= sea_level:
                continue
            name = wanted[block_id]
            category = (
                "tree"
                if name in tree_names
                else "shrub"
                if name in shrub_names
                else "ground_cover"
            )
            biome = sample["dominant_biome"]
            feature_blocks[biome][category] += 1
            feature_blocks[biome][f"block:{name}"] += 1
            feature_columns[biome][category].add((x, z))

    biome_reports = []
    for biome, area in sorted(land_columns.items(), key=lambda item: item[0]):
        blocks = feature_blocks[biome]
        cover = {
            category: len(feature_columns[biome][category])
            for category in ("tree", "shrub", "ground_cover")
        }
        biome_reports.append(
            {
                "biome": biome,
                "land_columns_approx": area,
                "confident_land_columns_approx": confident_land_columns[biome],
                "land_sample_points": land_sample_points[biome],
                "blocks": {
                    category: blocks[category]
                    for category in ("tree", "shrub", "ground_cover")
                },
                "columns_with_blocks": cover,
                "ground_cover_pct_land_approx": round(
                    100.0 * cover["ground_cover"] / max(1, area), 3
                ),
                "blocks_per_1000_land_columns_approx": {
                    category: round(1000.0 * blocks[category] / max(1, area), 2)
                    for category in ("tree", "shrub", "ground_cover")
                },
                "block_counts": dict(
                    sorted(
                        (
                            (key.removeprefix("block:"), value)
                            for key, value in blocks.items()
                            if key.startswith("block:")
                        ),
                        key=lambda item: (-item[1], item[0]),
                    )
                ),
            }
        )

    return {
        "schema_version": 1,
        "world_name": world_data.get("world_name", world_dir.name),
        "world_seed": world_seed,
        "analysis_center": {"x": center_x, "z": center_z},
        "worldgen_profile_version": world_data.get("procedural", {}).get(
            "worldgen_profile_version"
        ),
        "terrain_backend": world_data.get("procedural", {}).get(
            "terrain_backend"
        ),
        "worldgen_pack": world_data.get("procedural", {}).get("worldgen_pack_id"),
        "resource_packs": world_data.get("resource_packs", {}),
        "sea_level": sea_level,
        "biome_grid": str(grid_path),
        "biome_grid_step": step,
        "biome_grid_origin": grid_origin,
        "biome_assignment": (
            "exact coordinate sample of dominant biome"
            if step == 1
            else "nearest sampled dominant biome (approximate)"
        ),
        "land_area_estimate": (
            "saved terrain surface above sea level"
            if step == 1
            else "nearest sampled terrain surface above sea level"
        ),
        "chunk_files": len(chunk_paths),
        "mapped_plant_block_types": len(wanted),
        "unresolved_plant_block_names": missing_names,
        "outside_grid_columns": outside_grid_columns,
        "biomes": biome_reports,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("world_dir", type=Path)
    parser.add_argument("--biome-grid", type=Path, required=True)
    parser.add_argument("--radius", type=int, default=None)
    parser.add_argument("--center-x", type=int, default=None)
    parser.add_argument("--center-z", type=int, default=None)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()

    try:
        report = analyze(
            args.world_dir,
            args.biome_grid,
            args.radius,
            args.center_x,
            args.center_z,
        )
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
        print(f"analyze_worldgen_biome_flora: {exc}", file=sys.stderr)
        return 2

    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        sys.stdout.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
