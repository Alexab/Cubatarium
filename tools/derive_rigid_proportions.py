#!/usr/bin/env python3
"""Print normalized AABB spans from Luanti .b3d meshes for rigid part tuning."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import yaml

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))

from bake_rigid_creature_textures import load_yaml, mesh_bounds, vertex_to_block
from b3d_read import load_b3d_vertices

RESEARCH_DEFAULT = Path(r"E:/Work/Home/CubatariumTextureResearch")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--research", type=Path, default=RESEARCH_DEFAULT)
    parser.add_argument("--species", action="append")
    args = parser.parse_args()

    sources = load_yaml(TOOLS / "creature_luanti_sources.yaml")
    rigid = yaml.safe_load((TOOLS / "creature_rigid_parts.yaml").read_text()) or {}
    research = args.research.resolve()
    species_list = args.species or [
        sid for sid, spec in sources["species"].items() if spec.get("model")
    ]

    for species_id in species_list:
        spec = sources["species"][species_id]
        model = spec.get("model")
        if not model:
            continue
        creature_path = ROOT / "models" / "creatures" / species_id / "creature.json"
        if not creature_path.is_file():
            print(f"skip {species_id}: no creature.json")
            continue
        creature = json.loads(creature_path.read_text(encoding="utf-8"))
        rest = creature["bounds"]["rest"]
        verts = load_b3d_vertices(research / model)
        bounds = mesh_bounds(verts)
        x0, x1, y0, y1, z0, z1 = bounds
        span_x = x1 - x0
        span_y = y1 - y0
        span_z = z1 - z0
        print(f"\n=== {species_id} ({model}) ===")
        print(f"  mesh spans: X={span_x:.2f} Y={span_y:.2f} Z={span_z:.2f}")
        print(f"  bounds.rest: {rest}")
        parts = rigid.get(species_id, {}).get("parts", creature["visual"]["parts"])
        for part in parts:
            pid = part["id"]
            ox, oy, oz = part["offset"]
            sx, sy, sz = part["size"]
            hits = 0
            for v in verts:
                bx, by, bz = vertex_to_block(v, bounds, rest)
                if (
                    abs(bx - ox) <= sx / 2 + 0.05
                    and abs(by - oy) <= sy / 2 + 0.05
                    and abs(bz - oz) <= sz / 2 + 0.05
                ):
                    hits += 1
            print(f"  {pid}: offset={part['offset']} size={part['size']} verts~{hits}")


if __name__ == "__main__":
    main()
