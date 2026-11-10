"""Build, validate, and publish original creature assets.

Production reads recipes, palettes, and creature.json identity fields.
It does not read bedrock sources, Minecraft textures, or Luanti meshes.
"""

from __future__ import annotations

import json
from pathlib import Path

import yaml
from PIL import Image

from creaturegen.anatomy import build_model, model_span_blocks
from creaturegen.export_bone import export_geometry
from creaturegen.export_gltf import export_gltf
from creaturegen.export_rigid import export_rigid
from creaturegen.hashutil import file_hash, pixel_hash, stable_seed, write_json
from creaturegen.species_data import SPECIES
from creaturegen.texturegen import bake_atlas, bake_flat, bake_icon, bake_part_tile, bake_sprite

ROOT = Path(__file__).resolve().parents[2]
MODELS = ROOT / "models" / "creatures"
SKINS = ROOT / "models" / "skins"
RECIPES = ROOT / "tools" / "creature_recipes"
PALETTES = ROOT / "tools" / "creature_palettes.yaml"
CATALOG = ROOT / "tools" / "creature_catalog_original.yaml"
STAGING = ROOT / "assetgen_output" / "creatures" / "staging"
READS: list[Path] = []


def _forbid(path: Path) -> None:
    posix = path.as_posix().replace("\\", "/")
    if "/_sources/" in posix or posix.endswith("/_sources") or "bedrock_geo" in posix:
        raise RuntimeError(f"production build refused reference read: {posix}")


def _read_json(path: Path) -> dict:
    _forbid(path)
    READS.append(path)
    return json.loads(path.read_text(encoding="utf-8-sig"))


def _load_palettes() -> dict:
    _forbid(PALETTES)
    READS.append(PALETTES)
    return yaml.safe_load(PALETTES.read_text(encoding="utf-8"))["palettes"]


def creature_ids() -> list[str]:
    return sorted(SPECIES)


def emit_recipes() -> None:
    RECIPES.mkdir(parents=True, exist_ok=True)
    catalog = {"schema": "creature-catalog-original.v1", "species": {}}
    for species_id, base in SPECIES.items():
        definition = _read_json(MODELS / species_id / "creature.json")
        recipe = {
            "id": species_id,
            "backend": definition.get("visual", {}).get("backend"),
            "family": base["palette_id"],
            "authoring_source": "cubatarium-recipe",
            "recipe_version": base["recipe_version"],
            "palette_id": base["palette_id"],
            "pattern": base["pattern"],
            "template": base["template"],
            "body": base["body"],
            "head": base["head"],
            "limb": base["limb"],
            "extras": base["extras"],
            "muzzle": base["muzzle"],
            "bind_body": base["bind_body"],
            "direction": base["direction"],
            "wave": base["wave"],
            "silhouette": base.get("silhouette") or {},
            "marks": base.get("marks") or [],
            "selected_concept": base["selected_concept"],
            "concepts": base["concepts"],
            "seed": stable_seed(species_id, base["recipe_version"]),
        }
        path = RECIPES / f"{species_id}.yaml"
        path.write_text(yaml.safe_dump(recipe, sort_keys=False, allow_unicode=True), encoding="utf-8")
        catalog["species"][species_id] = {
            "backend": recipe["backend"],
            "wave": recipe["wave"],
            "palette_id": recipe["palette_id"],
            "template": recipe["template"],
            "state": "authored",
        }
    CATALOG.write_text(yaml.safe_dump(catalog, sort_keys=False, allow_unicode=True), encoding="utf-8")


def load_recipe(species_id: str) -> dict:
    path = RECIPES / f"{species_id}.yaml"
    if not path.is_file():
        emit_recipes()
    _forbid(path)
    READS.append(path)
    return yaml.safe_load(path.read_text(encoding="utf-8"))


def _target_px(definition: dict) -> int:
    rest = definition.get("bounds", {}).get("rest") or [1, 1, 1]
    return max(12, int(round(float(rest[1]) * 16)))


