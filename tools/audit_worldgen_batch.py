#!/usr/bin/env python3
"""Batch worldgen audit: generate worlds for many seeds and aggregate terrain metrics."""

from __future__ import annotations

import argparse
import json
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from integration_test_worldgen import parse_seeds, run_create
from worldgen_metrics_lib import analyze_world, compare_to_thresholds

REPO = Path(__file__).resolve().parents[1]

DEFAULT_SEEDS = [
    42,
    12345,
    1337,
    20240625,
    987654321,
    7,
    314159,
    271828,
    161803,
    9001,
]

AGGREGATE_KEYS = (
    "height_mean",
    "delta_mean",
    "delta_ge_4_pct",
    "delta_ge_8_pct",
    "flatness_pct",
    "rolling_hill_pct",
    "plateau_edge_pct",
    "cave_air_volume",
    "max_cave_depth_below_surface",
    "cave_chunk_coverage_pct",
    "ground_cover_density",
    "nn_distance_cv",
    "local_density_variance",
    "micro_pit_pct",
)


def aggregate_metrics(per_seed: dict[int, dict]) -> dict:
    summary: dict[str, dict] = {}
    for key in AGGREGATE_KEYS:
        values = [
            float(metrics[key])
            for metrics in per_seed.values()
            if key in metrics and metrics[key] is not None
        ]
        if not values:
            continue
        summary[key] = {
            "min": min(values),
            "max": max(values),
            "mean": statistics.mean(values),
            "median": statistics.median(values),
        }
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description="Batch audit procedural worldgen")
    parser.add_argument("--exe", type=Path, default=REPO / "bin" / "Cubatarium.exe")
    parser.add_argument("--cwd", type=Path, default=REPO / "bin")
    parser.add_argument(
        "--baseline",
        type=Path,
        default=REPO / "tools" / "worldgen_baseline.json",
    )
    parser.add_argument("--radius-chunks", type=int, default=4)
    parser.add_argument("--seeds", type=str, default=None)
    parser.add_argument("--timeout", type=int, default=480)
    parser.add_argument(
        "--output",
        type=Path,
        default=REPO / "tools" / "worldgen_audit_report.json",
    )
    parser.add_argument("--keep-worlds", action="store_true")
    parser.add_argument(
        "--skip-generate",
        action="store_true",
        help="Analyze existing worlds in cwd/worlds/AUDIT_<seed>",
    )
    args = parser.parse_args()

    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    thresholds = dict(baseline.get("thresholds", {}))
    thresholds["max_height"] = baseline.get("max_height", 128)
    seeds = parse_seeds(args.seeds, DEFAULT_SEEDS)
    refs = REPO / "content" / "worldgen_refs.json"
    spawn_radius = baseline.get("spawn_radius", 48)

    per_seed: dict[int, dict] = {}
    failures: list[str] = []

    for seed in seeds:
        world_name = f"AUDIT_{seed}"
        world_dir = args.cwd / "worlds" / world_name
        report_path = args.cwd / f"audit_world_{seed}.json"

        if not args.skip_generate:
            if world_dir.is_dir() and not args.keep_worlds:
                shutil.rmtree(world_dir, ignore_errors=True)
            try:
                run_create(
                    args.exe,
                    args.cwd,
                    seed,
                    world_name,
                    args.radius_chunks,
                    report_path,
                    args.timeout,
                )
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
                failures.append(f"seed {seed}: create-world failed: {exc}")
                continue

        if not world_dir.is_dir():
            failures.append(f"seed {seed}: missing world dir {world_dir}")
            continue

        metrics = analyze_world(
            world_dir,
            refs,
            repo_root=REPO,
            spawn_radius=spawn_radius,
            max_height=baseline.get("max_height", 128),
        )
        per_seed[seed] = metrics
        seed_failures = compare_to_thresholds(metrics, thresholds)
        for msg in seed_failures:
            failures.append(f"seed {seed}: {msg}")

        print(
            f"seed {seed}: flat={metrics.get('flatness_pct', 0):.1f}% "
            f"rolling={metrics.get('rolling_hill_pct', 0):.1f}% "
            f"cliff4={metrics.get('delta_ge_4_pct', 0):.2f}% "
            f"cave_depth={metrics.get('max_cave_depth_below_surface', 0)} "
            f"cave_cov={metrics.get('cave_chunk_coverage_pct', 0):.1f}% "
            f"nn_cv={metrics.get('nn_distance_cv', 0):.2f}"
        )

        if not args.keep_worlds and not args.skip_generate:
            shutil.rmtree(world_dir, ignore_errors=True)

    report = {
        "seeds": seeds,
        "radius_chunks": args.radius_chunks,
        "spawn_radius": spawn_radius,
        "per_seed": {str(seed): metrics for seed, metrics in per_seed.items()},
        "aggregate": aggregate_metrics(per_seed),
        "failures": failures,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"wrote {args.output}")

    if failures:
        print("audit_worldgen_batch: FAILED", file=sys.stderr)
        for line in failures:
            print(f"  - {line}", file=sys.stderr)
        return 1

    print("audit_worldgen_batch: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
