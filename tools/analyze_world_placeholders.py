#!/usr/bin/env python3
"""Scan world chunks for runtime/synthetic block ids (placeholder textures)."""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))
from worldgen_metrics_lib import decode_chunk  # noqa: E402

RUNTIME_ID_MIN = 4096


def load_pack_id_map(pack_dir: Path) -> tuple[dict[int, str], dict[str, int]]:
    id_to_name: dict[int, str] = {}
    name_to_id: dict[str, int] = {}
    blocks_dir = pack_dir / "blocks"
    if not blocks_dir.is_dir():
        return id_to_name, name_to_id
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


def count_horizontal_triples(blocks: dict[tuple[int, int, int], int], bid: int) -> int:
    triples = 0
    for (x, y, z), block_id in blocks.items():
        if block_id != bid:
            continue
        if blocks.get((x + 1, y, z)) == bid and blocks.get((x + 2, y, z)) == bid:
            triples += 1
    return triples


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("world_dir", type=Path)
    parser.add_argument(
        "--pack",
        type=Path,
        default=REPO / "bin" / "resource_packs" / "minetest_default_16",
    )
    args = parser.parse_args()

    world_dir = args.world_dir
    chunk_dir = world_dir / "chunks"
    if not chunk_dir.is_dir():
        print(f"Missing chunks dir: {chunk_dir}", file=sys.stderr)
        return 1

    id_to_name, name_to_id = load_pack_id_map(args.pack)
    counts: Counter[int] = Counter()
    runtime: Counter[int] = Counter()
    triples_by_id: Counter[int] = Counter()

    for path in chunk_dir.glob("*.cchunk"):
        blocks = decode_chunk(path)
        for bid in blocks.values():
            counts[bid] += 1
            if bid >= RUNTIME_ID_MIN:
                runtime[bid] += 1
        for bid in set(runtime):
            triples_by_id[bid] += count_horizontal_triples(blocks, bid)

    print(f"=== {world_dir.name} placeholder scan ===")
    print(f"Voxels total: {sum(counts.values())}")
    print(f"Runtime ids (>={RUNTIME_ID_MIN}): {sum(runtime.values())}")
    for bid, c in runtime.most_common():
        print(
            f"  id {bid}: {c} voxels, "
            f"pack name={id_to_name.get(bid, 'SYNTHETIC')}, "
            f"horizontal triples={triples_by_id[bid]}"
        )

    watch = ("tree_bark", "stone", "tree_side", "cobble")
    print("\nExpected pack blocks:")
    for name in watch:
        bid = name_to_id.get(name)
        print(f"  {name}: pack id={bid}, world count={counts.get(bid, 0) if bid else 0}")

    print("\nTop 20 block ids:")
    for bid, c in counts.most_common(20):
        label = id_to_name.get(bid, "?" if bid < RUNTIME_ID_MIN else "SYNTHETIC")
        print(f"  {bid:5d} {label:32s} {c}")

    return 0 if not runtime else 2


if __name__ == "__main__":
    raise SystemExit(main())
