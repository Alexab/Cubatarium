#!/usr/bin/env python3
"""Wall-time waterfall audit for manual/autofly perf jsonl."""
from __future__ import annotations

import argparse
import json
import statistics
import sys
from collections import Counter
from pathlib import Path


def load_fly_spikes(path: Path, y_min: float) -> list[dict]:
    rows: list[dict] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if row.get("kind") != "spike":
            continue
        if float(row.get("player_y") or 0) <= y_min:
            continue
        rows.append(row)
    return rows


def dominant_wall_stage(row: dict) -> str:
    wall = float(row.get("wall_ms") or row.get("max_wall_ms") or 0)
    if wall <= 0:
        return "unknown"
    stages = {
        "stream": float(row.get("stream_ms") or 0),
        "emerge": float(row.get("mesh_emerge_ms") or 0),
        "prep": float(
            row.get("prep_refresh_pressure_ms") or row.get("prep_refresh_ms") or 0
        ),
        "mesh_async": float(row.get("mesh_async_ms") or 0),
        "swap": float(row.get("swap_wait_ms") or 0),
        "block_input": float(row.get("block_input_ms") or 0),
    }
    return max(stages, key=stages.get)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("jsonl", type=Path)
    ap.add_argument("--y-min", type=float, default=10.0)
    args = ap.parse_args()
    if not args.jsonl.is_file():
        print(f"FAIL: missing {args.jsonl}", file=sys.stderr)
        return 1

    spikes = load_fly_spikes(args.jsonl, args.y_min)
    if not spikes:
        print("spikes=0")
        return 0

    wall = [float(r.get("wall_ms") or r.get("max_wall_ms") or 0) for r in spikes]
    wall_med = statistics.median(wall)
    eff_fps = 1000.0 / wall_med if wall_med > 0 else 0.0

    keys = [
        "stream_ms",
        "mesh_emerge_ms",
        "prep_refresh_pressure_ms",
        "mesh_async_ms",
        "swap_wait_ms",
        "block_input_ms",
        "world_extra_ms",
    ]
    print(f"spikes={len(spikes)} wall_med={wall_med:.2f} effective_fps={eff_fps:.2f}")
    for k in keys:
        vals = [float(r.get(k) or 0) for r in spikes]
        med = statistics.median(vals)
        share = 100.0 * med / wall_med if wall_med > 0 else 0.0
        print(f"  {k}: med={med:.2f} share={share:.1f}%")

    dom = Counter(dominant_wall_stage(r) for r in spikes)
    print("dominant_wall_stage:")
    for stage, count in dom.most_common():
        pct = 100.0 * count / len(spikes)
        print(f"  {stage}: {count} ({pct:.1f}%)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
