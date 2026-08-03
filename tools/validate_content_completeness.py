#!/usr/bin/env python3
"""Validate completeness of blocks, objects, and creatures for current systems."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PACKS = REPO / "resource_packs"
OBJECTS = REPO / "objects"
CREATURES = REPO / "models" / "creatures"

RESERVED_NAMES = {"__missing__", "__air__"}
FACE_COUNTS = {6, 12}
PLACEMENT_MODES = {"surface_layer", "vertical_plant"}
CREATURE_ROLES = {"controlled_default", "mob", "bot"}
VISUAL_BACKENDS = {"rigid_voxels", "bone_skeleton", "gltf_skeleton"}
CRITICAL_PREFABS = ("deco_log_pine", "path_cobble_3x3", "campfire_ring")


def err(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)


def warn(msg: str) -> None:
    print(f"WARN: {msg}", file=sys.stderr)


def collect_block_names() -> set[str]:
    names: set[str] = set()
    if not PACKS.is_dir():
        return names
    for pack in PACKS.iterdir():
        blocks_dir = pack / "blocks"
        if not blocks_dir.is_dir():
            continue
        for path in blocks_dir.glob("*.json"):
            try:
                data = json.loads(path.read_text(encoding="utf-8-sig"))
            except (OSError, json.JSONDecodeError):
                continue
            name = data.get("name")
            if isinstance(name, str) and name:
                names.add(name)
    return names


def validate_blocks() -> int:
    errors = 0
    checked = 0
    if not PACKS.is_dir():
        err("missing resource_packs/")
        return 1

    for pack in sorted(PACKS.iterdir()):
        blocks_dir = pack / "blocks"
        if not blocks_dir.is_dir():
            continue
        tex_dir = pack / "textures" / "blocks"
        for path in sorted(blocks_dir.glob("*.json")):
            checked += 1
            try:
                data = json.loads(path.read_text(encoding="utf-8-sig"))
            except json.JSONDecodeError as exc:
                err(f"{path}: {exc}")
                errors += 1
                continue

            name = data.get("name", "")
            if not name:
                err(f"{path}: missing name")
                errors += 1
            elif name in RESERVED_NAMES:
                err(f"{path}: reserved name '{name}'")
                errors += 1

            textures = data.get("textures")
            if not isinstance(textures, list) or len(textures) not in FACE_COUNTS:
                err(f"{path}: textures must have 6 or 12 entries")
                errors += 1
            elif tex_dir.is_dir():
                for stem in textures[:6]:
                    if isinstance(stem, str) and stem:
                        png = tex_dir / f"{stem}.png"
                        if not png.is_file():
                            warn(f"{path}: missing PNG for stem '{stem}'")

            if "hardness" not in data:
                err(f"{path}: missing hardness")
                errors += 1
            else:
                hardness = data["hardness"]
                if not isinstance(hardness, (int, float)) or hardness < 0:
                    err(f"{path}: hardness must be number >= 0")
                    errors += 1

            physics = data.get("physics")
            render = data.get("render") if isinstance(data.get("render"), dict) else {}
            if isinstance(physics, dict):
                preset = physics.get("preset")
                style = render.get("style")
                if preset in ("water", "lava") and style not in (None, "fluid"):
                    warn(f"{path}: fluid preset with style={style!r}")

    print(f"blocks: checked {checked} JSON files")
    return errors


def validate_objects(known_blocks: set[str]) -> int:
    errors = 0
    checked = 0
    found_critical: set[str] = set()
    if not OBJECTS.is_dir():
        err("missing objects/")
        return 1

    for path in sorted(OBJECTS.rglob("*.json")):
        if path.name.startswith("."):
            continue
        checked += 1
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
        except json.JSONDecodeError as exc:
            err(f"{path}: {exc}")
            errors += 1
            continue

        blocks = data.get("blocks")
        if not isinstance(blocks, list) or not blocks:
            err(f"{path}: blocks[] required and non-empty")
            errors += 1
            continue

        for entry in blocks:
            if not isinstance(entry, dict):
                err(f"{path}: block entry must be object")
                errors += 1
                continue
            btype = entry.get("type")
            if not isinstance(btype, str) or not btype:
                err(f"{path}: block entry missing type")
                errors += 1
            elif btype not in known_blocks and "::" not in btype:
                err(f"{path}: unknown block type '{btype}'")
                errors += 1

        placement = data.get("placement")
        if isinstance(placement, dict):
            mode = placement.get("mode")
            if mode is not None and mode not in PLACEMENT_MODES:
                err(f"{path}: invalid placement.mode '{mode}'")
                errors += 1

        stem = path.stem
        name = data.get("name", stem)
        if name in CRITICAL_PREFABS or stem in CRITICAL_PREFABS:
            found_critical.add(name if name in CRITICAL_PREFABS else stem)

    for crit in CRITICAL_PREFABS:
        if crit not in found_critical:
            err(f"missing critical prefab '{crit}'")
            errors += 1

    print(f"objects: checked {checked} JSON files")
    return errors


def validate_creatures() -> int:
    errors = 0
    checked = 0
    if not CREATURES.is_dir():
        err("missing models/creatures/")
        return 1

    for species_dir in sorted(CREATURES.iterdir()):
        if not species_dir.is_dir():
            continue
        # Authoring caches / non-species folders (e.g. _sources).
        if species_dir.name.startswith("_"):
            continue
        path = species_dir / "creature.json"
        if not path.is_file():
            err(f"{species_dir}: missing creature.json")
            errors += 1
            continue
        checked += 1
        try:
            data = json.loads(path.read_text(encoding="utf-8-sig"))
        except json.JSONDecodeError as exc:
            err(f"{path}: {exc}")
            errors += 1
            continue

        cid = data.get("id")
        if not isinstance(cid, str) or not cid:
            err(f"{path}: missing id")
            errors += 1
        elif cid != species_dir.name:
            err(f"{path}: id '{cid}' != folder '{species_dir.name}'")
            errors += 1

        for key in ("display_name", "role"):
            if key not in data:
                err(f"{path}: missing '{key}'")
                errors += 1
        role = data.get("role")
        if isinstance(role, str) and role not in CREATURE_ROLES:
            warn(f"{path}: unusual role '{role}'")

        if "bounds" not in data:
            err(f"{path}: missing bounds")
            errors += 1

        if "locomotion" not in data and "locomotion_archetype" not in data:
            err(f"{path}: missing locomotion or locomotion_archetype")
            errors += 1

        if "vitals" not in data and "attributes" not in data:
            warn(f"{path}: missing vitals/attributes (defaults may apply)")

        visual = data.get("visual")
        if not isinstance(visual, dict):
            err(f"{path}: missing visual object")
            errors += 1
            continue
        backend = visual.get("backend")
        if backend not in VISUAL_BACKENDS:
            err(f"{path}: visual.backend must be one of {sorted(VISUAL_BACKENDS)}")
            errors += 1
            continue

        if backend == "gltf_skeleton":
            gltf = visual.get("gltf") if isinstance(visual.get("gltf"), dict) else {}
            model = gltf.get("model") or visual.get("model")
            if not model:
                err(f"{path}: gltf_skeleton missing model path")
                errors += 1
            else:
                model_path = species_dir / str(model)
                if not model_path.is_file():
                    alt = CREATURES / str(model)
                    if not alt.is_file():
                        warn(f"{path}: gltf model not found at {model}")
        elif backend == "bone_skeleton":
            geom = visual.get("geometry_file") or visual.get("geometry")
            if not geom:
                warn(f"{path}: bone_skeleton without geometry_file")
        elif backend == "rigid_voxels":
            parts = visual.get("parts")
            if not isinstance(parts, list) or not parts:
                # Demo/stub species may use generated meshes without inline parts.
                warn(f"{path}: rigid_voxels without inline parts[]")

    # Pack overlays (soft)
    if PACKS.is_dir():
        for pack in PACKS.iterdir():
            overlay = pack / "creatures"
            if not overlay.is_dir():
                continue
            for species_dir in overlay.iterdir():
                cpath = species_dir / "creature.json"
                if cpath.is_file():
                    try:
                        json.loads(cpath.read_text(encoding="utf-8-sig"))
                    except json.JSONDecodeError as exc:
                        err(f"{cpath}: {exc}")
                        errors += 1

    print(f"creatures: checked {checked} species")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--blocks-only", action="store_true", help="Validate blocks only"
    )
    parser.add_argument(
        "--objects-only", action="store_true", help="Validate objects only"
    )
    parser.add_argument(
        "--creatures-only", action="store_true", help="Validate creatures only"
    )
    args = parser.parse_args()

    only = args.blocks_only or args.objects_only or args.creatures_only
    errors = 0
    known = collect_block_names()

    if not only or args.blocks_only:
        errors += validate_blocks()
    if not only or args.objects_only:
        errors += validate_objects(known)
    if not only or args.creatures_only:
        errors += validate_creatures()

    if errors:
        print(f"validate_content_completeness: FAILED ({errors} errors)")
        return 1
    print("validate_content_completeness: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