def _bounds(definition: dict) -> tuple[float, float, float]:
    rest = definition.get("bounds", {}).get("rest") or [1, 1, 1]
    return float(rest[0]), float(rest[1]), float(rest[2])


def _clip_names(definition: dict) -> list[str]:
    animation = definition.get("visual", {}).get("animation") or {}
    state_map = animation.get("state_map") or {}
    names = list(dict.fromkeys(state_map.values()))
    return names or ["idle", "walk"]


def _geometry_id(definition: dict, species_id: str) -> str:
    return definition.get("visual", {}).get("geometry") or f"geometry.{species_id}"


def build_species(species_id: str, staging: Path | None = None, concept_scale=None) -> Path:
    recipe = load_recipe(species_id)
    definition = _read_json(MODELS / species_id / "creature.json")
    palettes = _load_palettes()
    palette = palettes[recipe["palette_id"]]
    model = build_model(recipe, _target_px(definition), concept_scale, _bounds(definition))
    out = staging or (STAGING / species_id)
    if out.exists():
        for child in out.rglob("*"):
            if child.is_file():
                child.unlink()
    out.mkdir(parents=True, exist_ok=True)
    (out / "textures").mkdir(exist_ok=True)
    backend = recipe["backend"]
    billboard = bool(definition.get("visual", {}).get("sprite", {}).get("billboard"))
    manifest_files = []
    if billboard or recipe["template"] == "sprite":
        image = bake_sprite(palette, species_id, recipe["recipe_version"])
        png = out / "textures" / "body.png"
        image.save(png)
        manifest_files.append(png)
    elif backend == "bone_skeleton":
        geo = export_geometry(model, _geometry_id(definition, species_id))
        geo_path = out / "geometry.geo.json"
        write_json(geo_path, geo)
        atlas = bake_atlas(model, palette, recipe["pattern"], species_id, recipe["recipe_version"], tuple(recipe.get("marks") or ()))
        diffuse = out / "textures" / "diffuse.png"
        atlas.save(diffuse)
        icon = out / "textures" / "icon.png"
        bake_icon(atlas).save(icon)
        manifest_files.extend((geo_path, diffuse, icon))
    elif backend == "gltf_skeleton":
        gltf, blob = export_gltf(model, _clip_names(definition))
        gltf_path = out / "model.gltf"
        bin_path = out / "model.bin"
        write_json(gltf_path, gltf)
        bin_path.write_bytes(blob)
        atlas = bake_atlas(model, palette, recipe["pattern"], species_id, recipe["recipe_version"], tuple(recipe.get("marks") or ()))
        stems = [material["name"] for material in gltf["materials"]]
        written = []
        for stem in stems:
            png = out / "textures" / f"{stem}.png"
            if stem == "body":
                atlas.save(png)
            else:
                bake_flat(palette, stem, recipe["pattern"], species_id, atlas.size).save(png)
            written.append(png)
        icon = out / "textures" / "icon.png"
        bake_icon(atlas).save(icon)
        manifest_files.extend([gltf_path, bin_path, icon, *written])
    else:
        rigid = export_rigid(model)
        rigid_path = out / "rigid_model.json"
        write_json(rigid_path, rigid)
        stems = sorted({part["texture"] for part in rigid["parts"]})
        body_tile = None
        for stem in stems:
            png = out / "textures" / f"{stem}.png"
            tile = bake_part_tile(palette, stem, species_id)
            tile.save(png)
            if stem == "body":
                body_tile = tile
            manifest_files.append(png)
        icon = out / "textures" / "icon.png"
        bake_icon(body_tile or bake_part_tile(palette, "body", species_id)).save(icon)
        manifest_files.extend((rigid_path, icon))
    files = []
    for path in manifest_files:
        record = {
            "path": path.relative_to(out).as_posix(),
            "role": path.suffix,
            "sha256": file_hash(path),
            "recipe_version": recipe["recipe_version"],
            "seed": recipe["seed"],
            "authoring_source": "cubatarium-recipe",
        }
        if path.suffix == ".png":
            with Image.open(path) as image:
                record["pixel_sha256"] = pixel_hash(image)
                record["size"] = list(image.size)
        files.append(record)
    span = model_span_blocks(model)
    bounds = _bounds(definition)
    model_scale = round(max(bounds) * 0.9 / span, 4) if span else 1.0
    write_json(out / "manifest.json", {
        "id": species_id,
        "backend": backend,
        "state": "exported",
        "model_scale": model_scale,
        "files": files,
    })
    return out


