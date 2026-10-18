#!/usr/bin/env python3
"""Shared metrics for procedural worldgen (.cchunk analysis)."""

from __future__ import annotations

import json
import struct
from collections import Counter, defaultdict
from pathlib import Path

CHUNK_SIZE = 16
CHUNK_VOLUME = CHUNK_SIZE**3
MAGIC = b"CCHK"


def read_u16(data: bytes, off: int) -> tuple[int, int]:
    return struct.unpack_from("<H", data, off)[0], off + 2


def read_u32(data: bytes, off: int) -> tuple[int, int]:
    return struct.unpack_from("<I", data, off)[0], off + 4


def read_i32(data: bytes, off: int) -> tuple[int, int]:
    return struct.unpack_from("<i", data, off)[0], off + 4


def decode_chunk(path: Path) -> dict[tuple[int, int, int], int]:
    data = path.read_bytes()
    if len(data) < 20 or data[:4] != MAGIC:
        return {}
    off = 5
    _, off = read_i32(data, off)
    _, off = read_i32(data, off)
    _, off = read_i32(data, off)
    palette_count, off = read_u16(data, off)
    palette: list[int] = []
    for _ in range(palette_count):
        bid, off = read_u16(data, off)
        palette.append(bid)
    run_count, off = read_u32(data, off)
    blocks: dict[tuple[int, int, int], int] = {}
    lx = ly = lz = 0

    def advance() -> None:
        nonlocal lx, ly, lz
        lx += 1
        if lx >= CHUNK_SIZE:
            lx = 0
            ly += 1
            if ly >= CHUNK_SIZE:
                ly = 0
                lz += 1

    stem = path.stem
    cx_s, cy_s, cz_s = stem.split("_")
    cx, cy, cz = int(cx_s), int(cy_s), int(cz_s)
    filled = 0
    for _ in range(run_count):
        length, off = read_u16(data, off)
        palette_idx, off = read_u16(data, off)
        if palette_idx >= len(palette):
            return {}
        bid = palette[palette_idx]
        for _ in range(length):
            if filled >= CHUNK_VOLUME or lz >= CHUNK_SIZE:
                return {}
            if bid != 0:
                wx = cx * CHUNK_SIZE + lx
                wy = cy * CHUNK_SIZE + ly
                wz = cz * CHUNK_SIZE + lz
                blocks[(wx, wy, wz)] = bid
            advance()
            filled += 1
    return blocks


TREE_LOG_BLOCK_NAMES = (
    "tree_log",
    "mtg_tree",
    "mtg_pine_tree",
    "mtg_aspen_tree",
    "mtg_acacia_tree",
    "mtg_jungletree",
)
TREE_LEAF_BLOCK_NAMES = (
    "tree_leaves",
    "mtg_leaves",
    "leaves_spruce",
    "leaves_spruce_light",
    "leaves_jungle",
    "leaves_jungle_light",
    "mtg_aspen_leaves",
    "mtg_acacia_leaves",
    "mtg_jungleleaves",
    "mtg_pine_needles",
)
SHRUB_BLOCK_NAMES = (
    "mtg_bush_stem",
    "mtg_bush_leaves",
    "mtg_blueberry_bush_leaves",
    "mtg_blueberry_bush_leaves_with_berries",
    "mtg_acacia_bush_stem",
    "mtg_acacia_bush_leaves",
    "mtg_pine_bush_stem",
    "mtg_pine_bush_needles",
)
TREE_BLOCK_NAMES = TREE_LOG_BLOCK_NAMES + TREE_LEAF_BLOCK_NAMES
VEGETATION_BLOCK_NAMES = TREE_BLOCK_NAMES + SHRUB_BLOCK_NAMES


def vegetation_block_category(block_name: str) -> str | None:
    """Classify blocks used by authored tree and procedural shrub features."""
    if block_name in TREE_BLOCK_NAMES:
        return "tree"
    if block_name in SHRUB_BLOCK_NAMES:
        return "shrub"
    return None


def count_spawn_vegetation_blocks(
    world_dir: Path,
    id_to_name: dict[int, str],
    spawn_radius: int,
    *,
    center_x: int = 0,
    center_z: int = 0,
) -> dict[str, int]:
    chunk_dir = world_dir / "chunks"
    if not chunk_dir.is_dir():
        return {}
    counts: Counter[str] = Counter()
    for path in chunk_dir.glob("*.cchunk"):
        for (wx, _wy, wz), bid in decode_chunk(path).items():
            if abs(wx - center_x) > spawn_radius or abs(wz - center_z) > spawn_radius:
                continue
            name = id_to_name.get(bid, "")
            if name in VEGETATION_BLOCK_NAMES:
                counts[name] += 1
    return dict(counts)


def terrain_surface_y(
    columns: dict[tuple[int, int, int], int],
    wx: int,
    wz: int,
    *,
    air_id: int = 0,
) -> int | None:
    """Top solid block with air above (worldgen ground), not canopy top."""
    heights = [wy for (x, wy, z), bid in columns.items() if x == wx and z == wz and bid != air_id]
    if not heights:
        return None
    for wy in sorted(heights, reverse=True):
        above = columns.get((wx, wy + 1, wz), air_id)
        if above == air_id:
            return wy
    return None


