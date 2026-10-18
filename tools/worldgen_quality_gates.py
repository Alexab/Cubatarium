#!/usr/bin/env python3
"""Phase-3 worldgen quality gates: flatness, cave coverage, vegetation clustering."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from worldgen_metrics_lib import analyze_world, compare_to_thresholds

REPO = Path(__file__).resolve().parents[1]

def gate_thresholds(baseline: dict, backend: str | None = None) -> dict:
    thresholds = dict(baseline.get("thresholds", {}))
    if backend:
        thresholds.update(
            baseline.get("backend_thresholds", {}).get(backend, {})
        )
    thresholds["max_height"] = baseline.get("max_height", 319)
    return thresholds


def main() -> int:
    parser = argparse.ArgumentParser(description="Worldgen phase-3 quality gates")
    parser.add_argument("world_dir", type=Path, help="Path to generated world folder")
    parser.add_argument(
        "--baseline",
        type=Path,
        default=REPO / "tools" / "worldgen_baseline.json",
    )
    parser.add_argument(
        "--refs",
        type=Path,
        default=REPO / "content" / "worldgen_refs.json",
    )
    parser.add_argument("--spawn-radius", type=int, default=None)
    args = parser.parse_args()

    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    spawn_radius = args.spawn_radius or baseline.get("spawn_radius", 48)
    world_meta_path = args.world_dir / "world_data.json"
    world_meta = (
        json.loads(world_meta_path.read_text(encoding="utf-8"))
        if world_meta_path.is_file()
        else {}
    )
    backend = str(world_meta.get("procedural", {}).get("terrain_backend", ""))
    thresholds = gate_thresholds(baseline, backend)

    metrics = analyze_world(
        args.world_dir,
        args.refs,
        repo_root=REPO,
        spawn_radius=spawn_radius,
        max_height=baseline.get("max_height", 319),
    )
    failures = compare_to_thresholds(metrics, thresholds)

    print(
        f"flatness={metrics.get('spawn_flatness_pct', metrics.get('flatness_pct', 0)):.2f}% "
        f"rolling={metrics.get('spawn_rolling_hill_pct', metrics.get('rolling_hill_pct', 0)):.2f}% "
        f"cave_cov={metrics.get('cave_chunk_coverage_pct', 0):.2f}% "
        f"veg_cv={metrics.get('nn_distance_cv', 0):.3f} "
        f"trees={metrics.get('spawn_tree_blocks', 0)} "
        f"(spawn land; placement blocks)"
    )

    if failures:
        print("worldgen_quality_gates: FAILED", file=sys.stderr)
        for line in failures:
            print(f"  - {line}", file=sys.stderr)
        return 1

    print("worldgen_quality_gates: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
