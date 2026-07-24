#!/usr/bin/env python3
"""Validate prefab biome coverage and coarse height smoothness heuristics."""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BASELINE = REPO / "tools" / "worldgen_baseline.json"
PACK_BIOMES = REPO / "content" / "worldgen_packs" / "default" / "biomes"
OBJECT_FEATURES = REPO / "content" / "object_features.json"
HEIGHT_JSON = REPO / "content" / "worldgen_packs" / "default" / "height.json"
CLIMATE_JSON = REPO / "content" / "worldgen_packs" / "default" / "climate.json"

REFERENCE_SEEDS = [12345, 42, 20240625, 1337, 987654321]
SEA_LEVEL = 48
MAX_HEIGHT = 128
SAMPLE_RADIUS = 96
ROUGHNESS = 0.58
AMPLITUDE_BLOCKS = 4.5 * (MAX_HEIGHT / 12.0 if MAX_HEIGHT > 15 else 1.0) * ROUGHNESS

# Legacy single-sided proxy budgets (kept for quick regression signal).
MAX_MEAN_ABS_DELTA_Y = 1.1
MAX_PCT_DELTA_GT_2 = 2.5
MAX_PCT_DELTA_GT_3 = 0.5
MAX_UNDERWATER_CLIFF_COUNT = 120
RAVINE_RARITY = 600
RAVINE_AQUATIC_MAX_DEPTH = 5


def load_thresholds() -> dict:
    if not BASELINE.is_file():
        return {}
    baseline = json.loads(BASELINE.read_text(encoding="utf-8"))
    thresholds = dict(baseline.get("thresholds", {}))
    thresholds["max_height"] = baseline.get("max_height", MAX_HEIGHT)
    smoothness = baseline.get("smoothness_thresholds", {})
    thresholds.update(smoothness)
    return thresholds