def count_spawn_ground_logs(
    world_dir: Path,
    id_to_name: dict[int, str],
    spawn_radius: int,
    surface: dict[tuple[int, int], tuple[int, int]],
) -> int:
    """tree_log blocks placed at column terrain surface (tree bases, bush centers)."""
    chunk_dir = world_dir / "chunks"
    if not chunk_dir.is_dir():
        return 0
    columns: dict[tuple[int, int, int], int] = {}
    for path in chunk_dir.glob("*.cchunk"):
        columns.update(decode_chunk(path))
    count = 0
    for (wx, wy, wz), bid in columns.items():
        if abs(wx) > spawn_radius or abs(wz) > spawn_radius:
            continue
        if id_to_name.get(bid, "") != "tree_log":
            continue
        surface_entry = surface.get((wx, wz))
        ground_y = surface_entry[0] if surface_entry is not None else None
        if ground_y is not None and wy == ground_y:
            count += 1
    return count


def count_bush_common_footprints(
    world_dir: Path,
    id_to_name: dict[int, str],
    spawn_radius: int,
    surface: dict[tuple[int, int], tuple[int, int]],
) -> int:
    """tree_log with a ring of tree_leaves on the same Y (bush_common)."""
    chunk_dir = world_dir / "chunks"
    if not chunk_dir.is_dir():
        return 0
    columns: dict[tuple[int, int, int], int] = {}
    for path in chunk_dir.glob("*.cchunk"):
        columns.update(decode_chunk(path))
    logs: list[tuple[int, int, int]] = []
    leaves: set[tuple[int, int, int]] = set()
    for (wx, wy, wz), bid in columns.items():
        if abs(wx) > spawn_radius or abs(wz) > spawn_radius:
            continue
        name = id_to_name.get(bid, "")
        if name == "tree_log":
            logs.append((wx, wy, wz))
        elif name == "tree_leaves":
            leaves.add((wx, wy, wz))
    count = 0
    for wx, wy, wz in logs:
        ring = 0
        for dx, dz in (
            (-1, -1),
            (0, -1),
            (1, -1),
            (-1, 0),
            (1, 0),
            (-1, 1),
            (0, 1),
            (1, 1),
        ):
            if (wx + dx, wy, wz + dz) in leaves:
                ring += 1
        if ring >= 6:
            count += 1
    return count


def count_spawn_fire_blocks(
    world_dir: Path,
    id_to_name: dict[int, str],
    spawn_radius: int,
) -> int:
    chunk_dir = world_dir / "chunks"
    if not chunk_dir.is_dir():
        return 0
    count = 0
    for path in chunk_dir.glob("*.cchunk"):
        for (wx, _wy, wz), bid in decode_chunk(path).items():
            if abs(wx) > spawn_radius or abs(wz) > spawn_radius:
                continue
            if id_to_name.get(bid, "") == "fire":
                count += 1
    return count


def count_shore_air_gaps(
    columns: dict[tuple[int, int, int], int],
    *,
    water_id: int,
    sea_level: int,
    min_world_y: int = 0,
    air_id: int = 0,
) -> int:
    """AIR at y<=sea with a 6-neighbor water block (shore seal gap)."""
    dirs = (
        (0, 1, 0),
        (-1, 0, 0),
        (1, 0, 0),
        (0, 0, -1),
        (0, 0, 1),
        (0, -1, 0),
    )
    gaps = 0
    for (wx, wy, wz), bid in columns.items():
        if bid != air_id or wy < min_world_y or wy > sea_level:
            continue
        for dx, dy, dz in dirs:
            if columns.get((wx + dx, wy + dy, wz + dz), air_id) == water_id:
                gaps += 1
                break
    return gaps


def ocean_depth_stats(
    columns: dict[tuple[int, int, int], int],
    *,
    sea_level: int,
    water_id: int | None,
    min_world_y: int = 0,
) -> dict:
    """Measure contiguous surface-ocean water depth from sea level to floor."""
    if water_id is None:
        return {
            "ocean_columns": 0,
            "ocean_depth_p10": 0,
            "ocean_depth_p50": 0,
            "ocean_depth_p90": 0,
            "ocean_depth_max": 0,
        }

    depths: list[int] = []
    for (x, y, z), bid in columns.items():
        if y != sea_level or bid != water_id:
            continue
        floor_y = sea_level - 1
        while floor_y >= min_world_y and columns.get((x, floor_y, z), 0) == water_id:
            floor_y -= 1
        depths.append(sea_level - floor_y)

    if not depths:
        return {
            "ocean_columns": 0,
            "ocean_depth_p10": 0,
            "ocean_depth_p50": 0,
            "ocean_depth_p90": 0,
            "ocean_depth_max": 0,
        }

    depths.sort()
    return {
        "ocean_columns": len(depths),
        "ocean_depth_p10": depths[int((len(depths) - 1) * 0.10)],
        "ocean_depth_p50": depths[int((len(depths) - 1) * 0.50)],
        "ocean_depth_p90": depths[int((len(depths) - 1) * 0.90)],
        "ocean_depth_max": depths[-1],
    }


def micro_pit_stats(surface: dict[tuple[int, int], tuple[int, int]]) -> dict:
    pits = 0
    flat_columns = 0
    for (x, z), (y, _) in surface.items():
        neighbors: list[int] = []
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            neighbor = surface.get((x + dx, z + dz))
            if neighbor is None:
                continue
            neighbors.append(neighbor[0])
        if len(neighbors) < 4:
            continue
        local_range = max(neighbors + [y]) - min(neighbors + [y])
        if local_range > 2:
            continue
        flat_columns += 1
        min_neighbor = min(neighbors)
        if y <= min_neighbor - 1 and (min_neighbor - y) <= 2:
            pits += 1
    return {
        "flat_columns": flat_columns,
        "micro_pits": pits,
        "micro_pit_pct": 100.0 * pits / max(1, flat_columns),
    }


