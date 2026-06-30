#!/usr/bin/env python3
"""Derive creature bounds from rigid_model.json parts (AABB at feet y=0)."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def part_aabb(offset: list[float], size: list[float]) -> tuple[list[float], list[float]]:
    ox, oy, oz = offset
    sx, sy, sz = size
    half = [sx * 0.5, sy * 0.5, sz * 0.5]
    bmin = [ox - half[0], oy - half[1], oz - half[2]]
    bmax = [ox + half[0], oy + half[1], oz + half[2]]
    return bmin, bmax


def merge_aabb(
    acc_min: list[float] | None, acc_max: list[float] | None, bmin: list[float], bmax: list[float]
) -> tuple[list[float], list[float]]:
    if acc_min is None:
        return bmin[:], bmax[:]
    return (
        [min(acc_min[i], bmin[i]) for i in range(3)],
        [max(acc_max[i], bmax[i]) for i in range(3)],
    )


def derive_size(bmin: list[float], bmax: list[float]) -> list[float]:
    return [round(bmax[i] - bmin[i], 4) for i in range(3)]


def vec3_round(v: list[float]) -> list[float]:
    return [round(x, 4) for x in v]


def merge_bounds(derived: dict[str, list[float]], override: dict[str, Any]) -> dict[str, list[float]]:
    out = {k: derived[k][:] for k in ("rest", "max", "min")}
    for key in ("rest", "max", "min"):
        if key in override and isinstance(override[key], list) and len(override[key]) >= 3:
            out[key] = [float(x) for x in override[key][:3]]
    return out


def derive_from_parts(parts: list[dict[str, Any]]) -> dict[str, list[float]]:
    acc_min: list[float] | None = None
    acc_max: list[float] | None = None
    for part in parts:
        offset = part.get("offset", [0, 0, 0])
        size = part.get("size", [0.6, 1.8, 0.6])
        bmin, bmax = part_aabb(offset, size)
        acc_min, acc_max = merge_aabb(acc_min, acc_max, bmin, bmax)
    if acc_min is None or acc_max is None:
        raise ValueError("no parts to derive bounds from")
    rest = derive_size(acc_min, acc_max)
    # Crouch min: same width/depth, reduced height heuristic (85% of rest height).
    min_size = rest[:]
    min_size[1] = round(rest[1] * 0.85, 4)
    return {
        "rest": vec3_round(rest),
        "max": vec3_round(rest),
        "min": vec3_round(min_size),
    }


def process_creature(creature_path: Path, check_only: bool, threshold: float) -> bool:
    data = json.loads(creature_path.read_text(encoding="utf-8"))
    species_dir = creature_path.parent
    visual = data.get("visual", {})
    rigid_model = visual.get("rigid_model")
    if not rigid_model:
        print(f"skip {creature_path}: no visual.rigid_model", file=sys.stderr)
        return True

    model_path = species_dir / rigid_model
    model = json.loads(model_path.read_text(encoding="utf-8"))
    parts = model.get("parts", [])
    derived = derive_from_parts(parts)

    bounds = data.setdefault("bounds", {})
    override = bounds.get("_override", {})
    if not isinstance(override, dict):
        override = {}

    merged = merge_bounds(derived, override)
    old_derived = bounds.get("_derived", {})

    if check_only:
        drift = False
        for key in ("rest", "max", "min"):
            if key in old_derived:
                old = old_derived[key]
                new = derived[key]
                for i in range(3):
                    if abs(old[i] - new[i]) > threshold:
                        print(
                            f"DRIFT {creature_path}: _derived.{key}[{i}] "
                            f"{old[i]} -> {new[i]}"
                        )
                        drift = True
        return not drift

    bounds["_derived"] = derived
    bounds["rest"] = merged["rest"]
    bounds["max"] = merged["max"]
    bounds["min"] = merged["min"]
    if "source" not in bounds:
        bounds["source"] = "parts_aabb"

    creature_path.write_text(
        json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(f"updated {creature_path}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("creatures", nargs="+", type=Path, help="creature.json paths")
    parser.add_argument("--check", action="store_true", help="warn on _derived drift only")
    parser.add_argument("--threshold", type=float, default=0.02)
    args = parser.parse_args()

    ok = True
    for path in args.creatures:
        if not path.exists():
            print(f"missing {path}", file=sys.stderr)
            ok = False
            continue
        if not process_creature(path, args.check, args.threshold):
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
