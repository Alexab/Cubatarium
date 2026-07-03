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


VEGETATION_BLOCK_NAMES = ("tree_log", "tree_leaves")


def count_spawn_vegetation_blocks(
    world_dir: Path,
    id_to_name: dict[int, str],
    spawn_radius: int,
) -> dict[str, int]:
    chunk_dir = world_dir / "chunks"
    if not chunk_dir.is_dir():
        return {}
    counts: Counter[str] = Counter()
    for path in chunk_dir.glob("*.cchunk"):
        for (wx, _wy, wz), bid in decode_chunk(path).items():
            if abs(wx) > spawn_radius or abs(wz) > spawn_radius:
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
        ground_y = terrain_surface_y(columns, wx, wz)
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
        if bid != air_id or wy < 1 or wy > sea_level:
            continue
        for dx, dy, dz in dirs:
            if columns.get((wx + dx, wy + dy, wz + dz), air_id) == water_id:
                gaps += 1
                break
    return gaps


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


def build_id_to_name_from_world(world_dir: Path, repo_root: Path) -> dict[int, str]:
    meta_path = world_dir / "world_data.json"
    if not meta_path.is_file():
        return {}
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    packs_cfg = meta.get("resource_packs", {})
    primary = packs_cfg.get("primary", [])
    if not isinstance(primary, list):
        primary = []
    id_to_name: dict[int, str] = {}
    packs_root = repo_root / "resource_packs"
    for pack_id in primary:
        pack_blocks = load_pack_blocks(packs_root / str(pack_id))
        for name, bid in pack_blocks.items():
            id_to_name[bid] = name
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
    max_height: int = 128,
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
    pits = micro_pit_stats(surface)

    result: dict = {
        "columns": len(surface),
        "height_min": min(heights),
        "height_max": max(heights),
        "height_mean": sum(heights) / len(heights),
        "delta_mean": sum(deltas) / max(1, len(deltas)),
        "delta_p95": deltas[int(len(deltas) * 0.95)] if deltas else 0,
        "delta_ge_1_pct": 100.0 * cliff1 / max(1, len(deltas)),
        "delta_ge_4_pct": 100.0 * cliff4 / max(1, len(deltas)),
        "delta_ge_8_pct": 100.0 * cliff8 / max(1, len(deltas)),
        "delta_ge_16_pct": 100.0 * cliff16 / max(1, len(deltas)),
        "max_height_violations": max_violations,
        "top_block_ids": top_blocks.most_common(8),
        "micro_pit_pct": pits["micro_pit_pct"],
        "micro_pits": pits["micro_pits"],
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


def analyze_world(
    world_dir: Path,
    refs_path: Path,
    *,
    repo_root: Path | None = None,
    spawn_radius: int = 48,
    max_height: int = 128,
) -> dict:
    repo = repo_root or refs_path.parent.parent
    slot_map = load_slot_map(refs_path)
    id_to_name = build_id_to_name_from_world(world_dir, repo)
    surface = load_world_columns(world_dir)
    metrics = analyze_surface(
        surface,
        spawn_radius=spawn_radius,
        max_height=max_height,
        id_to_name=id_to_name,
        slot_map=slot_map,
    )
    veg = count_spawn_vegetation_blocks(world_dir, id_to_name, spawn_radius)
    metrics["spawn_vegetation"] = veg
    metrics["spawn_tree_blocks"] = veg.get("tree_log", 0) + veg.get("tree_leaves", 0)
    metrics["spawn_ground_logs"] = count_spawn_ground_logs(
        world_dir, id_to_name, spawn_radius, surface
    )
    metrics["spawn_bush_common_footprints"] = count_bush_common_footprints(
        world_dir, id_to_name, spawn_radius, surface
    )
    metrics["spawn_fire_blocks"] = count_spawn_fire_blocks(
        world_dir, id_to_name, spawn_radius
    )
    meta_path = world_dir / "world_data.json"
    sea_level = 48
    if meta_path.is_file():
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        metrics["seed"] = meta.get("world_seed")
        metrics["world_name"] = meta.get("world_name")
        if meta.get("sea_level") is not None:
            sea_level = int(meta["sea_level"])
    water_id = next(
        (bid for bid, name in id_to_name.items() if name == "water"),
        None,
    )
    if water_id is not None:
        chunk_dir = world_dir / "chunks"
        columns: dict[tuple[int, int, int], int] = {}
        if chunk_dir.is_dir():
            for path in chunk_dir.glob("*.cchunk"):
                columns.update(decode_chunk(path))
        metrics["shore_air_gaps"] = count_shore_air_gaps(
            columns,
            water_id=water_id,
            sea_level=sea_level,
        )
    return metrics


def compare_to_thresholds(metrics: dict, thresholds: dict) -> list[str]:
    failures: list[str] = []

    def fail(msg: str) -> None:
        failures.append(msg)

    if metrics.get("columns", 0) == 0:
        fail("no terrain columns")
        return failures

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
    max_h = thresholds.get("max_height", 128)
    if metrics.get("height_max", 0) > max_h + slack:
        fail(f"height_max={metrics['height_max']} > {max_h + slack}")

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

    return failures
