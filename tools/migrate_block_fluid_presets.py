#!/usr/bin/env python3
"""Add physics.fluid_permeable and physics.fluid_kind to block JSON when inferable.

Heuristic matches runtime fallback in FluidPermeabilityUtil / FluidKindPresetUtil:
- fluid_kind: render.style == fluid, or preset water/lava, or name hints
- fluid_permeable: cross/cutout with occupancy < 1 (unless already explicit)

Usage:
  python tools/migrate_block_fluid_presets.py --dry-run
  python tools/migrate_block_fluid_presets.py --write
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BLOCKS_ROOT = REPO_ROOT / "resource_packs"


def infer_fluid_kind(data: dict) -> str | None:
    render = data.get("render") or {}
    style = render.get("style", "")
    if style == "fluid":
        name = str(data.get("name", "")).lower()
        if "lava" in name:
            return "lava"
        return "water"
    physics = data.get("physics") or {}
    preset = str(physics.get("preset", "")).lower()
    if preset in ("water", "lava"):
        return preset
    return None


def infer_fluid_permeable(data: dict) -> bool | None:
    physics = data.get("physics") or {}
    if "fluid_permeable" in physics:
        return None
    render = data.get("render") or {}
    style = render.get("style", "")
    if style == "fluid":
        return None
    movement = physics.get("movement") or {}
    occupancy = movement.get("occupancy", 1.0)
    if style in ("cross", "cutout") and occupancy < 1.0:
        return True
    return None


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def migrate_file(path: Path, write: bool) -> tuple[bool, list[str]]:
    data = load_json(path)
    changes: list[str] = []
    physics = data.setdefault("physics", {})

    kind = infer_fluid_kind(data)
    if kind and physics.get("fluid_kind") != kind:
        changes.append(f"fluid_kind={kind}")
        if write:
            physics["fluid_kind"] = kind

    permeable = infer_fluid_permeable(data)
    if permeable is not None:
        changes.append(f"fluid_permeable={str(permeable).lower()}")
        if write:
            physics["fluid_permeable"] = permeable

    if not changes:
        return False, changes

    if write:
        path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n",
                        encoding="utf-8")
    return True, changes


def validate_all() -> int:
    errors: list[str] = []
    for path in sorted(BLOCKS_ROOT.glob("*/blocks/*.json")):
        try:
            data = load_json(path)
        except json.JSONDecodeError as exc:
            errors.append(f"{path}: invalid JSON: {exc}")
            continue
        name = data.get("name", "")
        if not name:
            errors.append(f"{path}: missing name")
            continue
        textures = data.get("textures", [])
        if not isinstance(textures, list) or len(textures) < 6:
            errors.append(f"{path}: textures array must have 6 entries")
            continue
        if not all(isinstance(t, str) for t in textures[:6]):
            errors.append(f"{path}: texture stems must be strings")
    if errors:
        for line in errors[:50]:
            print(line)
        if len(errors) > 50:
            print(f"... and {len(errors) - 50} more")
        print(f"validate: FAILED ({len(errors)} error(s))")
        return 1
    print("validate: OK")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--dry-run", action="store_true")
    group.add_argument("--write", action="store_true")
    group.add_argument("--validate", action="store_true",
                       help="Check all block JSON files parse and have 6 texture stems")
    args = parser.parse_args()

    if args.validate:
        return validate_all()

    touched = 0
    for path in sorted(BLOCKS_ROOT.glob("*/blocks/*.json")):
        changed, changes = migrate_file(path, write=args.write)
        if changed:
            touched += 1
            rel = path.relative_to(REPO_ROOT)
            print(f"{rel}: {', '.join(changes)}")

    mode = "updated" if args.write else "would update"
    print(f"{mode} {touched} block definition(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
