#!/usr/bin/env python3
"""Analyze terrain columns from saved .cchunk files."""

from __future__ import annotations

import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from worldgen_metrics_lib import analyze_world, load_world_columns

REPO = Path(__file__).resolve().parents[1]
TARGET_WORLDS = [
    "World_005",
    "World_006",
    "World_007",
    "World_008",
    "World_009",
]
REFS = REPO / "content" / "worldgen_refs.json"


def compare_worlds(a: dict, b: dict) -> bool:
    if a.get("columns") != b.get("columns"):
        return False
    if a.get("height_mean") != b.get("height_mean"):
        return False
    if a.get("top_block_ids") != b.get("top_block_ids"):
        return False
    return True


def main() -> int:
    worlds_root = REPO / "bin" / "worlds"
    results: dict[str, dict] = {}

    for name in TARGET_WORLDS:
        world_dir = worlds_root / name
        if not world_dir.is_dir():
            print(f"=== {name} (missing) ===")
            continue
        stats = analyze_world(world_dir, REFS, repo_root=REPO)
        results[name] = stats
        print(f"=== {name} seed={stats.get('seed')} ===")
        for key, val in stats.items():
            if key not in ("top_block_ids", "spawn"):
                print(f"  {key}: {val}")
        print(f"  top_block_ids: {stats.get('top_block_ids', [])}")
        spawn = stats.get("spawn", {})
        if spawn:
            print(f"  spawn columns: {spawn.get('columns')}")
            print(f"  spawn height_mean: {spawn.get('height_mean')}")
            slots = spawn.get("surface_slots", {})
            if slots:
                top = sorted(slots.items(), key=lambda kv: -kv[1])[:6]
                print(f"  spawn surface_slots: {top}")

    if "World_006" in results and "World_007" in results:
        if compare_worlds(results["World_006"], results["World_007"]):
            print(
                "\nWorld_006 and World_007 terrain stats are IDENTICAL "
                "(same seed/chunks pattern)."
            )
    if "World_005" in results and "World_006" in results:
        if compare_worlds(results["World_005"], results["World_006"]):
            print("\nWorld_005 and World_006 terrain stats are IDENTICAL.")

    w7_dir = worlds_root / "World_007"
    if w7_dir.is_dir():
        w7 = load_world_columns(w7_dir)
        print("\n=== World_007 spawn patch sample (|x|,|z|<=32) ===")
        sample_heights = []
        sample_tops = Counter()
        for (x, z), (y, bid) in w7.items():
            if abs(x) <= 32 and abs(z) <= 32:
                sample_heights.append(y)
                sample_tops[bid] += 1
        if sample_heights:
            print(f"  columns: {len(sample_heights)}")
            print(f"  height range: {min(sample_heights)}..{max(sample_heights)}")
            print(f"  top blocks: {sample_tops.most_common(6)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
