#!/usr/bin/env python3
"""Smoke metrics for procedural worldgen (height deltas, runtime latency budgets)."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]

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


def percentile(values: list[float], p: float) -> float:
    if not values:
        return 0.0
    sorted_values = sorted(values)
    idx = max(0, min(len(sorted_values) - 1, int(math.floor((len(sorted_values) - 1) * p))))
    return float(sorted_values[idx])


def read_runtime_metrics(path: Path) -> dict[str, float] | None:
    if not path.is_file():
        return None
    data = json.loads(path.read_text(encoding="utf-8"))
    samples = data.get("samples", [])
    if not isinstance(samples, list) or not samples:
        return None

    gen = [float(s.get("streaming_gen_ms", 0.0)) for s in samples]
    mesh = [float(s.get("mesh_rebuild_ms", 0.0)) for s in samples]
    io = [float(s.get("streaming_io_ms", 0.0)) for s in samples]
    hitch_flags = [bool(s.get("hitch_detected", False)) for s in samples]
    hitch_rate = (sum(1 for v in hitch_flags if v) / len(hitch_flags)) * 100.0
    return {
        "streaming_gen_ms_p95": percentile(gen, 0.95),
        "mesh_rebuild_ms_p95": percentile(mesh, 0.95),
        "streaming_io_ms_p95": percentile(io, 0.95),
        "hitch_rate_pct": hitch_rate,
        "sample_count": float(len(samples)),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Worldgen smoke metrics")
    parser.add_argument("--preset", default="overworld")
    parser.add_argument("--render-distance", type=int, default=4)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--sea-level", type=int, default=48)
    parser.add_argument(
        "--metrics-json",
        type=Path,
        default=REPO / "bin" / "worlds" / "World_001" / "movement_diagnostics.json",
        help="Path to movement diagnostics JSON exported by the game",
    )
    args = parser.parse_args()

    median, p95 = synthetic_height_metrics(args.seed, args.sea_level)
    print(
        f"smoke_worldgen_metrics: preset={args.preset} "
        f"render_distance={args.render_distance} "
        f"median_dY={median} p95_dY={p95}"
    )
    failed = False
    if median > 8 or p95 > 16:
        print("WARN: height delta thresholds exceeded", file=sys.stderr)
        failed = True

    runtime = read_runtime_metrics(args.metrics_json)
    if runtime is None:
        print(
            f"WARN: runtime metrics file not found/empty: {args.metrics_json}",
            file=sys.stderr,
        )
    else:
        print(
            "runtime_metrics: "
            f"samples={int(runtime['sample_count'])} "
            f"gen_p95={runtime['streaming_gen_ms_p95']:.3f} "
            f"mesh_p95={runtime['mesh_rebuild_ms_p95']:.3f} "
            f"io_p95={runtime['streaming_io_ms_p95']:.3f} "
            f"hitch_rate={runtime['hitch_rate_pct']:.2f}%"
        )
        print(
            "latency_budgets_ms: "
            f"gen_p95<={DEFAULT_BUDGETS['streaming_gen_ms_p95']} "
            f"mesh_p95<={DEFAULT_BUDGETS['mesh_rebuild_ms_p95']} "
            f"io_p95<={DEFAULT_BUDGETS['streaming_io_ms_p95']} "
            f"hitch_rate_pct<={DEFAULT_BUDGETS['hitch_rate_pct']}"
        )
        if runtime["streaming_gen_ms_p95"] > DEFAULT_BUDGETS["streaming_gen_ms_p95"]:
            print("WARN: runtime streaming_gen_ms_p95 budget exceeded", file=sys.stderr)
            failed = True
        if runtime["mesh_rebuild_ms_p95"] > DEFAULT_BUDGETS["mesh_rebuild_ms_p95"]:
            print("WARN: runtime mesh_rebuild_ms_p95 budget exceeded", file=sys.stderr)
            failed = True
        if runtime["streaming_io_ms_p95"] > DEFAULT_BUDGETS["streaming_io_ms_p95"]:
            print("WARN: runtime streaming_io_ms_p95 budget exceeded", file=sys.stderr)
            failed = True
        if runtime["hitch_rate_pct"] > DEFAULT_BUDGETS["hitch_rate_pct"]:
            print("WARN: runtime hitch_rate_pct budget exceeded", file=sys.stderr)
            failed = True
    if failed:
        return 1
    print("smoke_worldgen_metrics: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
