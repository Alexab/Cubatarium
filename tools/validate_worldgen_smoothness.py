#!/usr/bin/env python3
"""Validate prefab biome coverage and coarse height smoothness heuristics."""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
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

# Regression budgets for coarse layered height (Python proxy, not full C++ pipeline).
MAX_MEAN_ABS_DELTA_Y = 1.35
MAX_PCT_DELTA_GT_2 = 4.5


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
    h01 = (
        layers["continental"]["weight"] * continental
        + layers["regional"]["weight"] * regional
        + layers["detail"]["weight"] * detail
    )
    h01 = max(0.0, min(1.0, h01))
    h01 = h01 ** float(overworld["curve_exponent"])
    delta = (h01 - float(overworld["sea_bias"])) * AMPLITUDE_BLOCKS * 0.35
    return SEA_LEVEL + int(math.floor(delta + 0.5))


def height_smoothness_metrics(seed: int, height_cfg: dict) -> tuple[float, float]:
    deltas: list[int] = []
    for x in range(-SAMPLE_RADIUS, SAMPLE_RADIUS):
        for z in range(-SAMPLE_RADIUS, SAMPLE_RADIUS):
            y = layered_height_y(x, z, seed, height_cfg)
            y_e = layered_height_y(x + 1, z, seed, height_cfg)
            y_n = layered_height_y(x, z + 1, seed, height_cfg)
            deltas.append(abs(y - y_e))
            deltas.append(abs(y - y_n))
    if not deltas:
        return 0.0, 0.0
    mean_abs = sum(deltas) / len(deltas)
    pct_gt_2 = 100.0 * sum(1 for d in deltas if d > 2) / len(deltas)
    return mean_abs, pct_gt_2


def main() -> int:
    height_cfg = check_schema_files()
    all_biomes, covered = object_biome_coverage()
    missing = sorted(all_biomes - covered)
    if missing:
        print(f"WARN: biomes without object rules: {', '.join(missing)}", file=sys.stderr)
        return 1

    failed = False
    for seed in REFERENCE_SEEDS:
        mean_abs, pct_gt_2 = height_smoothness_metrics(seed, height_cfg)
        print(
            f"seed={seed}: mean_abs_delta_y={mean_abs:.3f} "
            f"pct_delta_gt_2={pct_gt_2:.2f}%"
        )
        if mean_abs > MAX_MEAN_ABS_DELTA_Y or pct_gt_2 > MAX_PCT_DELTA_GT_2:
            failed = True

    print(
        f"validate_worldgen_smoothness: biomes={len(all_biomes)} "
        f"covered={len(covered)} reference_seeds={REFERENCE_SEEDS}"
    )
    if failed:
        print(
            f"WARN: smoothness budgets exceeded "
            f"(mean_abs<={MAX_MEAN_ABS_DELTA_Y}, pct_gt_2<={MAX_PCT_DELTA_GT_2}%)",
            file=sys.stderr,
        )
        return 1
    print("validate_worldgen_smoothness: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
