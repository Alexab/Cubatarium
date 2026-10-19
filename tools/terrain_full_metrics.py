#!/usr/bin/env python3
"""Whole-area terrain shape metrics for saved worlds (not only the spawn window).

Usage: python tools/terrain_full_metrics.py WORLD_DIR [WORLD_DIR ...]
           [--png-dir DIR] [--json OUT] [--no-gate]

Surface = highest non-air, non-vegetation block per (x, z). Gates come from
tools/worldgen_baseline.json -> "global" and fail the run (exit 1) when exceeded.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from worldgen_metrics_lib import (  # noqa: E402
    build_id_to_name_from_world,
    decode_chunk,
    non_terrain_block_names_for_world,
)
import quick_terrain_metrics as q  # noqa: E402

REPO = Path(__file__).resolve().parents[1]
BAND = 16


def load_height_grid(world: Path):
    cols: dict[tuple[int, int, int], int] = {}
    for path in (world / "chunks").glob("*.cchunk"):
        cols.update(decode_chunk(path))
    id_to_name = build_id_to_name_from_world(world, REPO)
    ignored_names = non_terrain_block_names_for_world(world, REPO) | {"lily_pad"}
    ignored = {b for b, n in id_to_name.items() if n in ignored_names}
    surf: dict[tuple[int, int], int] = {}
    for (x, y, z), bid in cols.items():
        if bid == 0 or bid in ignored:
            continue
        if y > surf.get((x, z), -(10**9)):
            surf[(x, z)] = y
    if not surf:
        return None
    xs = [k[0] for k in surf]
    zs = [k[1] for k in surf]
    x0, z0 = min(xs), min(zs)
    grid = np.full((max(zs) - z0 + 1, max(xs) - x0 + 1), np.nan)
    for (x, z), y in surf.items():
        grid[z - z0, x - x0] = y
    return grid, (x0, z0)


def range5(h: np.ndarray) -> np.ndarray:
    stack = np.stack(
        [h[2:-2, 2:-2], h[:-4, 2:-2], h[4:, 2:-2], h[2:-2, :-4], h[2:-2, 4:]]
    )
    return np.nanmax(stack, 0) - np.nanmin(stack, 0)


def metrics_for(world: Path, sea_level: int, png_dir: Path | None) -> dict:
    loaded = load_height_grid(world)
    if loaded is None:
        return {"error": "no chunks"}
    h, origin = loaded
    valid = h[~np.isnan(h)]
    dx = np.abs(np.diff(h, axis=1))
    dz = np.abs(np.diff(h, axis=0))
    d = np.concatenate([dx[~np.isnan(dx)], dz[~np.isnan(dz)]])
    r5 = range5(h)
    r5 = r5[~np.isnan(r5)]
    reliefs = []
    nz, nx = h.shape
    for zi in range(0, nz - BAND + 1, BAND):
        for xi in range(0, nx - BAND + 1, BAND):
            blk = h[zi : zi + BAND, xi : xi + BAND]
            if not np.isnan(blk).any():
                reliefs.append(blk.max() - blk.min())
    rel = np.array(reliefs) if reliefs else np.zeros(1)
    land = valid > sea_level
    out = {
        "columns": int(valid.size),
        "land_pct": float(100 * land.mean()),
        "height_p5_p50_p95": [float(v) for v in np.percentile(valid, [5, 50, 95])],
        "height_max": float(valid.max()),
        "delta_ge_4_pct": float(100 * np.mean(d >= 4)),
        "delta_ge_8_pct": float(100 * np.mean(d >= 8)),
        "delta_ge_16_pct": float(100 * np.mean(d >= 16)),
        "range5_ge_8_pct": float(100 * np.mean(r5 >= 8)),
        "range5_ge_16_pct": float(100 * np.mean(r5 >= 16)),
        "relief16_p50": float(np.percentile(rel, 50)),
        "relief16_p90": float(np.percentile(rel, 90)),
        "relief16_p99": float(np.percentile(rel, 99)),
        "relief16_max": float(rel.max()),
        # Terracing: share of columns on a sloped region whose axis neighbours
        # are exactly level (ring/step artefacts) -- informational.
        "extent": [origin[0], origin[1], nx, nz],
    }
    if png_dir is not None:
        try:
            from PIL import Image

            png_dir.mkdir(parents=True, exist_ok=True)
            fill = np.nan_to_num(h, nan=np.nanmin(h))
            norm = ((fill - fill.min()) / max(1.0, fill.max() - fill.min()) * 255).astype(
                np.uint8
            )
            gz, gx = np.gradient(fill)
            shade = np.clip(128 + (gx - gz) * 12, 0, 255).astype(np.uint8)
            Image.fromarray(np.stack([norm, shade, shade], -1)).save(
                png_dir / f"{world.name}_hillshade.png"
            )
        except Exception as exc:  # PIL optional
            out["png_error"] = str(exc)
    return out


def check(metrics: dict, gates: dict) -> list[str]:
    fails = []
    mapping = {
        "relief16_p50_max": "relief16_p50",
        "relief16_p90_max": "relief16_p90",
        "relief16_p99_max": "relief16_p99",
        "delta_ge_16_pct_max": "delta_ge_16_pct",
        "delta_ge_8_pct_max": "delta_ge_8_pct",
        "range5_ge_16_pct_max": "range5_ge_16_pct",
        "land_pct_max": "land_pct",
    }
    for gate, key in mapping.items():
        if gate in gates and metrics[key] > gates[gate]:
            fails.append(f"{key}={metrics[key]:.3f} > {gates[gate]}")
    if "land_pct_min" in gates and metrics["land_pct"] < gates["land_pct_min"]:
        fails.append(f"land_pct={metrics['land_pct']:.1f} < {gates['land_pct_min']}")
    return fails


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("worlds", nargs="+", type=Path)
    ap.add_argument("--png-dir", type=Path)
    ap.add_argument("--json", type=Path)
    ap.add_argument("--no-gate", action="store_true")
    ap.add_argument(
        "--baseline", type=Path, default=REPO / "tools" / "worldgen_baseline.json"
    )
    args = ap.parse_args()
    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    gates = baseline.get("global", {}).get("thresholds", {})
    sea = int(baseline.get("sea_level", 63))
    report, failed = {}, False
    for world in args.worlds:
        m = metrics_for(world, sea, args.png_dir)
        fails = [] if args.no_gate or "error" in m else check(m, gates)
        m["gate_failures"] = fails
        report[world.name] = m
        failed |= bool(fails)
        print(f"== {world.name}")
        for k, v in m.items():
            if k not in ("extent", "gate_failures"):
                print(f"  {k}: {v:.3f}" if isinstance(v, float) else f"  {k}: {v}")
        print("  GATE:", "FAIL " + "; ".join(fails) if fails else "ok")
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