def validate_staging(species_id: str, staging: Path | None = None) -> list[str]:
    out = staging or (STAGING / species_id)
    errors = []
    manifest = out / "manifest.json"
    if not manifest.is_file():
        return ["missing manifest"]
    for record in _read_json(manifest)["files"]:
        path = out / record["path"]
        if not path.is_file():
            errors.append(f"missing {record['path']}")
            continue
        if record.get("sha256") != file_hash(path):
            errors.append(f"hash changed {record['path']}")
        if path.suffix == ".png":
            with Image.open(path) as image:
                image.load()
                if image.getextrema() is None:
                    errors.append(f"undecodable {path.name}")
    geo = out / "geometry.geo.json"
    if geo.is_file():
        data = _read_json(geo)
        keys = [key for key in data if key.startswith("geometry.")]
        bones = data[keys[0]]["bones"] if keys else []
        if not bones:
            errors.append("no bones")
        tex_w = data[keys[0]]["texturewidth"]
        tex_h = data[keys[0]]["textureheight"]
        with Image.open(out / "textures" / "diffuse.png") as image:
            if image.size != (tex_w, tex_h):
                errors.append(f"diffuse size {image.size} != {(tex_w, tex_h)}")
        for bone in bones:
            for cube in bone.get("cubes", []):
                w, h, d = cube["size"]
                u, v = cube["uv"]
                if u < 0 or v < 0 or u + 2 * (w + d) > tex_w or v + h + d > tex_h:
                    errors.append(f"uv overflow {bone['name']}")
    return errors


def _merge_tree(src: Path, dest: Path) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    for path in src.rglob("*"):
        if not path.is_file() or path.name == "manifest.json":
            continue
        target = dest / path.relative_to(src)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())


def _retarget_gltf_textures(species_id: str, staging: Path) -> None:
    creature_path = MODELS / species_id / "creature.json"
    definition = _read_json(creature_path)
    gltf_path = staging / "model.gltf"
    if not gltf_path.is_file():
        if not definition.get("visual", {}).get("animation") and definition.get("visual", {}).get("sprite") is None:
            if (staging / "model.gltf").is_file():
                pass
        return
    if not gltf_path.is_file():
        return
    gltf = _read_json(gltf_path)
    stems = [material["name"] for material in gltf.get("materials", [])]
    visual = definition.setdefault("visual", {})
    gltf_block = visual.setdefault("gltf", {})
    if stems:
        gltf_block["textures"] = stems
    manifest = _read_json(staging / "manifest.json")
    if manifest.get("model_scale"):
        gltf_block["model_scale"] = manifest["model_scale"]
    if "animation" not in visual:
        visual["animation"] = {
            "walk_cycle_hz": 2.0,
            "clips": {
                "idle": {"start": 0, "end": 1, "loop": True},
                "walk": {"start": 0, "end": 1, "loop": True},
                "fly": {"start": 0, "end": 1, "loop": True},
            },
            "state_map": {
                "Idle": "idle",
                "Walk": "walk",
                "Run": "walk",
                "Fly": "fly",
                "Hover": "fly",
                "Glide": "fly",
                "Action": "walk",
            },
        }
    write_json(creature_path, definition)


def _drop_stale_textures(staging: Path, dest: Path) -> None:
    staged = {path.name for path in (staging / "textures").glob("*") if path.is_file()}
    textures = dest / "textures"
    if not textures.is_dir():
        return
    for path in textures.iterdir():
        if path.is_file() and path.name not in staged:
            path.unlink()


