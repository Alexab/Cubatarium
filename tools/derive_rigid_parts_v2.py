#!/usr/bin/env python3
"""Derive rigid part offset/size from Luanti .b3d and optionally write creature_rigid_parts.yaml."""

from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path

import yaml

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from bake_rigid_creature_textures import (
    assign_vertex_parts,
    load_yaml,
    mesh_bounds,
    vertex_to_block,
)
from b3d_read import load_b3d_vertices
from creature_tier_a import TIER_A_MOBS

RESEARCH_DEFAULT = Path(r"E:/Work/Home/CubatariumTextureResearch")
GRID = 0.25


def snap_grid(value: float) -> float:
    return round(value / GRID) * GRID


def refine_part_from_vertices(
    part: dict,
    verts: list,
    bounds,
    rest: list[float],
    assignments: dict[int, str],
) -> dict:
    pid = part["id"]
    hits = [
        vertex_to_block(v, bounds, rest)
        for i, v in enumerate(verts)
        if assignments.get(i) == pid
    ]
    if len(hits) < 3:
        return part
    xs = [p[0] for p in hits]
    ys = [p[1] for p in hits]
    zs = [p[2] for p in hits]
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    z0, z1 = min(zs), max(zs)
    size = [
        max(GRID, snap_grid(x1 - x0)),
        max(GRID, snap_grid(y1 - y0)),
        max(GRID, snap_grid(z1 - z0)),
    ]
    offset = [
        snap_grid((x0 + x1) / 2),
        snap_grid((y0 + y1) / 2),
        snap_grid((z0 + z1) / 2),
    ]
    out = copy.deepcopy(part)
    out["offset"] = offset
    out["size"] = size
    if out.get("pivot"):
        px, py, pz = out["pivot"]
        out["pivot"] = [
            snap_grid(px if abs(px - part["offset"][0]) > 1e-3 else offset[0]),
            snap_grid(py if abs(py - part["offset"][1]) > 1e-3 else offset[1] + size[1] / 2),
            snap_grid(pz if abs(pz - part["offset"][2]) > 1e-3 else offset[2]),
        ]
    return out


def load_parts_for_species(species_id: str, rigid: dict) -> list[dict]:
    if species_id in rigid and "parts" in rigid[species_id]:
        return copy.deepcopy(rigid[species_id]["parts"])
    creature_path = ROOT / "models" / "creatures" / species_id / "creature.json"
    creature = json.loads(creature_path.read_text(encoding="utf-8"))
    return copy.deepcopy(creature["visual"]["parts"])


def derive_species(
    species_id: str,
    sources: dict,
    rigid: dict,
    research: Path,
    match_margin: float,
    leg_margin: float,
) -> list[dict]:
    spec = sources["species"].get(species_id)
    if not spec or not spec.get("model"):
        raise ValueError(f"{species_id}: no luanti model")
    creature = json.loads(
        (ROOT / "models" / "creatures" / species_id / "creature.json").read_text(encoding="utf-8")
    )
    parts = load_parts_for_species(species_id, rigid)
    rest = creature["bounds"]["rest"]
    verts = load_b3d_vertices(research / spec["model"])
    bounds = mesh_bounds(verts)
    assignments = assign_vertex_parts(
        verts, bounds, rest, parts, match_margin, leg_margin
    )
    return [
        refine_part_from_vertices(p, verts, bounds, rest, assignments) for p in parts
    ]


def compare_parts(creature_parts: list[dict], derived: list[dict], epsilon: float = 0.08) -> tuple[bool, float]:
    by_id = {p["id"]: p for p in derived}
    max_drift = 0.0
    for cp in creature_parts:
        dp = by_id.get(cp["id"])
        if not dp:
            return False, float("inf")
        for i in range(3):
            max_drift = max(max_drift, abs(cp["offset"][i] - dp["offset"][i]))
            max_drift = max(max_drift, abs(cp["size"][i] - dp["size"][i]))
    return max_drift <= epsilon, max_drift


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--research", type=Path, default=RESEARCH_DEFAULT)
    parser.add_argument("--species", action="append")
    parser.add_argument("--tier-a", action="store_true")
    parser.add_argument("--write", action="store_true", help="Update creature_rigid_parts.yaml")
    parser.add_argument(
        "--compare",
        action="store_true",
        help="Exit 1 if derived parts drift from creature.json by more than 0.08 blocks",
    )
    args = parser.parse_args()

    sources = load_yaml(TOOLS / "creature_luanti_sources.yaml")
    maps = load_yaml(TOOLS / "creature_rigid_uv_maps.yaml")
    rigid_path = TOOLS / "creature_rigid_parts.yaml"
    rigid = yaml.safe_load(rigid_path.read_text(encoding="utf-8")) or {}
    match_margin = float(maps.get("uv_match_margin", 0.15))
    leg_margin = float(maps.get("uv_leg_match_margin", 0.35))
    research = args.research.resolve()

    if args.tier_a:
        species_list = list(TIER_A_MOBS)
    elif args.species:
        species_list = args.species
    else:
        species_list = [
            sid for sid, spec in sources["species"].items() if spec.get("model")
        ]

    compare_failures = 0
    for species_id in species_list:
        try:
            parts = derive_species(
                species_id, sources, rigid, research, match_margin, leg_margin
            )
        except (FileNotFoundError, ValueError) as exc:
            print(f"skip {species_id}: {exc}")
            if args.compare:
                compare_failures += 1
            continue
        creature = json.loads(
            (ROOT / "models" / "creatures" / species_id / "creature.json").read_text(encoding="utf-8")
        )
        creature_parts = creature["visual"]["parts"]
        if args.compare:
            ok, drift = compare_parts(creature_parts, parts)
            print(f"compare {species_id}: drift={drift:.3f} {'OK' if ok else 'FAIL'}")
            if not ok:
                compare_failures += 1
        print(f"\n=== {species_id} ({len(parts)} parts) ===")
        for part in parts:
            print(
                f"  {part['id']}: offset={part['offset']} size={part['size']} "
                f"tex={part.get('texture', '')}"
            )
        if args.write:
            rigid[species_id] = {"parts": parts}

    if args.write:
        rigid_path.write_text(
            yaml.safe_dump(rigid, sort_keys=False, allow_unicode=True),
            encoding="utf-8",
        )
        print(f"\nwrote {rigid_path}")
    if args.compare and compare_failures:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
