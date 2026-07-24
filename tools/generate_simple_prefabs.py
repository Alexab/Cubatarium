#!/usr/bin/env python3
"""Generate procedural CC0 prefab JSON files."""

from __future__ import annotations

import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "prefabs"


def write_prefab(name: str, category: str, display: str, blocks: list[dict]) -> None:
    merged: dict[tuple[int, int, int], str] = {}
    for block in blocks:
        key = (block["dx"], block["dy"], block["dz"])
        merged[key] = block["type"]
    deduped = [
        {"dx": dx, "dy": dy, "dz": dz, "type": btype}
        for (dx, dy, dz), btype in sorted(merged.items())
    ]
    data = {
        "name": name,
        "version": 2,
        "category": category,
        "displayName": display,
        "source": {"mod": "cubatarium/procedural", "license": "CC0"},
        "anchor": [0, 0, 0],
        "blocks": deduped,
    }
    path = OUT / f"{name}.json"
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {path} ({len(deduped)} blocks)")


def box(x0, y0, z0, x1, y1, z1, block: str) -> list[dict]:
    out = []
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            for z in range(z0, z1 + 1):
                out.append({"dx": x, "dy": y, "dz": z, "type": block})
    return out


def hollow_box(x0, y0, z0, x1, y1, z1, block: str) -> list[dict]:
    out = []
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            for z in range(z0, z1 + 1):
                if x in (x0, x1) or y in (y0, y1) or z in (z0, z1):
                    out.append({"dx": x, "dy": y, "dz": z, "type": block})
    return out


def house_wood_small() -> None:
    blocks = hollow_box(-2, 0, -2, 2, 3, 2, "wood")
    blocks += [{"dx": 0, "dy": 1, "dz": 2, "type": "wooden_door"}]
    blocks += [{"dx": -1, "dy": 2, "dz": 2, "type": "glass"}, {"dx": 1, "dy": 2, "dz": 2, "type": "glass"}]
    write_prefab("house_wood_small", "building", "Small Wood House", blocks)


def house_stone_cottage() -> None:
    blocks = hollow_box(-3, 0, -3, 3, 4, 3, "stone")
    blocks += [{"dx": 0, "dy": 1, "dz": 3, "type": "wooden_door"}]
    for x in (-1, 1):
        blocks.append({"dx": x, "dy": 3, "dz": 3, "type": "glass"})
    blocks += box(-3, 5, -3, 3, 5, 3, "wood")
    write_prefab("house_stone_cottage", "building", "Stone Cottage", blocks)


def well_stone() -> None:
    blocks = []
    for x in (-1, 0, 1):
        for z in (-1, 0, 1):
            if abs(x) + abs(z) <= 2:
                blocks.append({"dx": x, "dy": 0, "dz": z, "type": "stone"})
    blocks += [
        {"dx": -1, "dy": 1, "dz": 0, "type": "stone"},
        {"dx": 1, "dy": 1, "dz": 0, "type": "stone"},
        {"dx": 0, "dy": 1, "dz": -1, "type": "stone"},
        {"dx": 0, "dy": 1, "dz": 1, "type": "stone"},
        {"dx": 0, "dy": 2, "dz": 0, "type": "wood"},
    ]
    write_prefab("well_stone", "building", "Stone Well", blocks)


def tower_stone() -> None:
    blocks = hollow_box(-2, 0, -2, 2, 10, 2, "stone")
    blocks += box(-1, 11, -1, 1, 11, 1, "stonebricksmooth")
    write_prefab("tower_stone", "building", "Stone Tower", blocks)


def shed_wood() -> None:
    blocks = hollow_box(-1, 0, -1, 1, 2, 1, "wood")
    blocks.append({"dx": 0, "dy": 1, "dz": 1, "type": "wooden_door"})
    write_prefab("shed_wood", "building", "Wood Shed", blocks)


def campfire_ring() -> None:
    blocks = []
    for x, z in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
        blocks.append({"dx": x, "dy": 0, "dz": z, "type": "stone"})
    blocks.append({"dx": 0, "dy": 1, "dz": 0, "type": "fire"})
    write_prefab("campfire_ring", "decorative", "Campfire Ring", blocks)


def obelisk_stone() -> None:
    blocks = box(-1, 0, -1, 1, 7, 1, "stone")
    blocks += box(-1, 8, -1, 1, 8, 1, "stonebricksmooth")
    write_prefab("obelisk_stone", "decorative", "Stone Obelisk", blocks)


def path_cobble_3x3() -> None:
    blocks = box(-1, 0, -1, 1, 0, 1, "stone")
    write_prefab("path_cobble_3x3", "decorative", "Cobble Path", blocks)


def pillar_sandstone() -> None:
    blocks = box(0, 0, 0, 0, 5, 0, "sandstone")
    write_prefab("pillar_sandstone", "decorative", "Sandstone Pillar", blocks)


def wall_ruin_segment() -> None:
    blocks = box(-2, 0, 0, 2, 2, 0, "stone")
    blocks.append({"dx": 0, "dy": 1, "dz": 0, "type": "stonebricksmooth_cracked"})
    write_prefab("wall_ruin_segment", "decorative", "Ruin Wall Segment", blocks)


def farm_plot_small() -> None:
    blocks = box(-2, -1, -2, 2, -1, 2, "dirt")
    blocks += box(-1, 0, -1, 1, 0, 1, "wheat")
    write_prefab("farm_plot_small", "decorative", "Small Farm Plot", blocks)


def bridge_wood_small() -> None:
    blocks = box(-2, 0, -4, 2, 0, 4, "wood")
    blocks += [{"dx": -2, "dy": -1, "dz": z, "type": "wood"} for z in range(-4, 5, 4)]
    blocks += [{"dx": 2, "dy": -1, "dz": z, "type": "wood"} for z in range(-4, 5, 4)]
    write_prefab("bridge_wood_small", "building", "Small Wood Bridge", blocks)


def main() -> int:
    house_wood_small()
    house_stone_cottage()
    well_stone()
    tower_stone()
    shed_wood()
    campfire_ring()
    obelisk_stone()
    path_cobble_3x3()
    pillar_sandstone()
    wall_ruin_segment()
    farm_plot_small()
    bridge_wood_small()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