def load_world_columns(world_dir: Path) -> dict[tuple[int, int], tuple[int, int]]:
    """Return (surface_y, surface_block_id) per (wx, wz)."""
    chunk_dir = world_dir / "chunks"
    if not chunk_dir.is_dir():
        return {}

    columns: dict[tuple[int, int, int], int] = {}
    for path in chunk_dir.glob("*.cchunk"):
        columns.update(decode_chunk(path))

    surface: dict[tuple[int, int], tuple[int, int]] = {}
    by_xz: dict[tuple[int, int], list[tuple[int, int]]] = defaultdict(list)
    for (wx, wy, wz), bid in columns.items():
        by_xz[(wx, wz)].append((wy, bid))

    for xz, entries in by_xz.items():
        wy, bid = max(entries, key=lambda t: t[0])
        surface[xz] = (wy, bid)
    return surface


def load_slot_map(refs_path: Path) -> dict[str, list[str]]:
    data = json.loads(refs_path.read_text(encoding="utf-8"))
    slots = data.get("slots", {})
    result: dict[str, list[str]] = {}
    for slot_name, entry in slots.items():
        names = entry.get("block_names", [])
        if isinstance(names, list):
            result[slot_name] = [str(n) for n in names]
    return result


def load_pack_blocks(pack_dir: Path) -> dict[str, int]:
    """block name -> pack id from resource_packs/*/blocks/*.json."""
    blocks_dir = pack_dir / "blocks"
    if not blocks_dir.is_dir():
        return {}
    out: dict[str, int] = {}
    for path in blocks_dir.glob("*.json"):
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        name = data.get("name")
        bid = data.get("id")
        if name and isinstance(bid, int):
            out[str(name)] = bid
    return out


