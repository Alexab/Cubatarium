#!/usr/bin/env python3
"""Tree/shrub clustering metrics for saved worlds.

Usage: python tools/forest_metrics.py WORLD_DIR [...] [--json OUT] [--no-gate]

Trees are counted by trunk bases (a log block without a log below), not by
leaf blocks, so the numbers separate "forest" from "noise". Reported:
  trees_per_10k_land   density over land columns (surface above sea level)
  clark_evans_r        mean nearest-neighbour distance / expected for random
                       placement (<1 clustered, ~1 random, >1 regular)
  dispersion_index     variance/mean of tree counts in 32x32 cells (>1 clustered)
  empty_cell_pct       share of land cells (32x32) with no trees (clearings)
  trees_on_steep_pct   trunks whose base has a >=4 block drop within 2 columns
Gates come from worldgen_baseline.json -> "forest".
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

REPO = Path(__file__).resolve().parents[1]
CELL = 32


def is_trunk(name: str) -> bool:
    n = name.lower()
    return (
        n == "mtg_tree"
        or ("tree" in n and ("log" in n or "trunk" in n or "bark" in n))
    ) and "bush" not in n


def metrics_for(world: Path, sea_level: int) -> dict:
    names = build_id_to_name_from_world(world, REPO)
    trunk_ids = {b for b, n in names.items() if is_trunk(n)}
    shrub_ids = {b for b, n in names.items() if "bush_stem" in n.lower()}
    ignored_names = non_terrain_block_names_for_world(world, REPO) | {"lily_pad"}
    ignored = {b for b, n in names.items() if n in ignored_names}

    blocks: dict[tuple[int, int, int], int] = {}
    for path in (world / "chunks").glob("*.cchunk"):
        blocks.update(decode_chunk(path))
    surf: dict[tuple[int, int], int] = {}
    for (x, y, z), b in blocks.items():
        if b == 0 or b in ignored:
            continue
        if y > surf.get((x, z), -(10**9)):
            surf[(x, z)] = y
    land = {k for k, y in surf.items() if y > sea_level}

    bases = []
    shrubs = 0
    for (x, y, z), b in blocks.items():
        if b in trunk_ids and blocks.get((x, y - 1, z), 0) not in trunk_ids:
            bases.append((x, z, y))
        elif b in shrub_ids and blocks.get((x, y - 1, z), 0) not in shrub_ids:
            shrubs += 1
    land_cols = len(land)
    out: dict = {
        "land_columns": land_cols,
        "trees": len(bases),
        "shrubs": shrubs,
    }
    if land_cols == 0:
        return out
    out["trees_per_10k_land"] = 1e4 * len(bases) / land_cols

    pts = np.array([(x, z) for x, z, _ in bases], dtype=float)
    if len(pts) >= 3:
        d = np.sqrt(((pts[:, None, :] - pts[None, :, :]) ** 2).sum(-1))
        np.fill_diagonal(d, np.inf)
        mean_nn = d.min(1).mean()
        expected = 0.5 / np.sqrt(len(pts) / land_cols)
        out["clark_evans_r"] = float(mean_nn / expected)

    cells: dict[tuple[int, int], int] = {}
    for x, z in land:
        cells.setdefault((x // CELL, z // CELL), 0)
    for x, z, _ in bases:
        key = (x // CELL, z // CELL)
        if key in cells:
            cells[key] += 1
    counts = np.array(list(cells.values()), dtype=float)
    if len(counts) > 1 and counts.mean() > 0:
        out["dispersion_index"] = float(counts.var() / counts.mean())
        out["empty_cell_pct"] = float(100 * np.mean(counts == 0))

    steep = 0
    for x, z, y in bases:
        around = [surf.get((x + dx, z + dz)) for dx, dz in ((2, 0), (-2, 0), (0, 2), (0, -2))]
        around = [a for a in around if a is not None]
        if around and (max(around) - min(around + [y - 1])) >= 4:
            steep += 1
    out["trees_on_steep_pct"] = 100 * steep / max(1, len(bases))
    return out


def check(m: dict, gates: dict) -> list[str]:
    fails = []
    def lt(key, gate):
        if gate in gates and key in m and m[key] < gates[gate]:
            fails.append(f"{key}={m[key]:.2f} < {gates[gate]}")
    def gt(key, gate):
        if gate in gates and key in m and m[key] > gates[gate]:
            fails.append(f"{key}={m[key]:.2f} > {gates[gate]}")
    lt("dispersion_index", "dispersion_index_min")
    lt("empty_cell_pct", "empty_cell_pct_min")
    gt("clark_evans_r", "clark_evans_r_max")
    gt("trees_on_steep_pct", "trees_on_steep_pct_max")
    return fails


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("worlds", nargs="+", type=Path)
    ap.add_argument("--json", type=Path)
    ap.add_argument("--no-gate", action="store_true")
    ap.add_argument(
        "--baseline", type=Path, default=REPO / "tools" / "worldgen_baseline.json"
    )
    args = ap.parse_args()
    baseline = json.loads(args.baseline.read_text(encoding="utf-8"))
    gates = baseline.get("forest", {}).get("thresholds", {})
    sea = int(baseline.get("sea_level", 63))
    report, failed = {}, False
    for world in args.worlds:
        m = metrics_for(world, sea)
        fails = [] if args.no_gate else check(m, gates)
        m["gate_failures"] = fails
        report[world.name] = m
        failed |= bool(fails)
        print(f"== {world.name}")
        for k, v in m.items():
            if k != "gate_failures":
                print(f"  {k}: {v:.3f}" if isinstance(v, float) else f"  {k}: {v}")
        print("  GATE:", "FAIL " + "; ".join(fails) if fails else "ok")
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
