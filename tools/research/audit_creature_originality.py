"""Read-only asset inventory for the original-creature redesign research.

Writes a report only when --output is supplied. Reference matches are exact
local file matches, not an originality or licensing verdict.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def png_exists(directory: Path, stem: str) -> bool:
    return bool(stem) and (directory / f"{stem}.png").is_file()


def resolve_species_texture(species_id: str, species_dir: Path, stem: str, default_stem: str) -> dict:
    """Mirror ResolveCreatureSpeciesTexture file keys, without skin."""
    order: list[str] = []
    for candidate in (stem, "diffuse", default_stem, "body"):
        if candidate and candidate not in order:
            order.append(candidate)
    textures = species_dir / "textures"
    for candidate in order:
        if png_exists(textures, candidate):
            return {
                "requested_stem": stem,
                "resolved_stem": candidate,
                "resolved_key": f"{species_id}/{candidate}",
                "fell_back": candidate != stem,
            }
    return {
        "requested_stem": stem,
        "resolved_stem": None,
        "resolved_key": None,
        "fell_back": False,
    }


def resolve_skinned_texture(species_id: str, species_dir: Path, stem: str,
                            default_stem: str, skin_id: str) -> dict:
    skin_textures = ROOT / "models" / "skins" / skin_id / "textures"
    if png_exists(skin_textures, stem):
        return {
            "requested_stem": stem,
            "resolved_stem": stem,
            "resolved_key": f"skin/{skin_id}/{stem}",
            "fell_back": False,
        }
    if png_exists(skin_textures, "diffuse"):
        return {
            "requested_stem": stem,
            "resolved_stem": "diffuse",
            "resolved_key": f"skin/{skin_id}/diffuse",
            "fell_back": stem != "diffuse",
        }
    base = resolve_species_texture(species_id, species_dir, stem, default_stem)
    base["skin_id"] = skin_id
    return base


def texture_record(path: Path) -> dict:
    with Image.open(path) as image:
        rgba = image.convert("RGBA")
        pixel_hash = hashlib.sha256(rgba.tobytes()).hexdigest()
        size = list(image.size)
    return {
        "path": path.relative_to(ROOT).as_posix(),
        "size": size,
        "sha256": digest(path),
        "rgba_sha256": pixel_hash,
    }


def inventory() -> dict:
    sources = ROOT / "models/creatures/_sources/bedrock_geo"
    reference_hashes: dict[str, list[str]] = {}
    for path in sorted(sources.glob("*.json")):
        reference_hashes.setdefault(digest(path), []).append(
            path.relative_to(ROOT).as_posix()
        )
    rows = []
    for path in sorted((ROOT / "models/creatures").glob("*/creature.json")):
        data = read_json(path)
        visual = data.get("visual", {})
        species_dir = path.parent
        gltf_path = species_dir / visual.get("gltf", {}).get("model", "model.gltf")
        gltf = read_json(gltf_path) if gltf_path.is_file() else {}
        geo_path = species_dir / visual.get("geometry_file", "geometry.geo.json")
        geo = read_json(geo_path) if geo_path.is_file() else {}
        textures = [texture_record(p) for p in sorted((species_dir / "textures").glob("*.png"))]
        stems = {Path(t["path"]).stem for t in textures}
        backend = visual.get("backend")
        required = set()
        if visual.get("sprite", {}).get("billboard"):
            required.add(visual.get("default_texture", "body"))
        elif backend == "bone_skeleton":
            required.add(visual.get("texture", "diffuse"))
        elif backend == "gltf_skeleton":
            # The current loader uses material.name as the texture stem.
            for mesh in gltf.get("meshes", []):
                for primitive in mesh.get("primitives", []):
                    index = primitive.get("material", -1)
                    materials = gltf.get("materials", [])
                    required.add(materials[index].get("name", "body") if 0 <= index < len(materials) else "body")
        else:
            parts = visual.get("parts", [])
            rigid_file = visual.get("rigid_model")
            if rigid_file and (species_dir / rigid_file).is_file():
                parts = read_json(species_dir / rigid_file).get("parts", [])
            required.update(p.get("texture", visual.get("default_texture", "body")) for p in parts)
        geo_ids = [entry.get("description", {}).get("identifier") for entry in geo.get("minecraft:geometry", [])]
        geo_ids.extend(k for k in geo if k.startswith("geometry."))
        license_path = species_dir / "LICENSE.txt"
        species_id = data.get("id", species_dir.name)
        default_stem = visual.get("default_texture") or "body"
        billboard = bool(visual.get("sprite", {}).get("billboard"))
        legacy_parts_ignored = backend in ("bone_skeleton", "gltf_skeleton") or billboard
        effective = [
            resolve_species_texture(species_id, species_dir, stem, default_stem)
            for stem in sorted(required)
        ]
        rows.append({
            "id": species_id,
            "backend": backend,
            "effective_sprite": billboard,
            "spawnable": bool(data.get("catalog", {}).get("spawnable")),
            "role": data.get("role"),
            "habitat": data.get("habitat"),
            "catalog": data.get("catalog", {}),
            "bounds": data.get("bounds", {}),
            "creature_sha256": digest(path),
            "geometry_ids": geo_ids,
            "geometry_exact_source_matches": reference_hashes.get(digest(geo_path), []) if geo_path.is_file() else [],
            "geometry_sha256": digest(geo_path) if geo_path.is_file() else None,
            "gltf_sha256": digest(gltf_path) if gltf_path.is_file() else None,
            "skinned": bool(gltf.get("skins")),
            "max_joints": max((len(s.get("joints", [])) for s in gltf.get("skins", [])), default=0),
            "clips": [a.get("name") for a in gltf.get("animations", [])],
            "required_runtime_texture_stems": sorted(required),
            "missing_runtime_texture_stems": sorted(required - stems),
            "legacy_parts_ignored": legacy_parts_ignored,
            "legacy_part_count": len(visual.get("parts") or []),
            "effective_textures": effective,
            "textures": textures,
            "license_text": license_path.read_text(encoding="utf-8-sig") if license_path.is_file() else None,
        })
    species_by_id = {row["id"]: row for row in rows}
    skins = []
    for path in sorted((ROOT / "models/skins").glob("*/skin.json")):
        definition = read_json(path)
        creature_id = definition.get("creature_id")
        species = species_by_id.get(creature_id, {})
        stems = species.get("required_runtime_texture_stems") or ["diffuse"]
        species_dir = ROOT / "models" / "creatures" / (creature_id or "")
        skins.append({
            "id": path.parent.name,
            "definition": definition,
            "textures": [texture_record(p) for p in sorted((path.parent / "textures").glob("*.png"))],
            "effective_textures": [
                resolve_skinned_texture(creature_id or "", species_dir, stem, "body", path.parent.name)
                for stem in stems
            ],
        })
    example_pack = ROOT / "resource_packs" / "_example_creature_demo"
    return {
        "schema": "creature-originality-baseline.v2",
        "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "scope": "Source asset definitions plus file-level fallback and skin resolution. Does not launch the desktop app, does not judge art, and does not treat legacy visual.parts as bone or glTF textures.",
        "summary": {
            "species": len(rows),
            "backends": dict(sorted(Counter(r["backend"] for r in rows).items())),
            "habitats": dict(sorted(Counter(r["habitat"] for r in rows).items())),
            "natural_spawnable": sum(bool(r["catalog"].get("natural_spawnable")) for r in rows),
            "spawnable_ids": [r["id"] for r in rows if r["spawnable"]],
            "non_spawnable_ids": [r["id"] for r in rows if not r["spawnable"]],
            "skinned_gltf": sum(r["backend"] == "gltf_skeleton" and r["skinned"] for r in rows),
            "static_gltf": [r["id"] for r in rows if r["backend"] == "gltf_skeleton" and not r["skinned"]],
            "effective_sprites": [r["id"] for r in rows if r["effective_sprite"]],
            "creature_texture_files": sum(len(r["textures"]) for r in rows),
            "skin_definitions": len(skins),
            "skin_texture_files": sum(len(s["textures"]) for s in skins),
            "missing_texture_species": [r["id"] for r in rows if r["missing_runtime_texture_stems"]],
            "fallback_texture_species": [
                r["id"] for r in rows
                if any(item.get("fell_back") and item.get("resolved_key") for item in r["effective_textures"])
            ],
            "unresolved_texture_species": [
                r["id"] for r in rows if any(not item.get("resolved_key") for item in r["effective_textures"])
            ],
            "legacy_parts_ignored_species": [r["id"] for r in rows if r["legacy_parts_ignored"] and r["legacy_part_count"]],
            "exact_reference_geometry_species": [r["id"] for r in rows if r["geometry_exact_source_matches"]],
            "example_pack": {
                "path": example_pack.relative_to(ROOT).as_posix() if example_pack.is_dir() else None,
                "replaces_geometry": False,
                "note": "Pack creature.json can change display_name. Bone and glTF caches keep models/creatures geometry until the process restarts.",
            },
            "bin_sync": {
                "desktop_reads": "repository models/ via WorkDir",
                "bin_models_is_copy": True,
                "clear_creature_visual_cache_syncs_skins": False,
            },
        },
        "species": rows,
        "skins": skins,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = inventory()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["summary"], ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