def load_pack_block_entries(pack_dir: Path) -> list[tuple[str, int | None]]:
    """Return block names and optional stable ids from one resource pack."""
    blocks_dir = pack_dir / "blocks"
    if not blocks_dir.is_dir():
        return []
    out: list[tuple[str, int | None]] = []
    for path in sorted(blocks_dir.glob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            continue
        name = data.get("name")
        bid = data.get("id")
        if isinstance(name, str) and name:
            out.append((name, bid if isinstance(bid, int) else None))
    return out


def build_id_to_name_from_world(world_dir: Path, repo_root: Path) -> dict[int, str]:
    meta_path = world_dir / "world_data.json"
    if not meta_path.is_file():
        return {}
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    packs_cfg = meta.get("resource_packs", {})
    selected_packs: list[str] = []
    for tier in ("primary", "secondary"):
        pack_ids = packs_cfg.get(tier, [])
        if isinstance(pack_ids, list):
            selected_packs.extend(str(pack_id) for pack_id in pack_ids)
    id_to_name: dict[int, str] = {}
    names_without_stable_ids: set[str] = set()
    packs_root = repo_root / "resource_packs"
    for pack_id in selected_packs:
        for name, bid in load_pack_block_entries(packs_root / pack_id):
            if bid is None:
                names_without_stable_ids.add(name)
            else:
                id_to_name[bid] = name

    # BlockMergeRegistry gives id-less pack blocks deterministic fallback ids
    # after the highest explicit pack id, sorted by block name. Mirror that
    # assignment so metrics can classify blocks contributed by secondary packs.
    next_id = max(1, max(id_to_name, default=0) + 1)
    for name in sorted(names_without_stable_ids):
        if name in id_to_name.values():
            continue
        while next_id in id_to_name:
            next_id += 1
        id_to_name[next_id] = name
        next_id += 1
    return id_to_name


def name_to_slot(block_name: str, slot_map: dict[str, list[str]]) -> str | None:
    for slot, names in slot_map.items():
        if block_name in names:
            return slot
    return None


def block_id_to_slot(
    block_id: int,
    id_to_name: dict[int, str],
    slot_map: dict[str, list[str]],
) -> str:
    name = id_to_name.get(block_id)
    if not name:
        return f"unknown_{block_id}"
    slot = name_to_slot(name, slot_map)
    return slot if slot else name


GROUND_COVER_BLOCK_NAMES = frozenset(
    {
        "tall_grass",
        "dry_grass",
        "flower_red",
        "flower_yellow",
        "flower_blue",
        "flower_white",
    }
)


def load_world_block_columns(world_dir: Path) -> dict[tuple[int, int, int], int]:
    chunk_dir = world_dir / "chunks"
    if not chunk_dir.is_dir():
        return {}
    columns: dict[tuple[int, int, int], int] = {}
    for path in chunk_dir.glob("*.cchunk"):
        columns.update(decode_chunk(path))
    return columns


def terrain_surface_map(
    columns: dict[tuple[int, int, int], int],
    *,
    air_id: int = 0,
    ignored_ids: set[int] | frozenset[int] = frozenset(),
) -> dict[tuple[int, int], int]:
    surface: dict[tuple[int, int], int] = {}
    for (wx, wy, wz), bid in columns.items():
        if bid == air_id or bid in ignored_ids:
            continue
        xz = (wx, wz)
        surface[xz] = max(wy, surface.get(xz, wy))
    return surface


def local_height_range(
    surface: dict[tuple[int, int], int], x: int, z: int
) -> int | None:
    center = surface.get((x, z))
    if center is None:
        return None
    neighbors = [center]
    for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        neighbor = surface.get((x + dx, z + dz))
        if neighbor is not None:
            neighbors.append(neighbor)
    if len(neighbors) < 2:
        return None
    return max(neighbors) - min(neighbors)


def terrain_shape_stats(surface: dict[tuple[int, int], tuple[int, int]]) -> dict:
    heights_only: dict[tuple[int, int], int] = {
        xz: y for xz, (y, _) in surface.items()
    }
    if not heights_only:
        return {
            "flatness_pct": 0.0,
            "rolling_hill_pct": 0.0,
            "plateau_edge_pct": 0.0,
        }

    flat_count = 0
    rolling_count = 0
    plateau_edges = 0
    measured = 0

    for (x, z), y in heights_only.items():
        local_range = local_height_range(heights_only, x, z)
        if local_range is None:
            continue
        measured += 1
        if local_range <= 1:
            flat_count += 1
        if 2 <= local_range <= 4:
            rolling_count += 1

        neighbor_heights: list[int] = []
        for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            neighbor = heights_only.get((x + dx, z + dz))
            if neighbor is not None:
                neighbor_heights.append(neighbor)
        if not neighbor_heights:
            continue
        neighbor_range = max(neighbor_heights + [y]) - min(neighbor_heights + [y])
        if neighbor_range <= 1:
            max_delta = max(abs(y - ny) for ny in neighbor_heights)
            if max_delta >= 4:
                plateau_edges += 1

    denom = max(1, measured)
    return {
        "flatness_pct": 100.0 * flat_count / denom,
        "rolling_hill_pct": 100.0 * rolling_count / denom,
        "plateau_edge_pct": 100.0 * plateau_edges / denom,
        "shape_columns_measured": measured,
    }


def cave_stats(
    columns: dict[tuple[int, int, int], int],
    surface: dict[tuple[int, int], int],
    *,
    air_id: int = 0,
    spawn_radius: int | None = None,
    min_world_y: int = 0,
) -> dict:
    if not columns or not surface:
        return {
            "cave_air_volume": 0,
            "max_cave_depth_below_surface": 0,
            "cave_chunk_coverage_pct": 0.0,
        }

    cave_air = 0
    max_depth = 0
    chunks_with_caves: set[tuple[int, int]] = set()
    total_chunks: set[tuple[int, int]] = set()

    for (wx, wz), ground_y in surface.items():
        if spawn_radius is not None and (
            abs(wx) > spawn_radius or abs(wz) > spawn_radius
        ):
            continue
        chunk_x = wx // CHUNK_SIZE if wx >= 0 else (wx - CHUNK_SIZE + 1) // CHUNK_SIZE
        chunk_z = wz // CHUNK_SIZE if wz >= 0 else (wz - CHUNK_SIZE + 1) // CHUNK_SIZE
        total_chunks.add((chunk_x, chunk_z))
        for wy in range(min_world_y, ground_y):
            if columns.get((wx, wy, wz), air_id) != air_id:
                continue
            depth = ground_y - wy
            cave_air += 1
            max_depth = max(max_depth, depth)
            chunks_with_caves.add((chunk_x, chunk_z))

    chunk_coverage = (
        100.0 * len(chunks_with_caves) / max(1, len(total_chunks))
        if total_chunks
        else 0.0
    )
    return {
        "cave_air_volume": cave_air,
        "max_cave_depth_below_surface": max_depth,
        "cave_chunk_coverage_pct": chunk_coverage,
        "cave_chunks_with_air": len(chunks_with_caves),
        "cave_chunks_total": len(total_chunks),
    }


def cave_void_fluid_stats(
    columns: dict[tuple[int, int, int], int],
    surface: dict[tuple[int, int], int],
    id_to_name: dict[int, str],
    *,
    air_id: int = 0,
    min_world_y: int = 0,
) -> dict:
    """Estimate dry and liquid cave voxels below each column's terrain surface."""
    water_ids = {bid for bid, name in id_to_name.items() if name == "water"}
    lava_ids = {bid for bid, name in id_to_name.items() if name == "lava"}
    dry_air = water = lava = 0
    for (wx, wz), ground_y in surface.items():
        for wy in range(min_world_y, ground_y):
            block_id = columns.get((wx, wy, wz), air_id)
            if block_id == air_id:
                dry_air += 1
            elif block_id in water_ids:
                water += 1
            elif block_id in lava_ids:
                lava += 1

    total = dry_air + water + lava
    denominator = max(1, total)
    return {
        "cave_dry_air_voxels": dry_air,
        "cave_water_voxels": water,
        "cave_lava_voxels": lava,
        "cave_void_voxels": total,
        "cave_dry_pct": 100.0 * dry_air / denominator,
        "cave_water_pct": 100.0 * water / denominator,
        "cave_lava_pct": 100.0 * lava / denominator,
    }


def vegetation_cluster_stats(
    world_dir: Path,
    id_to_name: dict[int, str],
    spawn_radius: int,
    surface: dict[tuple[int, int], tuple[int, int]],
) -> dict:
    columns = load_world_block_columns(world_dir)
    ground_cover_columns: set[tuple[int, int]] = set()
    tree_columns: set[tuple[int, int]] = set()

    for (wx, wy, wz), bid in columns.items():
        if abs(wx) > spawn_radius or abs(wz) > spawn_radius:
            continue
        name = id_to_name.get(bid, "")
        surface_entry = surface.get((wx, wz))
        ground_y = surface_entry[0] if surface_entry is not None else None
        if name in GROUND_COVER_BLOCK_NAMES:
            if ground_y is not None and wy <= ground_y + 2:
                ground_cover_columns.add((wx, wz))
        if name in VEGETATION_BLOCK_NAMES:
            if ground_y is not None and wy >= ground_y:
                tree_columns.add((wx, wz))

    spawn_columns = [
        xz
        for xz in surface
        if abs(xz[0]) <= spawn_radius and abs(xz[1]) <= spawn_radius
    ]
    ground_cover_density = (
        100.0 * len(ground_cover_columns) / max(1, len(spawn_columns))
    )

    feature_columns = sorted(ground_cover_columns | tree_columns)
    if len(feature_columns) > 384:
        stride = max(1, len(feature_columns) // 384)
        feature_columns = feature_columns[::stride]
    nn_distances: list[float] = []
    for i, (x0, z0) in enumerate(feature_columns):
        best = float("inf")
        for j, (x1, z1) in enumerate(feature_columns):
            if i == j:
                continue
            dist = ((x0 - x1) ** 2 + (z0 - z1) ** 2) ** 0.5
            if dist < best:
                best = dist
        if best < float("inf"):
            nn_distances.append(best)

    if len(nn_distances) >= 2:
        mean_nn = sum(nn_distances) / len(nn_distances)
        variance = sum((d - mean_nn) ** 2 for d in nn_distances) / len(nn_distances)
        nn_cv = (variance**0.5) / max(mean_nn, 1e-6)
    else:
        nn_cv = 0.0

    window = 16
    densities: list[float] = []
    if spawn_columns:
        min_x = min(x for x, _ in spawn_columns)
        max_x = max(x for x, _ in spawn_columns)
        min_z = min(z for _, z in spawn_columns)
        max_z = max(z for _, z in spawn_columns)
        for wx in range(min_x, max_x + 1, window):
            for wz in range(min_z, max_z + 1, window):
                count = sum(
                    1
                    for x, z in ground_cover_columns
                    if wx <= x < wx + window and wz <= z < wz + window
                )
                densities.append(float(count))

    if len(densities) >= 2:
        mean_density = sum(densities) / len(densities)
        density_var = sum((d - mean_density) ** 2 for d in densities) / len(
            densities
        )
    else:
        density_var = 0.0

    return {
        "ground_cover_density": ground_cover_density,
        "ground_cover_columns": len(ground_cover_columns),
        "nn_distance_cv": nn_cv,
        "local_density_variance": density_var,
        "feature_columns": len(feature_columns),
    }


def _height_deltas(surface: dict[tuple[int, int], tuple[int, int]]) -> list[int]:
    deltas: list[int] = []
    by_x: dict[int, dict[int, int]] = defaultdict(dict)
    for (x, z), (y, _) in surface.items():
        by_x[x][z] = y
    for x, zmap in by_x.items():
        for z, y in zmap.items():
            if x + 1 in by_x and z in by_x[x + 1]:
                deltas.append(abs(y - by_x[x + 1][z]))
            if z + 1 in zmap:
                deltas.append(abs(y - zmap[z + 1]))
    deltas.sort()
    return deltas


def _spawn_slot_fractions(
    surface: dict[tuple[int, int], tuple[int, int]],
    spawn_radius: int,
    id_to_name: dict[int, str],
    slot_map: dict[str, list[str]],
) -> dict[str, float]:
    counts: Counter[str] = Counter()
    total = 0
    for (x, z), (_, bid) in surface.items():
        if abs(x) > spawn_radius or abs(z) > spawn_radius:
            continue
        slot = block_id_to_slot(bid, id_to_name, slot_map)
        counts[slot] += 1
        total += 1
    if total == 0:
        return {}
    return {slot: 100.0 * count / total for slot, count in counts.items()}


def analyze_surface(
    surface: dict[tuple[int, int], tuple[int, int]],
    *,
    spawn_radius: int = 48,
    max_height: int = 319,
    min_height: int = 0,
    id_to_name: dict[int, str] | None = None,
    slot_map: dict[str, list[str]] | None = None,
) -> dict:
    if not surface:
        return {"columns": 0}

    heights = [y for y, _ in surface.values()]
    top_blocks = Counter(bid for _, bid in surface.values())
    deltas = _height_deltas(surface)

    cliff16 = sum(1 for d in deltas if d >= 16)
    cliff8 = sum(1 for d in deltas if d >= 8)
    cliff4 = sum(1 for d in deltas if d >= 4)
    cliff1 = sum(1 for d in deltas if d >= 1)
    max_violations = sum(1 for y in heights if y > max_height)
    min_violations = sum(1 for y in heights if y < min_height)
    pits = micro_pit_stats(surface)
    shape = terrain_shape_stats(surface)

    result: dict = {
        "columns": len(surface),
        "height_min": min(heights),
        "height_max": max(heights),
        "world_min_height": min_height,
        "world_max_height": max_height,
        "height_mean": sum(heights) / len(heights),
        "delta_mean": sum(deltas) / max(1, len(deltas)),
        "delta_p95": deltas[int(len(deltas) * 0.95)] if deltas else 0,
        "delta_ge_1_pct": 100.0 * cliff1 / max(1, len(deltas)),
        "delta_ge_4_pct": 100.0 * cliff4 / max(1, len(deltas)),
        "delta_ge_8_pct": 100.0 * cliff8 / max(1, len(deltas)),
        "delta_ge_16_pct": 100.0 * cliff16 / max(1, len(deltas)),
        "max_height_violations": max_violations,
        "min_height_violations": min_violations,
        "top_block_ids": top_blocks.most_common(8),
        "micro_pit_pct": pits["micro_pit_pct"],
        "micro_pits": pits["micro_pits"],
        "flatness_pct": shape["flatness_pct"],
        "rolling_hill_pct": shape["rolling_hill_pct"],
        "plateau_edge_pct": shape["plateau_edge_pct"],
    }

    if id_to_name is not None and slot_map is not None:
        spawn_slots = _spawn_slot_fractions(
            surface, spawn_radius, id_to_name, slot_map
        )
        spawn_heights = [
            y
            for (x, z), (y, _) in surface.items()
            if abs(x) <= spawn_radius and abs(z) <= spawn_radius
        ]
        result["spawn"] = {
            "columns": len(spawn_heights),
            "height_mean": sum(spawn_heights) / max(1, len(spawn_heights)),
            "surface_slots": spawn_slots,
        }
    return result


def spawn_center_from_meta(meta: dict) -> tuple[int, int]:
    spawn = meta.get("spawn_point")
    if isinstance(spawn, list) and len(spawn) >= 3:
        return int(round(float(spawn[0]))), int(round(float(spawn[2])))
    return 0, 0


def world_vertical_bounds(
    meta: dict,
    *,
    min_height: int | None = None,
    max_height: int | None = None,
) -> tuple[int, int]:
    """Return the saved world bounds, keeping legacy saves at their old range."""
    procedural = meta.get("procedural", {})
    min_world_y = (
        min_height if min_height is not None else procedural.get("min_height", 0)
    )
    max_world_y = (
        max_height if max_height is not None else procedural.get("max_height", 319)
    )
    return int(min_world_y), int(max_world_y)


def spawn_surface_subset(
    surface: dict[tuple[int, int], tuple[int, int]],
    center_x: int,
    center_z: int,
    radius: int,
) -> dict[tuple[int, int], tuple[int, int]]:
    return {
        xz: val
        for xz, val in surface.items()
        if abs(xz[0] - center_x) <= radius and abs(xz[1] - center_z) <= radius
    }


def land_surface_subset(
    surface: dict[tuple[int, int], tuple[int, int]],
    water_id: int | None,
) -> dict[tuple[int, int], tuple[int, int]]:
    if water_id is None:
        return dict(surface)
    land: dict[tuple[int, int], tuple[int, int]] = {}
    for xz, (y, bid) in surface.items():
        if bid == water_id:
            continue
        land[xz] = (y, bid)
    return land


def spawn_land_shape_metrics(
    surface: dict[tuple[int, int], tuple[int, int]],
    *,
    center_x: int,
    center_z: int,
    radius: int,
    water_id: int | None,
) -> dict:
    spawn_surface = spawn_surface_subset(surface, center_x, center_z, radius)
    spawn_land = land_surface_subset(spawn_surface, water_id)
    shape_source = spawn_land if spawn_land else spawn_surface
    if not shape_source:
        return {
            "spawn_center": [center_x, center_z],
            "spawn_land_columns": 0,
            "spawn_flatness_pct": 0.0,
            "spawn_rolling_hill_pct": 0.0,
            "spawn_plateau_edge_pct": 0.0,
        }
    shape = terrain_shape_stats(shape_source)
    return {
        "spawn_center": [center_x, center_z],
        "spawn_land_columns": len(shape_source),
        "spawn_flatness_pct": shape["flatness_pct"],
        "spawn_rolling_hill_pct": shape["rolling_hill_pct"],
        "spawn_plateau_edge_pct": shape["plateau_edge_pct"],
    }


def analyze_world(
    world_dir: Path,
    refs_path: Path,
    *,
    repo_root: Path | None = None,
    spawn_radius: int = 48,
    max_height: int | None = None,
    min_height: int | None = None,
) -> dict:
    repo = repo_root or refs_path.parent.parent
    slot_map = load_slot_map(refs_path)
    id_to_name = build_id_to_name_from_world(world_dir, repo)
    surface = load_world_columns(world_dir)
    meta_path = world_dir / "world_data.json"
    meta: dict = {}
    if meta_path.is_file():
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
    spawn_x, spawn_z = spawn_center_from_meta(meta)
    # Legacy saves without vertical profile metadata predate signed-height
    # overworlds and must keep their historical metric range.
    min_world_y, world_max_y = world_vertical_bounds(
        meta,
        min_height=min_height,
        max_height=max_height,
    )
    procedural = meta.get("procedural", {})
    metrics = analyze_surface(
        surface,
        spawn_radius=spawn_radius,
        max_height=world_max_y,
        min_height=min_world_y,
        id_to_name=id_to_name,
        slot_map=slot_map,
    )
    veg = count_spawn_vegetation_blocks(
        world_dir,
        id_to_name,
        spawn_radius,
        center_x=spawn_x,
        center_z=spawn_z,
    )
    metrics["spawn_vegetation"] = veg
    metrics["spawn_tree_blocks"] = sum(veg.get(name, 0) for name in TREE_BLOCK_NAMES)
    metrics["spawn_shrub_blocks"] = sum(
        veg.get(name, 0) for name in SHRUB_BLOCK_NAMES
    )
    metrics["spawn_ground_logs"] = count_spawn_ground_logs(
        world_dir, id_to_name, spawn_radius, surface
    )
    metrics["spawn_bush_common_footprints"] = count_bush_common_footprints(
        world_dir, id_to_name, spawn_radius, surface
    )
    metrics["spawn_fire_blocks"] = count_spawn_fire_blocks(
        world_dir, id_to_name, spawn_radius
    )
    sea_level = int(procedural.get("sea_level", meta.get("sea_level", 63)))
    if meta:
        metrics["seed"] = meta.get("world_seed")
        metrics["world_name"] = meta.get("world_name")
    water_id = next(
        (bid for bid, name in id_to_name.items() if name == "water"),
        None,
    )
    metrics.update(
        spawn_land_shape_metrics(
            surface,
            center_x=spawn_x,
            center_z=spawn_z,
            radius=spawn_radius,
            water_id=water_id,
        )
    )
    columns = load_world_block_columns(world_dir)
    metrics.update(
        ocean_depth_stats(
            columns,
            sea_level=sea_level,
            water_id=water_id,
            min_world_y=min_world_y,
        )
    )
    metrics["sea_level"] = sea_level
    non_terrain_ids = {
        bid for bid, name in id_to_name.items() if name in {"water", "lava", "fire"}
    }
    terrain_surface = terrain_surface_map(columns, ignored_ids=non_terrain_ids)
    metrics.update(
        cave_stats(
            columns,
            terrain_surface,
            # Measure cave networks over the generated test area, not only at spawn.
            spawn_radius=None,
            min_world_y=min_world_y,
        )
    )
    metrics.update(
        cave_void_fluid_stats(
            columns,
            terrain_surface,
            id_to_name,
            min_world_y=min_world_y,
        )
    )
    metrics.update(
        vegetation_cluster_stats(
            world_dir,
            id_to_name,
            spawn_radius,
            surface,
        )
    )

    if water_id is not None:
        metrics["shore_air_gaps"] = count_shore_air_gaps(
            columns,
            water_id=water_id,
            sea_level=sea_level,
            min_world_y=min_world_y,
        )
    return metrics


def compare_to_thresholds(metrics: dict, thresholds: dict) -> list[str]:
    failures: list[str] = []

    def fail(msg: str) -> None:
        failures.append(msg)

    if metrics.get("columns", 0) == 0:
        fail("no terrain columns")
        return failures

    ocean_min_columns = thresholds.get("ocean_columns_min_for_depth_gate", 64)
    if metrics.get("ocean_columns", 0) >= ocean_min_columns:
        ocean_depth_p50_min = thresholds.get("ocean_depth_p50_min")
        if (
            ocean_depth_p50_min is not None
            and metrics.get("ocean_depth_p50", 0) < ocean_depth_p50_min
        ):
            fail(
                f"ocean_depth_p50={metrics.get('ocean_depth_p50', 0)} < "
                f"{ocean_depth_p50_min}"
            )
        ocean_depth_p90_min = thresholds.get("ocean_depth_p90_min")
        if (
            ocean_depth_p90_min is not None
            and metrics.get("ocean_depth_p90", 0) < ocean_depth_p90_min
        ):
            fail(
                f"ocean_depth_p90={metrics.get('ocean_depth_p90', 0)} < "
                f"{ocean_depth_p90_min}"
            )

    if metrics.get("delta_ge_16_pct", 0) > thresholds.get("delta_ge_16_pct_max", 0.5):
        fail(
            f"delta_ge_16_pct={metrics['delta_ge_16_pct']:.3f} > "
            f"{thresholds['delta_ge_16_pct_max']}"
        )
    if metrics.get("delta_ge_8_pct", 0) > thresholds.get("delta_ge_8_pct_max", 5.0):
        fail(
            f"delta_ge_8_pct={metrics['delta_ge_8_pct']:.3f} > "
            f"{thresholds['delta_ge_8_pct_max']}"
        )

    mean_y = metrics.get("height_mean", 0)
    if mean_y < thresholds.get("height_mean_min", 48.0):
        fail(f"height_mean={mean_y:.2f} < {thresholds['height_mean_min']}")
    if mean_y > thresholds.get("height_mean_max", 65.0):
        fail(f"height_mean={mean_y:.2f} > {thresholds['height_mean_max']}")

    slack = thresholds.get("height_max_slack", 1)
    max_h = thresholds.get("max_height", 319)
    if metrics.get("height_max", 0) > max_h + slack:
        fail(f"height_max={metrics['height_max']} > {max_h + slack}")

    min_h = thresholds.get("min_height", metrics.get("world_min_height", 0))
    if metrics.get("height_min", 0) < min_h:
        fail(f"height_min={metrics['height_min']} < {min_h}")
    if metrics.get("min_height_violations", 0) > thresholds.get(
        "min_height_violations_max", 0
    ):
        fail(f"min_height_violations={metrics['min_height_violations']}")

    if metrics.get("max_height_violations", 0) > thresholds.get(
        "max_height_violations_max", 0
    ):
        fail(f"max_height_violations={metrics['max_height_violations']}")

    spawn = metrics.get("spawn", {})
    slots = spawn.get("surface_slots", {})
    grass_pct = slots.get("grass", 0.0)
    meadow_pct = grass_pct + slots.get("tall_grass", 0.0)
    stone_pct = slots.get("stone", 0.0)
    meadow_min = thresholds.get("spawn_meadow_min_pct")
    if meadow_min is not None:
        if meadow_pct < meadow_min:
            fail(f"spawn meadow (grass+tall_grass)={meadow_pct:.1f}% < {meadow_min}%")
    elif grass_pct < thresholds.get("spawn_grass_min_pct", 30.0):
        fail(f"spawn grass={grass_pct:.1f}% < {thresholds['spawn_grass_min_pct']}%")
    if stone_pct > thresholds.get("spawn_stone_max_pct", 45.0):
        fail(f"spawn stone={stone_pct:.1f}% > {thresholds['spawn_stone_max_pct']}%")

    tree_min = thresholds.get("spawn_tree_blocks_min")
    if tree_min is not None:
        tree_blocks = metrics.get("spawn_tree_blocks", 0)
        if tree_blocks < tree_min:
            fail(f"spawn tree_blocks={tree_blocks} < {tree_min}")

    ground_log_max = thresholds.get("spawn_ground_logs_max")
    if ground_log_max is not None:
        ground_logs = metrics.get("spawn_ground_logs", 0)
        if ground_logs > ground_log_max:
            fail(f"spawn ground_logs={ground_logs} > {ground_log_max}")

    bush_max = thresholds.get("spawn_bush_common_footprints_max")
    if bush_max is not None:
        bush_count = metrics.get("spawn_bush_common_footprints", 0)
        if bush_count > bush_max:
            fail(f"spawn bush_common_footprints={bush_count} > {bush_max}")

    fire_max = thresholds.get("spawn_fire_blocks_max")
    if fire_max is not None:
        fire_blocks = metrics.get("spawn_fire_blocks", 0)
        if fire_blocks > fire_max:
            fail(f"spawn fire_blocks={fire_blocks} > {fire_max}")

    micro_pit_max = thresholds.get("micro_pit_pct_max")
    if micro_pit_max is not None:
        micro_pit_pct = metrics.get("micro_pit_pct", 0.0)
        if micro_pit_pct > micro_pit_max:
            fail(f"micro_pit_pct={micro_pit_pct:.2f}% > {micro_pit_max}%")

    shore_gaps_max = thresholds.get("shore_air_gaps_max")
    if shore_gaps_max is not None:
        shore_gaps = metrics.get("shore_air_gaps", 0)
        if shore_gaps > shore_gaps_max:
            fail(f"shore_air_gaps={shore_gaps} > {shore_gaps_max}")

    flatness_max = thresholds.get("flatness_pct_max")
    if flatness_max is not None:
        flatness = metrics.get(
            "spawn_flatness_pct", metrics.get("flatness_pct", 0.0)
        )
        if flatness > flatness_max:
            fail(f"flatness_pct={flatness:.2f}% > {flatness_max}%")

    flatness_min = thresholds.get("flatness_pct_min")
    if flatness_min is not None:
        flatness = metrics.get(
            "spawn_flatness_pct", metrics.get("flatness_pct", 0.0)
        )
        if flatness < flatness_min:
            fail(f"flatness_pct={flatness:.2f}% < {flatness_min}%")

    rolling_min = thresholds.get("rolling_hill_pct_min")
    if rolling_min is not None:
        rolling = metrics.get(
            "spawn_rolling_hill_pct", metrics.get("rolling_hill_pct", 0.0)
        )
        if rolling < rolling_min:
            fail(f"rolling_hill_pct={rolling:.2f}% < {rolling_min}%")

    plateau_edge_max = thresholds.get("plateau_edge_pct_max")
    if plateau_edge_max is not None:
        plateau_edge = metrics.get(
            "spawn_plateau_edge_pct", metrics.get("plateau_edge_pct", 0.0)
        )
        if plateau_edge > plateau_edge_max:
            fail(f"plateau_edge_pct={plateau_edge:.2f}% > {plateau_edge_max}%")

    cliff4_max = thresholds.get("delta_ge_4_pct_max")
    if cliff4_max is not None:
        cliff4 = metrics.get("delta_ge_4_pct", 0.0)
        if cliff4 > cliff4_max:
            fail(f"delta_ge_4_pct={cliff4:.3f}% > {cliff4_max}%")

    cave_coverage_min = thresholds.get("cave_chunk_coverage_pct_min")
    if cave_coverage_min is not None:
        cave_cov = metrics.get("cave_chunk_coverage_pct", 0.0)
        if cave_cov < cave_coverage_min:
            fail(
                f"cave_chunk_coverage_pct={cave_cov:.2f}% < {cave_coverage_min}%"
            )

    cave_depth_min = thresholds.get("max_cave_depth_below_surface_min")
    if cave_depth_min is not None:
        cave_depth = metrics.get("max_cave_depth_below_surface", 0)
        if cave_depth < cave_depth_min:
            fail(
                f"max_cave_depth_below_surface={cave_depth} < {cave_depth_min}"
            )

    nn_cv_min = thresholds.get("nn_distance_cv_min")
    if nn_cv_min is not None:
        nn_cv = metrics.get("nn_distance_cv", 0.0)
        if nn_cv < nn_cv_min:
            fail(f"nn_distance_cv={nn_cv:.3f} < {nn_cv_min}")

    return failures
