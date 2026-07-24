#!/usr/bin/env python3
"""Analyze vegetation/decoration near world spawn."""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter, defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))
from worldgen_metrics_lib import count_spawn_vegetation_blocks, decode_chunk  # noqa: E402

RUNTIME_ID_MIN = 4096


def load_pack_id_map(pack_dir: Path) -> tuple[dict[int, str], dict[str, int]]:
    id_to_name: dict[int, str] = {}
    name_to_id: dict[str, int] = {}
    blocks_dir = pack_dir / "blocks"
    for path in blocks_dir.glob("*.json"):
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
        except json.JSONDecodeError:
            continue
        name = data.get("name") or path.stem
        bid = data.get("id")
        if bid is None:
            continue
        id_to_name[bid] = name
        name_to_id[name] = bid
    return id_to_name, name_to_id


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("world_dir", type=Path)
    parser.add_argument("--radius", type=int, default=64)
    parser.add_argument(
        "--pack",
        type=Path,
        default=REPO / "bin" / "resource_packs" / "minetest_default_16",
    )
    args = parser.parse_args()

    world_dir = args.world_dir
    chunk_dir = world_dir / "chunks"
    if not chunk_dir.is_dir():
        print(f"Missing chunks: {chunk_dir}", file=sys.stderr)
        return 1

    id_to_name, name_to_id = load_pack_id_map(args.pack)
    columns: dict[tuple[int, int, int], int] = {}
    for path in chunk_dir.glob("*.cchunk"):
        columns.update(decode_chunk(path))

    spawn = (0.0, 53.0, 0.0)
    wd_path = world_dir / "world_data.json"
    if wd_path.is_file():
        wd = json.loads(wd_path.read_text(encoding="utf-8"))
        if "spawn_point" in wd and len(wd["spawn_point"]) == 3:
            spawn = tuple(wd["spawn_point"])

    r = args.radius
    watch = (
        "tree_log",
        "tree_leaves",
        "tree_bark",
        "tall_grass",
        "stone",
        "fire",
    )
    global_watch: Counter[str] = Counter()
    near: Counter[str] = Counter()
    for (_x, _y, _z), bid in columns.items():
        name = id_to_name.get(bid, "")
        if name in watch:
            global_watch[name] += 1
        if abs(_x) <= r and abs(_z) <= r and name in watch:
            near[name] += 1

    bark_id = name_to_id.get("tree_bark", 0)
    triples = 0
    if bark_id:
        for (x, y, z), bid in columns.items():
            if bid != bark_id or abs(x) > r or abs(z) > r:
                continue
            if columns.get((x + 1, y, z)) == bid and columns.get((x + 2, y, z)) == bid:
                triples += 1

    runtime = sum(1 for bid in columns.values() if bid >= RUNTIME_ID_MIN)
    log_cols: dict[tuple[int, int], list[int]] = defaultdict(list)
    for (x, y, z), bid in columns.items():
        if abs(x) > r or abs(z) > r:
            continue
        if id_to_name.get(bid) == "tree_log":
            log_cols[(x, z)].append(y)

    tall_stacks = sum(
        1
        for ys in log_cols.values()
        if len(ys) >= 2 and max(ys) - min(ys) >= 2
    )

    print(f"=== {world_dir.name} spawn analysis (radius {r}) ===")
    print(f"Chunks: {len(list(chunk_dir.glob('*.cchunk')))}")
    print(f"Spawn: {spawn}")
    print(f"Global watch blocks: {dict(global_watch)}")
    print(f"Near spawn: {dict(near)}")
    print(f"deco_log triples (tree_bark): {triples}")
    print(f"Runtime ids (>={RUNTIME_ID_MIN}): {runtime}")
    print(f"Multi-y tree_log columns (standing trees): {tall_stacks}")
    print(f"Metrics lib vegetation: {count_spawn_vegetation_blocks(world_dir, id_to_name, r)}")

    prof = sorted(
        [(y, bid) for (x, y, z), bid in columns.items() if x == 0 and z == 0],
        key=lambda t: t[0],
    )
    print("Column (0,0) top layers:")
    for y, bid in prof[-14:]:
        print(f"  y={y:3d} {id_to_name.get(bid, bid)}")

    if wd_path.is_file():
        proc = wd.get("procedural", {})
        print(
            f"Settings: trees={proc.get('trees')} decoration={proc.get('decoration')} "
            f"ground_cover={proc.get('ground_cover')}"
        )

    ok = near["tree_log"] > 0 or near["tree_leaves"] > 10 or triples > 0
    return 0 if ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
