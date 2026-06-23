#!/usr/bin/env python3
"""Derive creature habitat from Luanti mob .lua files (fly_in, fly, air_damage)."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_RESEARCH = Path("E:/Work/Home/CubatariumTextureResearch")

WATER_TOKENS = (
    "water",
    "river_water",
)


def extract_fly_in_block(text: str) -> str | None:
    m = re.search(r"fly_in\s*=\s*(\{[^}]*\}|\"[^\"]+\")", text, re.DOTALL)
    if m:
        return m.group(1)
    return None


def extract_bool_field(text: str, name: str) -> bool | None:
    m = re.search(rf"\b{name}\s*=\s*(true|false)", text)
    if not m:
        return None
    return m.group(1) == "true"


def extract_air_damage(text: str) -> float | None:
    m = re.search(r"air_damage\s*=\s*([0-9.]+)", text)
    if not m:
        return None
    return float(m.group(1))


LAVA_TOKENS = ("lava",)


def classify_lua(text: str) -> str:
    fly_in_raw = extract_fly_in_block(text)
    fly = extract_bool_field(text, "fly")
    air_damage = extract_air_damage(text)

    tokens: list[str] = []
    if fly_in_raw:
        tokens = re.findall(r"[a-zA-Z0-9_:]+", fly_in_raw.lower())
        tokens = [t for t in tokens if t not in ("default", "mcl_core", "group")]

    has_water = any(any(w in t for w in WATER_TOKENS) for t in tokens)
    has_lava = any(any(w in t for w in LAVA_TOKENS) for t in tokens)
    has_air = any(t == "air" for t in tokens) or (
        fly_in_raw and "air" in fly_in_raw.lower()
    )
    only_lava = bool(tokens) and has_lava and not has_water and not has_air
    only_water = bool(tokens) and has_water and not has_air and not has_lava
    water_and_air = has_water and has_air and not has_lava

    if only_lava:
        return "lava"
    if only_water:
        return "aquatic"
    if water_and_air:
        return "amphibious"
    if has_air or fly is True or (air_damage is not None and air_damage > 0):
        return "aerial"
    return "terrestrial"


def find_lua_files(research_root: Path) -> dict[str, Path]:
    out: dict[str, Path] = {}
    for path in research_root.rglob("*.lua"):
        stem = path.stem.lower()
        if stem in out:
            continue
        out[stem] = path
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--research", type=Path, default=DEFAULT_RESEARCH)
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=ROOT / "tools" / "creature_habitat_map.json",
    )
    args = parser.parse_args()

    if not args.research.is_dir():
        raise SystemExit(f"Research root not found: {args.research}")

    lua_index = find_lua_files(args.research)
    habitats: dict[str, str] = {}
    for species_id, path in sorted(lua_index.items()):
        text = path.read_text(encoding="utf-8", errors="replace")
        habitats[species_id] = classify_lua(text)

    args.output.write_text(
        json.dumps(habitats, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"wrote {len(habitats)} entries to {args.output}")


if __name__ == "__main__":
    main()
