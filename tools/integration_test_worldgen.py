#!/usr/bin/env python3
"""Integration test: headless --create-world + worldgen metrics."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from worldgen_metrics_lib import analyze_world, compare_to_thresholds

REPO = Path(__file__).resolve().parents[1]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def parse_seeds(raw: str | None, fallback: list[int]) -> list[int]:
    if not raw:
        return fallback
    return [int(part.strip()) for part in raw.split(",") if part.strip()]


def run_create(
    exe: Path,
    cwd: Path,
    seed: int,
    world_name: str,
    radius: int,
    report_path: Path,
    timeout: int,
) -> None:
    cmd = [
        str(exe),
        "--console",
        "--create-world",
        "--seed",
        str(seed),
        "--name",
        world_name,
        "--radius-chunks",
        str(radius),
        "--preset",
        "balanced",
        "--report-json",
        str(report_path),
    ]
    print("+", " ".join(cmd))
    subprocess.run(cmd, cwd=cwd, check=True, timeout=timeout)


def main() -> int:
    parser = argparse.ArgumentParser(description="Worldgen integration test")
    parser.add_argument("--exe", type=Path, default=REPO / "bin" / "Cubatarium.exe")
    parser.add_argument("--cwd", type=Path, default=REPO / "bin")
    parser.add_argument(
        "--baseline",
        type=Path,
        default=REPO / "tools" / "worldgen_baseline.json",
    )
    parser.add_argument("--radius-chunks", type=int, default=2)
    parser.add_argument(
        "--seeds",
        type=str,
        default=None,
        help="Comma-separated seeds (default: all reference_seeds from baseline)",
    )
    parser.add_argument(
        "--skip-determinism",
        action="store_true",
        help="Skip second create-world pass per seed (determinism check)",
    )
    parser.add_argument("--timeout", type=int, default=480)
    parser.add_argument("--keep-worlds", action="store_true")
    args = parser.parse_args()

    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    thresholds = dict(baseline.get("thresholds", {}))
    thresholds["max_height"] = baseline.get("max_height", 128)
    seeds = parse_seeds(args.seeds, baseline.get("reference_seeds", [42, 12354, 20240625]))
    refs = REPO / "content" / "worldgen_refs.json"
    spawn_radius = baseline.get("spawn_radius", 48)

    failures: list[str] = []
    for seed in seeds:
        world_name = f"CI_{seed}"
        world_dir = args.cwd / "worlds" / world_name
        report_path = args.cwd / f"create_world_{seed}.json"
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

        report = json.loads(report_path.read_text(encoding="utf-8"))
        if not report.get("success"):
            failures.append(f"seed {seed}: report success=false: {report.get('error')}")
            continue
        if report.get("seed") != seed:
            failures.append(
                f"seed {seed}: world_seed mismatch report={report.get('seed')}"
            )

        metrics = analyze_world(
            world_dir,
            refs,
            repo_root=REPO,
            spawn_radius=spawn_radius,
            max_height=baseline.get("max_height", 128),
        )
        seed_failures = compare_to_thresholds(metrics, thresholds)
        for msg in seed_failures:
            failures.append(f"seed {seed}: {msg}")

        chunk_a = world_dir / "chunks" / "0_0_0.cchunk"
        if not chunk_a.is_file():
            failures.append(f"seed {seed}: missing chunk 0_0_0.cchunk")
            continue

        if not args.skip_determinism:
            world_b = f"CI_{seed}_det"
            world_b_dir = args.cwd / "worlds" / world_b
            if world_b_dir.is_dir():
                shutil.rmtree(world_b_dir, ignore_errors=True)
            run_create(
                args.exe,
                args.cwd,
                seed,
                world_b,
                args.radius_chunks,
                args.cwd / f"create_world_{seed}_det.json",
                args.timeout,
            )
            chunk_b = world_b_dir / "chunks" / "0_0_0.cchunk"
            if chunk_b.is_file() and sha256_file(chunk_a) != sha256_file(chunk_b):
                failures.append(f"seed {seed}: determinism failed for 0_0_0.cchunk")
            if not args.keep_worlds:
                shutil.rmtree(world_b_dir, ignore_errors=True)

        spawn = metrics.get("spawn", {}).get("surface_slots", {})
        print(
            f"seed {seed}: grass={spawn.get('grass', 0):.1f}% "
            f"stone={spawn.get('stone', 0):.1f}% "
            f"trees={metrics.get('spawn_tree_blocks', 0)} "
            f"bush={metrics.get('spawn_bush_common_footprints', 0)} "
            f"logs={metrics.get('spawn_ground_logs', 0)} "
            f"fire={metrics.get('spawn_fire_blocks', 0)} "
            f"gaps={metrics.get('shore_air_gaps', 0)} "
            f"pits={metrics.get('micro_pit_pct', 0):.2f}% "
            f"cliff16={metrics.get('delta_ge_16_pct', 0):.3f}%"
        )

    if failures:
        print("integration_test_worldgen: FAILED", file=sys.stderr)
        for line in failures:
            print(f"  - {line}", file=sys.stderr)
        return 1

    print("integration_test_worldgen: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
