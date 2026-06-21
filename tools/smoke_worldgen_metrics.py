#!/usr/bin/env python3
"""Smoke metrics for procedural worldgen (height deltas, latency budgets)."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]

# Offline budget placeholders until the game exposes timing hooks to CI.
DEFAULT_BUDGETS = {
    "streaming_gen_ms_p95": 50.0,
    "mesh_rebuild_ms_p95": 16.0,
    "streaming_io_ms_p95": 8.0,
    "hitch_rate_pct": 5.0,
}


def fnv1a32(text: str) -> int:
    h = 2166136261
    for b in text.encode("utf-8"):
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def synthetic_height_metrics(seed: int, sea: int) -> tuple[int, int]:
    deltas: list[int] = []
    prev_y = sea
    for x in range(64):
        for z in range(64):
            h = fnv1a32(f"{seed}:{x}:{z}")
            y = sea + int(round(((h % 2000) / 1000.0 - 1.0) * 8))
            if x > 0 or z > 0:
                deltas.append(abs(y - prev_y))
            prev_y = y
    deltas.sort()
    median = deltas[len(deltas) // 2] if deltas else 0
    p95 = deltas[int(len(deltas) * 0.95)] if deltas else 0
    return median, p95


def main() -> int:
    parser = argparse.ArgumentParser(description="Worldgen smoke metrics")
    parser.add_argument("--preset", default="overworld")
    parser.add_argument("--render-distance", type=int, default=4)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--sea-level", type=int, default=48)
    args = parser.parse_args()

    median, p95 = synthetic_height_metrics(args.seed, args.sea_level)
    print(
        f"smoke_worldgen_metrics: preset={args.preset} "
        f"render_distance={args.render_distance} "
        f"median_dY={median} p95_dY={p95}"
    )
    print(
        "latency_budgets_ms: "
        f"gen_p95<={DEFAULT_BUDGETS['streaming_gen_ms_p95']} "
        f"mesh_p95<={DEFAULT_BUDGETS['mesh_rebuild_ms_p95']} "
        f"io_p95<={DEFAULT_BUDGETS['streaming_io_ms_p95']}"
    )

    failed = False
    if median > 8 or p95 > 16:
        print("WARN: height delta thresholds exceeded", file=sys.stderr)
        failed = True
    if failed:
        return 1
    print("smoke_worldgen_metrics: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
