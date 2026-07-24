#!/usr/bin/env python3
"""Validate models/creatures/<species>/model.gltf and texture refs."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def _validate_skin(gltf: dict, errors: list[str]) -> None:
    skins = gltf.get("skins", [])
    if not skins:
        return
    skin = skins[0]
    joints = skin.get("joints", [])
    ibm = skin.get("inverseBindMatrices")
    if ibm is None:
        errors.append("skin missing inverseBindMatrices")
        return
    accessors = gltf.get("accessors", [])
    if ibm >= len(accessors):
        errors.append(f"inverseBindMatrices accessor {ibm} out of range")
        return
    acc = accessors[ibm]
    joint_count = len(joints)
    mat_count = acc.get("count", 0)
    if mat_count != joint_count:
        errors.append(
            f"inverseBindMatrices count {mat_count} != joint count {joint_count}"
        )


def _validate_animations(gltf: dict, errors: list[str]) -> None:
    anims = gltf.get("animations", [])
    if not anims:
        return
    names = [a.get("name", "") for a in anims]
    if "idle" not in names:
        errors.append("skinned glTF missing idle animation")
    for anim in anims:
        if not anim.get("channels"):
            errors.append(f"animation {anim.get('name')!r} has no channels")


def validate_species(species: str) -> list[str]:
    errors: list[str] = []
    species_dir = ROOT / "models" / "creatures" / species
    cj = species_dir / "creature.json"
    gltf_path = species_dir / "model.gltf"
    bin_path = species_dir / "model.bin"

    if not cj.is_file():
        errors.append("missing creature.json")
        return errors
    creature = json.loads(cj.read_text(encoding="utf-8"))
    backend = creature.get("visual", {}).get("backend")
    if backend != "gltf_skeleton":
        errors.append(f"backend is {backend!r}, expected gltf_skeleton")

    if not gltf_path.is_file():
        errors.append("missing model.gltf")
        return errors

    gltf = json.loads(gltf_path.read_text(encoding="utf-8"))
    if gltf.get("asset", {}).get("version") != "2.0":
        errors.append("asset.version != 2.0")
    if not gltf.get("meshes"):
        errors.append("no meshes")
    if not gltf.get("accessors"):
        errors.append("no accessors")

    buffers = gltf.get("buffers", [])
    if buffers and buffers[0].get("uri") == "model.bin":
        if not bin_path.is_file():
            errors.append("model.bin referenced but missing")
        else:
            expected = buffers[0].get("byteLength", 0)
            actual = bin_path.stat().st_size
            if expected and actual < expected:
                errors.append(f"model.bin size {actual} < byteLength {expected}")

    for img in gltf.get("images", []):
        uri = img.get("uri", "")
        if uri and not uri.startswith("data:"):
            tex = species_dir / uri
            if not tex.is_file():
                errors.append(f"missing image {uri}")

    gltf_block = creature.get("visual", {}).get("gltf", {})
    model = gltf_block.get("model", "model.gltf")
    if model != "model.gltf":
        errors.append(f"visual.gltf.model={model!r}")
    if not gltf_block.get("textures"):
        errors.append("visual.gltf.textures empty")

    _validate_skin(gltf, errors)
    if gltf.get("skins"):
        _validate_animations(gltf, errors)

    return errors


def all_gltf_species() -> list[str]:
    out: list[str] = []
    base = ROOT / "models" / "creatures"
    for p in sorted(base.iterdir()):
        cj = p / "creature.json"
        if not cj.is_file():
            continue
        d = json.loads(cj.read_text(encoding="utf-8"))
        if d.get("visual", {}).get("backend") == "gltf_skeleton":
            out.append(p.name)
    return out


def skinned_species() -> list[str]:
    out: list[str] = []
    for sp in all_gltf_species():
        gltf_path = ROOT / "models" / "creatures" / sp / "model.gltf"
        if not gltf_path.is_file():
            continue
        gltf = json.loads(gltf_path.read_text(encoding="utf-8"))
        if gltf.get("skins"):
            out.append(sp)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--species", help="single species id")
    ap.add_argument("--all", action="store_true", help="all gltf_skeleton species")
    ap.add_argument("--skinned-only", action="store_true", help="only skinned glTF assets")
    args = ap.parse_args()

    if args.species:
        species_list = [args.species]
    elif args.all:
        species_list = all_gltf_species()
    elif args.skinned_only:
        species_list = skinned_species()
    else:
        ap.error("specify --species, --all, or --skinned-only")

    err_count = 0
    for sp in species_list:
        errors = validate_species(sp)
        if errors:
            err_count += 1
            for e in errors:
                print(f"FAIL {sp}: {e}", file=sys.stderr)
        else:
            print(f"OK validate_gltf_creature: {sp}")
    return 1 if err_count else 0


if __name__ == "__main__":
    raise SystemExit(main())
