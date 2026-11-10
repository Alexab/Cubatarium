"""Print proportion ratios from Bedrock reference geometry.

This command is the only reader of models/creatures/_sources/bedrock_geo.
It copies no cubes and no UVs. Production build and publish do not import it.
The printed numbers are edited by hand into the species recipe.
"""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "models" / "creatures" / "_sources" / "bedrock_geo"

# Species that replaced a Bedrock mesh. bot shares the humanoid reference.
SOURCE_FILE = {
    "cow": "cow.geo.json",
    "pig": "pig.geo.json",
    "sheep": "sheep.geo.json",
    "wolf": "wolf.geo.json",
    "fox": "fox.geo.json",
    "chicken": "chicken.geo.json",
    "bunny": "rabbit.geo.json",
    "bee": "bee.geo.json",
    "spider": "spider.geo.json",
    "skeleton": "skeleton.geo.json",
    "zombie": "zombie.geo.json",
    "human": "skeleton.geo.json",
    "bot": "skeleton.geo.json",
    "dolphin": "dolphin.geo.json",
    "squid": "squid.geo.json",
    "tortoise": "turtle.geo.json",
    "trout": "salmon.geo.json",
}


def _bones(data: dict) -> list[dict]:
    if "minecraft:geometry" in data:
        for entry in data["minecraft:geometry"]:
            if entry.get("bones"):
                return entry["bones"]
    for key, value in data.items():
        if str(key).startswith("geometry.") and isinstance(value, dict):
            return value.get("bones", [])
    return []


def _visual_size(cube: dict) -> tuple[float, float, float]:
    size = cube.get("size") or [0, 0, 0]
    w, h, d = (abs(float(size[0])), abs(float(size[1])), abs(float(size[2])))
    rotation = cube.get("rotation") or []
    if rotation and abs(float(rotation[0])) >= 45:
        h, d = d, h
    return w, h, d


def _largest(bones: list[dict], names: tuple[str, ...]) -> tuple[float, float, float] | None:
    best = None
    volume = -1.0
    for bone in bones:
        if bone.get("name") not in names:
            continue
        for cube in bone.get("cubes") or []:
            w, h, d = _visual_size(cube)
            vol = w * h * d
            if vol > volume:
                volume = vol
                best = (w, h, d)
    return best


def measure_species(species_id: str) -> dict:
    path = SOURCES / SOURCE_FILE[species_id]
    bones = _bones(json.loads(path.read_text(encoding="utf-8-sig")))
    body = _largest(bones, ("body", "body0"))
    head = _largest(bones, ("head",))
    leg = _largest(bones, ("leg0", "rightLeg", "leg1"))
    ratios = {}
    if body and body[2]:
        ratios["body_w"] = round(body[0], 1)
        ratios["body_h"] = round(body[1], 1)
        ratios["body_d"] = round(body[2], 1)
        ratios["chest"] = round(body[0] / body[2], 2)
    if body and head and body[2]:
        ratios["snout"] = round(head[2] / body[2], 2)
        ratios["head_w"] = round(head[0] / max(body[0], 1), 2)
    if body and leg and body[0]:
        ratios["legs"] = round(leg[0] / body[0], 2)
        ratios["leg_h"] = round(leg[1], 1)
    return {"source": SOURCE_FILE[species_id], "ratios": ratios}


def main() -> None:
    for species_id in SOURCE_FILE:
        measured = measure_species(species_id)
        bits = " ".join(f"{key}={value}" for key, value in measured["ratios"].items())
        print(f"{species_id:10} {measured['source']:20} {bits}")


if __name__ == "__main__":
    main()
