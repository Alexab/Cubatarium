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

GATE_KEYS = (
    "flatness_pct_max",
    "flatness_pct_min",
    "rolling_hill_pct_min",
    "cave_chunk_coverage_pct_min",
    "nn_distance_cv_min",
)


def gate_thresholds(baseline: dict) -> dict:
    all_thresholds = dict(baseline.get("thresholds", {}))
    gates = {key: all_thresholds[key] for key in GATE_KEYS if key in all_thresholds}
    gates["max_height"] = baseline.get("max_height", 128)
    return gates


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
    thresholds = gate_thresholds(baseline)

    metrics = analyze_world(
        args.world_dir,
        args.refs,
        repo_root=REPO,
        spawn_radius=spawn_radius,
        max_height=baseline.get("max_height", 128),
    )
    failures = compare_to_thresholds(metrics, thresholds)

    print(
        f"flatness={metrics.get('flatness_pct', 0):.2f}% "
        f"rolling={metrics.get('rolling_hill_pct', 0):.2f}% "
        f"cave_cov={metrics.get('cave_chunk_coverage_pct', 0):.2f}% "
        f"veg_cv={metrics.get('nn_distance_cv', 0):.3f}"
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
