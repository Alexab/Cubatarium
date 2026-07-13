#!/usr/bin/env python3
"""Spawn-radius placement baseline for reference seeds (blocks on disk, not render)."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from quick_terrain_metrics import build_surface, load_columns
from worldgen_metrics_lib import analyze_world, compare_to_thresholds, terrain_shape_stats

REPO = Path(__file__).resolve().parents[1]


def land_shape_metrics(world_dir: Path, spawn_radius: int) -> dict:
    columns = load_columns(world_dir, 0, 0, spawn_radius)
    surface = build_surface(columns, land_only=True)
    if not surface:
        return {}
    ys = [y for y, _ in surface.values()]
    shape = terrain_shape_stats(surface)
    return {
        "land_columns": len(surface),
        "height_min": min(ys),
        "height_mean": sum(ys) / len(ys),
        "height_max": max(ys),
        **shape,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Placement baseline report (world blocks; render/mesh is separate)"
    )
    parser.add_argument(
        "--baseline",
        type=Path,
        default=REPO / "tools" / "worldgen_baseline.json",
    )
    parser.add_argument("--worlds-dir", type=Path, default=REPO / "bin" / "worlds")
    parser.add_argument("--seeds", type=str, default=None)
    parser.add_argument("--json", action="store_true", help="Emit JSON lines only")
    args = parser.parse_args()

    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    seeds = (
        [int(s.strip()) for s in args.seeds.split(",") if s.strip()]
        if args.seeds
        else baseline.get("reference_seeds", [42, 12354, 20240625])
    )
    spawn_radius = baseline.get("spawn_radius", 48)
    refs = REPO / "content" / "worldgen_refs.json"
    thresholds = dict(baseline.get("thresholds", {}))
    thresholds["max_height"] = baseline.get("max_height", 128)

    reports: list[dict] = []
    missing: list[int] = []
    for seed in seeds:
        world_name = f"CI_{seed}"
        world_dir = args.worlds_dir / world_name
        if not world_dir.is_dir():
            missing.append(seed)
            continue

        metrics = analyze_world(
            world_dir,
            refs,
            repo_root=REPO,
            spawn_radius=spawn_radius,
            max_height=baseline.get("max_height", 128),
        )
        land = land_shape_metrics(world_dir, spawn_radius)
        gate_failures = compare_to_thresholds(metrics, thresholds)
        report = {
            "seed": seed,
            "world": world_name,
            "placement_note": (
                "spawn_tree_blocks counts placed voxels in chunk files; "
                "in-game visibility is render/mesh (TD-CS-014), not worldgen"
            ),
            "metrics": metrics,
            "land_spawn_shape": land,
            "gate_failures": gate_failures,
        }
        reports.append(report)

        if not args.json:
            print(f"seed={seed} world={world_name}")
            print(
                f"  placement: trees={metrics.get('spawn_tree_blocks', 0)} "
                f"bush={metrics.get('spawn_bush_common_footprints', 0)} "
                f"logs={metrics.get('spawn_ground_logs', 0)}"
            )
            if land:
                print(
                    f"  land shape: flat={land.get('flatness_pct', 0):.1f}% "
                    f"rolling={land.get('rolling_hill_pct', 0):.1f}% "
                    f"cols={land.get('land_columns', 0)}"
                )
            print(
                f"  full-disk: flat={metrics.get('flatness_pct', 0):.1f}% "
                f"rolling={metrics.get('rolling_hill_pct', 0):.1f}%"
            )
            if gate_failures:
                print("  gate_failures:")
                for line in gate_failures:
                    print(f"    - {line}")
            else:
                print("  gates: OK")

    if args.json:
        for report in reports:
            print(json.dumps(report, ensure_ascii=False))

    if missing:
        print(
            f"worldgen_spawn_report: missing worlds for seeds {missing} "
            f"(run integration_test_worldgen.py first)",
            file=sys.stderr,
        )
        return 1 if not reports else 0

    if not reports:
        print("worldgen_spawn_report: no worlds found", file=sys.stderr)
        return 1

    failed = sum(1 for r in reports if r["gate_failures"])
    if failed:
        print(f"worldgen_spawn_report: {failed}/{len(reports)} seeds failed gates", file=sys.stderr)
        return 1

    if not args.json:
        print(f"worldgen_spawn_report: OK ({len(reports)} seeds)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