def biome_ids() -> list[str]:
    ids: list[str] = []
    for path in sorted(PACK_BIOMES.glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        ids.append(data.get("id", path.stem))
    return ids


def object_biome_coverage() -> tuple[set[str], set[str]]:
    data = json.loads(OBJECT_FEATURES.read_text(encoding="utf-8"))
    covered: set[str] = set()
    for pool in ("vegetation", "ground_cover", "decoration", "structures"):
        for rule in data.get(pool, []):
            for biome in rule.get("biomes", []):
                covered.add(biome)
    return set(biome_ids()), covered


def check_schema_files() -> dict:
    for path in (HEIGHT_JSON, CLIMATE_JSON):
        if not path.is_file():
            raise SystemExit(f"missing required pack file: {path}")
    return json.loads(HEIGHT_JSON.read_text(encoding="utf-8"))


def _hash2d(x: int, z: int, seed: int) -> float:
    h = (x * 374761393 + z * 668265263) ^ seed
    h = (h ^ (h >> 13)) * 1274126177
    h ^= h >> 16
    return (h & 0xFFFFFFFF) / 4294967295.0


def _smoothstep(t: float) -> float:
    t = max(0.0, min(1.0, t))
    return t * t * (3.0 - 2.0 * t)


def _fbm(x: float, z: float, seed: int, octaves: int) -> float:
    value = 0.0
    amplitude = 1.0
    frequency = 1.0
    norm = 0.0
    for i in range(octaves):
        ix = int(math.floor(x * frequency))
        iz = int(math.floor(z * frequency))
        fx = x * frequency - ix
        fz = z * frequency - iz
        v00 = _hash2d(ix, iz, seed + i * 17)
        v10 = _hash2d(ix + 1, iz, seed + i * 17)
        v01 = _hash2d(ix, iz + 1, seed + i * 17)
        v11 = _hash2d(ix + 1, iz + 1, seed + i * 17)
        sx = _smoothstep(fx)
        sz = _smoothstep(fz)
        v0 = v00 * (1.0 - sx) + v10 * sx
        v1 = v01 * (1.0 - sx) + v11 * sx
        sample = v0 * (1.0 - sz) + v1 * sz
        value += sample * amplitude
        norm += amplitude
        amplitude *= 0.5
        frequency *= 2.0
    return value / max(norm, 1e-6)


def layered_height_y(x: int, z: int, seed: int, height_cfg: dict) -> int:
    layers = height_cfg["layers"]
    overworld = height_cfg["overworld"]
    wx = float(x)
    wz = float(z)

    continental = _fbm(
        wx * layers["continental"]["scale"],
        wz * layers["continental"]["scale"],
        seed,
        int(layers["continental"]["octaves"]),
    )
    regional = _fbm(
        wx * layers["regional"]["scale"],
        wz * layers["regional"]["scale"],
        seed + 10,
        int(layers["regional"]["octaves"]),
    )
    detail = _fbm(
        wx * layers["detail"]["scale"],
        wz * layers["detail"]["scale"],
        seed + 20,
        int(layers["detail"]["octaves"]),
    )
    rolling = 0.0
    rolling_layer = layers.get("rolling")
    if rolling_layer:
        rolling = _fbm(
            wx * rolling_layer["scale"],
            wz * rolling_layer["scale"],
            seed + 30,
            int(rolling_layer.get("octaves", 2)),
        )
        rolling *= float(rolling_layer.get("weight", 0.0))
    h01 = (
        layers["continental"]["weight"] * continental
        + layers["regional"]["weight"] * regional
        + layers["detail"]["weight"] * detail
        + rolling
    )
    h01 = max(0.0, min(1.0, h01))
    h01 = h01 ** float(overworld["curve_exponent"])
    delta = (h01 - float(overworld["sea_bias"])) * AMPLITUDE_BLOCKS * 0.35
    return SEA_LEVEL + int(math.floor(delta + 0.5))


def height_smoothness_metrics(seed: int, height_cfg: dict) -> dict:
    deltas: list[int] = []
    for x in range(-SAMPLE_RADIUS, SAMPLE_RADIUS):
        for z in range(-SAMPLE_RADIUS, SAMPLE_RADIUS):
            y = layered_height_y(x, z, seed, height_cfg)
            y_e = layered_height_y(x + 1, z, seed, height_cfg)
            y_n = layered_height_y(x, z + 1, seed, height_cfg)
            deltas.append(abs(y - y_e))
            deltas.append(abs(y - y_n))
    if not deltas:
        return {
            "mean_abs_delta_y": 0.0,
            "pct_delta_gt_2": 0.0,
            "pct_delta_gt_3": 0.0,
            "flatness_pct": 0.0,
            "rolling_hill_pct": 0.0,
            "delta_ge_4_pct": 0.0,
        }
    mean_abs = sum(deltas) / len(deltas)
    pct_gt_2 = 100.0 * sum(1 for d in deltas if d > 2) / len(deltas)
    pct_gt_3 = 100.0 * sum(1 for d in deltas if d > 3) / len(deltas)
    flatness_pct = 100.0 * sum(1 for d in deltas if d == 0) / len(deltas)
    rolling_hill_pct = (
        100.0 * sum(1 for d in deltas if 1 <= d <= 3) / len(deltas)
    )
    delta_ge_4_pct = 100.0 * sum(1 for d in deltas if d >= 4) / len(deltas)
    return {
        "mean_abs_delta_y": mean_abs,
        "pct_delta_gt_2": pct_gt_2,
        "pct_delta_gt_3": pct_gt_3,
        "flatness_pct": flatness_pct,
        "rolling_hill_pct": rolling_hill_pct,
        "delta_ge_4_pct": delta_ge_4_pct,
    }


def ravine_hash(x: int, z: int, seed: int) -> int:
    return ((x * 374761393 + z * 668265263) ^ (seed + 4400)) & 0xFFFFFFFF


def underwater_cliff_proxy_count(seed: int, height_cfg: dict) -> int:
    """Count aquatic columns where ravine gate would carve deeper than aquatic cap."""
    count = 0
    for x in range(-SAMPLE_RADIUS, SAMPLE_RADIUS):
        for z in range(-SAMPLE_RADIUS, SAMPLE_RADIUS):
            surface_y = layered_height_y(x, z, seed, height_cfg)
            if surface_y > SEA_LEVEL + 2:
                continue
            if ravine_hash(x, z, seed) % RAVINE_RARITY != 0:
                continue
            ridge = 1.0 - abs(_fbm(x * 0.004, z * 0.004, seed + 4400, 3) * 2.0 - 1.0)
            path = _fbm(x * 0.015, z * 0.015, seed + 4401, 2)
            combined = ridge * path
            if combined < 0.82:
                continue
            depth_factor = _smoothstep((combined - 0.82) / (0.95 - 0.82))
            carve_depth = 8 + int(depth_factor * (40 - 8))
            if carve_depth > RAVINE_AQUATIC_MAX_DEPTH:
                count += 1
    return count


def check_bidirectional_budgets(metrics: dict, thresholds: dict) -> tuple[list[str], list[str]]:
    failures: list[str] = []
    warnings: list[str] = []

    def fail(msg: str) -> None:
        failures.append(msg)

    def warn(msg: str) -> None:
        warnings.append(msg)

    flatness_max = thresholds.get("flatness_pct_max")
    if flatness_max is not None and metrics["flatness_pct"] > flatness_max:
        warn(
            f"flatness_pct={metrics['flatness_pct']:.2f}% > {flatness_max}% "
            "(proxy; see integration_test_worldgen)"
        )

    flatness_min = thresholds.get("flatness_pct_min")
    if flatness_min is not None and metrics["flatness_pct"] < flatness_min:
        warn(
            f"flatness_pct={metrics['flatness_pct']:.2f}% < {flatness_min}% "
            "(proxy; see integration_test_worldgen)"
        )

    rolling_min = thresholds.get("rolling_hill_pct_min")
    if rolling_min is not None and metrics["rolling_hill_pct"] < rolling_min:
        warn(
            f"rolling_hill_pct={metrics['rolling_hill_pct']:.2f}% < {rolling_min}% "
            "(proxy; see integration_test_worldgen)"
        )

    cliff_max = thresholds.get("delta_ge_4_pct_max")
    if cliff_max is not None and metrics["delta_ge_4_pct"] > cliff_max:
        fail(
            f"delta_ge_4_pct={metrics['delta_ge_4_pct']:.2f}% > {cliff_max}%"
        )

    return failures, warnings


def main() -> int:
    height_cfg = check_schema_files()
    thresholds = load_thresholds()
    mean_abs_max = thresholds.get("mean_abs_delta_y_max", MAX_MEAN_ABS_DELTA_Y)
    pct_gt_2_max = thresholds.get("pct_delta_gt_2_max", MAX_PCT_DELTA_GT_2)
    pct_gt_3_max = thresholds.get("pct_delta_gt_3_max", MAX_PCT_DELTA_GT_3)
    underwater_cliff_max = thresholds.get(
        "max_underwater_cliff_count", MAX_UNDERWATER_CLIFF_COUNT
    )
    all_biomes, covered = object_biome_coverage()
    missing = sorted(all_biomes - covered)
    if missing:
        print(f"WARN: biomes without object rules: {', '.join(missing)}", file=sys.stderr)
        return 1

    failed = False
    bidirectional_failed = False
    for seed in REFERENCE_SEEDS:
        metrics = height_smoothness_metrics(seed, height_cfg)
        underwater_cliffs = underwater_cliff_proxy_count(seed, height_cfg)
        print(
            f"seed={seed}: mean_abs_delta_y={metrics['mean_abs_delta_y']:.3f} "
            f"pct_delta_gt_2={metrics['pct_delta_gt_2']:.2f}% "
            f"pct_delta_gt_3={metrics['pct_delta_gt_3']:.2f}% "
            f"flatness={metrics['flatness_pct']:.2f}% "
            f"rolling_hills={metrics['rolling_hill_pct']:.2f}% "
            f"delta_ge_4={metrics['delta_ge_4_pct']:.2f}% "
            f"underwater_cliffs={underwater_cliffs}"
        )
        if (
            metrics["mean_abs_delta_y"] > mean_abs_max
            or metrics["pct_delta_gt_2"] > pct_gt_2_max
            or metrics["pct_delta_gt_3"] > pct_gt_3_max
            or underwater_cliffs > underwater_cliff_max
        ):
            failed = True
        budget_failures, budget_warnings = check_bidirectional_budgets(
            metrics, thresholds
        )
        if budget_failures:
            bidirectional_failed = True
            for msg in budget_failures:
                print(f"WARN: seed={seed}: {msg}", file=sys.stderr)
        for msg in budget_warnings:
            print(f"NOTE: seed={seed}: {msg}", file=sys.stderr)

    print(
        f"validate_worldgen_smoothness: biomes={len(all_biomes)} "
        f"covered={len(covered)} reference_seeds={REFERENCE_SEEDS} "
        f"backend_proxy=heightmap"
    )
    if failed:
        print(
            f"WARN: legacy smoothness budgets exceeded "
            f"(mean_abs<={mean_abs_max}, pct_gt_2<={pct_gt_2_max}%, "
            f"pct_gt_3<={pct_gt_3_max}%, underwater_cliffs<={underwater_cliff_max})",
            file=sys.stderr,
        )
        return 1
    if bidirectional_failed:
        print("WARN: bidirectional terrain quality budgets exceeded", file=sys.stderr)
        return 1
    print("validate_worldgen_smoothness: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