def publish_species(species_id: str) -> None:
    staging = STAGING / species_id
    build_species(species_id, staging)
    errors = validate_staging(species_id, staging)
    if errors:
        raise RuntimeError(f"{species_id}: " + "; ".join(errors))
    _merge_tree(staging, MODELS / species_id)
    _drop_stale_textures(staging, MODELS / species_id)
    _retarget_gltf_textures(species_id, staging)
    _sync_bin(species_id)
    bin_dest = ROOT / "bin" / "models" / "creatures" / species_id
    if bin_dest.is_dir():
        _drop_stale_textures(staging, bin_dest)


def _sync_bin(species_id: str) -> None:
    source = MODELS / species_id
    dest = ROOT / "bin" / "models" / "creatures" / species_id
    if not dest.parent.is_dir():
        return
    _merge_tree(source, dest)


def _skin_palette(palette: dict, kind: str) -> dict:
    shifted = {name: list(color) for name, color in palette.items()}
    if kind == "black":
        shifted.update(wool=[28, 26, 24], body=[36, 32, 30], belly=[48, 44, 40], limb=[42, 38, 34], face=[64, 52, 44], mark=[18, 16, 14])
    elif kind == "gold":
        shifted.update(wool=[228, 184, 72], body=[210, 164, 64], belly=[240, 210, 120], face=[92, 64, 40], limb=[72, 52, 36], mark=[120, 78, 32])
    elif kind == "snow":
        shifted.update(body=[236, 240, 242], belly=[250, 250, 248], wool=[230, 234, 236], limb=[210, 214, 218], face=[90, 78, 68], mark=[70, 62, 56])
    return shifted


def _skin_marks(recipe: dict, kind: str) -> tuple[str, ...]:
    if kind == "black" or kind == "gold":
        return ("face_dark",)
    if kind == "snow":
        return ("ears",)
    return tuple(recipe.get("marks") or ())


def publish_skins() -> None:
    palettes = _load_palettes()
    plans = {
        "human_adventurer": ("human", "traveler", ""),
        "human_guard": ("human", "mineral", ""),
        "sheep_wool_black": ("sheep", "forest", "black"),
        "sheep_wool_golden": ("sheep", "meadow", "gold"),
        "wolf_snow": ("wolf", "river", "snow"),
    }
    for skin_id, (species_id, palette_id, kind) in plans.items():
        recipe = load_recipe(species_id)
        model = build_model(recipe)
        palette = _skin_palette(palettes[palette_id], kind)
        atlas = bake_atlas(model, palette, recipe["pattern"], skin_id, recipe["recipe_version"], _skin_marks(recipe, kind))
        textures = SKINS / skin_id / "textures"
        textures.mkdir(parents=True, exist_ok=True)
        stems = {path.stem for path in textures.glob("*.png")}
        stems.add("diffuse")
        for stem in stems:
            image = atlas
            if stem == "icon":
                image = bake_icon(atlas)
            image.save(textures / f"{stem}.png")
        bin_skin = ROOT / "bin" / "models" / "skins" / skin_id
        if bin_skin.parent.is_dir():
            _merge_tree(SKINS / skin_id, bin_skin)


def write_asset_manifest() -> None:
    entries = []
    for species_id in creature_ids():
        staging = STAGING / species_id / "manifest.json"
        if staging.is_file():
            data = _read_json(staging)
            data["state"] = "accepted"
            entries.append(data)
    write_json(MODELS / "asset_manifest.json", {
        "schema": "creature-asset-manifest.v1",
        "generator": "cubatarium-creaturegen",
        "reference_mode": "authoring-only",
        "species": entries,
    })


def reference_reads() -> list[str]:
    return [path.as_posix() for path in READS if "_sources" in path.as_posix()]


def isolation_ok() -> bool:
    return not reference_reads()
