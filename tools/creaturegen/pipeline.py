"""Build, validate, and publish original creature assets.

Production reads recipes, palettes, and creature.json identity fields.
It does not read bedrock sources, Minecraft textures, or Luanti meshes.
"""

from __future__ import annotations

import json
import hashlib
from pathlib import Path

import yaml
from PIL import Image

from creaturegen.anatomy import build_model, model_span_blocks
from creaturegen.export_bone import export_geometry
from creaturegen.export_gltf import export_gltf
from creaturegen.export_rigid import export_rigid
from creaturegen.hashutil import file_hash, pixel_hash, stable_seed, write_json
from creaturegen.quality import bundle_hash, published_payload_matches, require_review
from creaturegen.species_data import SPECIES
from creaturegen.texturegen import bake_atlas, bake_flat, bake_icon, bake_part_tile, bake_sprite
from creaturegen.surface_materials import bake_surface_atlas
from creaturegen.provenance import source_context,file_records,gltf_components,canonical_hash,write_authorship

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


def palette_for_recipe(recipe: dict, palettes: dict) -> dict:
    """Material families are defaults; species identity may override each role."""
    palette = dict(palettes[recipe["palette_id"]])
    for role, rgb in (recipe.get("palette_overrides") or {}).items():
        if role not in palette or not isinstance(rgb, (list, tuple)) or len(rgb) != 3:
            raise ValueError(f"Invalid species palette override {role!r}")
        if any(type(c) is not int or not 0 <= c <= 255 for c in rgb):
            raise ValueError(f"Invalid RGB for {role!r}: {rgb!r}")
        palette[role] = list(rgb)
    return palette


def creature_ids() -> list[str]:
    return sorted(SPECIES)


def emit_recipes(overwrite: bool = False) -> None:
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
        if path.is_file() and not overwrite:
            recipe = yaml.safe_load(path.read_text(encoding="utf-8"))
        else:
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
        raise FileNotFoundError(f"Missing authored recipe {path}; run explicit 'creaturegen emit'")
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
    palette = palette_for_recipe(recipe, palettes)
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
        atlas = (bake_surface_atlas(model, palette, recipe["surface_style"])
                 if recipe.get("surface_style") else
                 bake_atlas(model, palette, recipe["pattern"], species_id, recipe["recipe_version"], tuple(recipe.get("marks") or ())))
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
        atlas = (bake_surface_atlas(model, palette, recipe["surface_style"])
                 if recipe.get("surface_style") else
                 bake_atlas(model, palette, recipe["pattern"], species_id, recipe["recipe_version"], tuple(recipe.get("marks") or ())))
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
        rigid = export_rigid(model,center_pivot_bones=recipe.get('rigid_center_pivot_bones',()))
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
    manifest_files.append(write_authorship(ROOT,out,species_id,species_id))
    files = file_records(manifest_files,out,recipe)
    span = model_span_blocks(model)
    bounds = _bounds(definition)
    model_scale = recipe.get("model_scale", round(max(bounds) * 0.9 / span, 4) if span else 1.0)
    write_json(out / "manifest.json", {
        "id": species_id,
        "backend": backend,
        "state": "exported",
        "model_scale": model_scale,
        "files": files,
    })
    manifest = _read_json(out / "manifest.json")
    manifest.update(source_context(ROOT,species_id))
    if (out/'model.gltf').is_file():manifest['components']=gltf_components(gltf,blob)
    elif (out/'geometry.geo.json').is_file():
        manifest['components']={'meshes':[{'geometry':_geometry_id(definition,species_id),
            'data_sha256':canonical_hash(geo),'bone_count':len(model.bones)}],
            'procedural_animation_profile':recipe.get('animation_profile',definition['visual'].get('animation_profile'))}
    elif (out/'rigid_model.json').is_file():
        manifest['components']={'meshes':[{'data_sha256':canonical_hash(rigid),'part_count':len(rigid['parts'])}]}
    planned = runtime_definition(species_id, out)
    manifest['motion_clock_contract'] = motion_clock_contract(planned)
    manifest["runtime_definition_sha256"] = hashlib.sha256(
        json.dumps(planned,sort_keys=True,separators=(",", ":"),ensure_ascii=False).encode("utf-8")).hexdigest()
    write_json(out / "manifest.json", manifest)
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
    gltf_file = out / "model.gltf"
    if gltf_file.is_file():
        g = _read_json(gltf_file)
        children = {c for n in g.get("nodes", []) for c in n.get("children", [])}
        if any(i in children for s in g.get("scenes", []) for i in s.get("nodes", [])):
            errors.append("glTF scene lists joint descendants as roots")
        for skin in g.get("skins", []):
            if len(skin.get("joints", [])) > 96:
                errors.append("glTF exceeds the runtime 96-joint palette")
        for material in g.get("materials", []):
            if not (out / "textures" / (material.get("name", "body") + ".png")).is_file():
                errors.append(f"unresolved material {material.get('name')}")
        recipe = load_recipe(species_id)
        if recipe.get("motion"):
            # Capabilities belong to the authored contract, not merely to the
            # currently mapped locomotion states (e.g. die must not disappear).
            contract_path = ROOT / "tools" / "creature_quality_contracts.yaml"
            if contract_path.is_file():
                contract = yaml.safe_load(contract_path.read_text(encoding="utf-8"))["species"][species_id]
                have = {a.get("name") for a in g.get("animations", [])}
                for name in contract["required_clip_capabilities"]:
                    if name not in have:
                        errors.append(f"lost clip capability: {name}")
    return errors


def _merge_tree(src: Path, dest: Path) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    for path in src.rglob("*"):
        if not path.is_file() or path.name == "manifest.json":
            continue
        target = dest / path.relative_to(src)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())


def runtime_definition(species_id: str, staging: Path) -> dict:
    creature_path = MODELS / species_id / "creature.json"
    definition = _read_json(creature_path)
    definition.setdefault("visual", {}).setdefault("icon", {})["mode"] = "parts_preview"
    recipe = load_recipe(species_id)
    if recipe.get("backend") == "bone_skeleton" and not recipe.get("animation_profile"):
        raise ValueError(f"Authored bone recipe requires explicit animation_profile: {species_id}")
    if recipe.get("backend") == "gltf_skeleton" and recipe.get("anatomy"):
        axis = recipe.get("forward_axis")
        if axis not in ("+z", "-z"):
            raise ValueError(f"Authored glTF requires explicit forward_axis (+z/-z): {species_id}")
        travel_axis = recipe.get("locomotion_axis",axis)
        if travel_axis not in ("+z","-z","+x","-x"):
            raise ValueError(f"Invalid authored locomotion_axis: {travel_axis!r}")
        definition["visual"].setdefault("gltf", {})["model_yaw_offset_deg"] = {"+z":0,"-z":180,"+x":270,"-x":90}[travel_axis]
    if "animation_profile" in recipe:
        profile = recipe["animation_profile"]
        if definition["visual"].get("backend") != "bone_skeleton" or profile not in {
                "quadruped", "fox", "arachnid", "humanoid", "chicken", "rabbit",
                "aquatic", "aerial", "serpentine", "turtle", "dolphin", "squid"}:
            raise ValueError(f"Invalid authored animation profile: {profile!r}")
        definition["visual"]["animation_profile"] = profile
    for name, authored in recipe.get("motion", {}).get("clips", {}).items():
        if "runtime_speed" not in authored:
            continue
        speed = authored["runtime_speed"]
        clips = definition["visual"].get("animation", {}).get("clips", {})
        if type(speed) not in (int, float) or not 0 < speed <= 20:
            raise ValueError(f"Invalid runtime clip speed: {name!r}: {speed!r}")
        if name not in clips:
            if type(authored.get("runtime_loop")) is not bool:
                raise ValueError(f"New runtime clip requires explicit runtime_loop: {name!r}")
            clips[name] = {"start":0,"end":1,"loop":authored["runtime_loop"]}
        clips[name]["speed"] = speed
    states = recipe.get("animation_state_map", {})
    valid_states = {"Idle","Walk","Run","Jump","Fall","Crouch","Slither",
                    "Coil","Action","Hover","Glide","Swim","Tread","Fly"}
    for state, name in states.items():
        authored = recipe.get("motion", {}).get("clips", {})
        declared = definition["visual"].get("animation", {}).get("clips", {})
        if state not in valid_states or name not in authored or name not in declared:
            raise ValueError(f"Invalid authored animation state mapping: {state!r}: {name!r}")
        definition["visual"]["animation"].setdefault("state_map", {})[state] = name
    if "animation_overrides" in recipe:
        allowed = {"walk_cycle_hz", "leg_swing_deg", "arm_swing_deg",
                   "fly_body_pitch_deg", "body_bob_blocks", "tail_swing_deg",
                   "run_speed_multiplier", "crouch_leg_bend_deg",
                   "wing_idle_swing_deg", "look_at_deg"}
        for key, value in recipe["animation_overrides"].items():
            if key not in allowed or type(value) not in (int, float) or not 0 <= value <= 180:
                raise ValueError(f"Invalid animation override {key!r}: {value!r}")
            definition["visual"].setdefault("animation", {})[key] = value
    gltf_path = staging / "model.gltf"
    if not gltf_path.is_file():
        geo_path = staging / "geometry.geo.json"
        if geo_path.is_file():
            geo = _read_json(geo_path)
            block = next(v for k,v in geo.items() if k.startswith("geometry."))
            definition["visual"]["texture_size"] = [block["texturewidth"], block["textureheight"]]
        return definition
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
    # Grounded G clips follow distance without the legacy minimum playback
    # floor. Flight/fluid clips and the B/R phase profiles keep their own clocks.
    ground_types={'terrestrial_biped','terrestrial_quadruped','serpentine'}
    sync=recipe.get('ground_distance_sync',definition.get('locomotion_archetype') in ground_types)
    if type(sync) is not bool:raise ValueError('ground_distance_sync must be boolean')
    clips=visual.get('animation',{}).get('clips',{})
    for clip in clips.values():clip.pop('ground_distance_per_cycle',None)
    if sync:
        by_name={a['name']:a for a in gltf.get('animations',[])}
        states=visual.get('animation',{}).get('state_map',{})
        for name in {states.get(s) for s in ('Walk','Run','Slither')} - {None}:
            clip=clips.get(name);animation=by_name.get(name)
            if not clip or not animation or not clip.get('loop',True):continue
            speed=float(clip.get('speed',1));reference=float(definition['locomotion']['walk_speed'])
            duration=max(float(gltf['accessors'][s['input']]['max'][0])for s in animation['samplers'])
            if speed<=0 or reference<=0 or duration<=0:raise ValueError('Invalid distance gait calibration')
            clip['ground_distance_per_cycle']=reference*duration/speed
    return definition


def motion_clock_contract(definition: dict) -> dict:
    visual=definition['visual'];backend=visual['backend'];animation=visual.get('animation',{})
    if backend=='gltf_skeleton':
        return {'kind':'gltf_ground_distance_opt_in','clips':{name:clip['ground_distance_per_cycle']
            for name,clip in animation.get('clips',{}).items()if clip.get('ground_distance_per_cycle',0)>0},
            'other_states':'existing flight/fluid/idle playback; no ground contract asserted'}
    if definition['id']=='fire_spirit':return {'kind':'sprite_no_ground_feet','footplant':'not_applicable'}
    hz=float(animation.get('walk_cycle_hz',2));reference=float(definition['locomotion']['walk_speed'])
    return {'kind':'actual_travel_anim_phase','backend':backend,
            'full_phase_distance_world':reference/hz if hz>0 else None,
            'note':'Existing B/R actual-travel phase; joint-specific planting remains separate'}


def _retarget_gltf_textures(species_id: str, staging: Path) -> None:
    write_json(MODELS / species_id / "creature.json",
               runtime_definition(species_id, staging))


def _drop_stale_textures(staging: Path, dest: Path) -> None:
    staged = {path.name for path in (staging / "textures").glob("*") if path.is_file()}
    textures = dest / "textures"
    if not textures.is_dir():
        return
    for path in textures.iterdir():
        if path.is_file() and path.name not in staged:
            path.unlink()


def _drop_stale_model_files(staging: Path,dest: Path) -> None:
    if not dest.resolve().is_relative_to(ROOT.resolve()):
        raise RuntimeError('Model cleanup outside project root')
    for name in ('model.gltf','model.bin','geometry.geo.json','rigid_model.json'):
        target=dest/name
        if target.is_file() and not (staging/name).is_file():target.unlink()


def publish_species(species_id: str, review_path: Path | None = None) -> None:
    staging = STAGING / species_id
    build_species(species_id, staging)
    errors = validate_staging(species_id, staging)
    if errors:
        raise RuntimeError(f"{species_id}: " + "; ".join(errors))
    require_review(species_id, staging, review_path)
    _merge_tree(staging, MODELS / species_id)
    _drop_stale_textures(staging, MODELS / species_id)
    _drop_stale_model_files(staging, MODELS / species_id)
    _retarget_gltf_textures(species_id, staging)
    _sync_bin(species_id)
    bin_dest = ROOT / "bin" / "models" / "creatures" / species_id
    if bin_dest.is_dir():
        _drop_stale_textures(staging, bin_dest)
        _drop_stale_model_files(staging,bin_dest)
    manifest = _read_json(staging / "manifest.json")
    manifest["state"] = "accepted"
    manifest["review_bundle_sha256"] = bundle_hash(staging)
    write_json(staging / "manifest.json", manifest)


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
        shifted.update(body=[230, 233, 227], belly=[246, 242, 225], wool=[230, 234, 236],
                       limb=[203, 210, 205], face=[224, 229, 219], muzzle=[235, 232, 215],
                       mark=[150, 165, 163], accent=[67, 77, 76])
    return shifted


def _skin_marks(recipe: dict, kind: str) -> tuple[str, ...]:
    if kind == "black" or kind == "gold":
        return ("face_dark",)
    if kind == "snow":
        return ("ears",)
    return tuple(recipe.get("marks") or ())


SKIN_PLANS = {
        "human_adventurer": ("human", "traveler", ""),
        "human_guard": ("human", "mineral", ""),
        "sheep_wool_black": ("sheep", "forest", "black"),
        "sheep_wool_golden": ("sheep", "meadow", "gold"),
        "wolf_snow": ("wolf", "river", "snow"),
}


def build_skin(skin_id: str, staging: Path | None = None) -> Path:
    species_id, palette_id, kind = SKIN_PLANS[skin_id]
    recipe = load_recipe(species_id)
    model = build_model(recipe)
    palettes = _load_palettes()
    palette = _skin_palette(palette_for_recipe(recipe, palettes), kind)
    if skin_id == "human_adventurer":
        palette.update(body=[120,86,50], belly=[210,190,145], limb=[63,73,62])
    elif skin_id == "human_guard":
        palette.update(body=[75,86,108], belly=[171,166,145], limb=[51,57,70], mark=[119,105,78])
    atlas = (bake_surface_atlas(model, palette, recipe["surface_style"])
             if recipe.get("surface_style") else
             bake_atlas(model, palette, recipe["pattern"], skin_id,
                        recipe["recipe_version"], _skin_marks(recipe, kind)))
    stage = staging or (STAGING / "skins" / skin_id)
    if stage.is_dir():
        for path in stage.rglob("*"):
            if path.is_file(): path.unlink()
    textures = stage / "textures"
    textures.mkdir(parents=True, exist_ok=True)
    definition = _read_json(SKINS / skin_id / "skin.json")
    visual=definition.get('visual',{})
    stems=set(visual.get('texture_map',{}).values())|{'diffuse'}
    if visual.get('texture'):stems.add(visual['texture'])
    if visual.get('icon',{}).get('texture'):stems.add(visual['icon']['texture'])
    for stem in sorted(stems):
        (bake_icon(atlas) if stem == "icon" else atlas).save(textures / f"{stem}.png")
    definition.setdefault("visual", {}).setdefault("icon", {})["mode"] = "parts_preview"
    write_json(stage / "skin.json", definition)
    license_path=write_authorship(ROOT,stage,skin_id,species_id)
    write_json(stage / "manifest.json", {"id":skin_id, "backend":recipe["backend"],
        "base_species":species_id,"state":"exported",**source_context(ROOT,species_id),
        "files":file_records([stage/'skin.json',license_path,*textures.glob('*.png')],stage,recipe),
        "base_geometry_sha256":hashlib.sha256(json.dumps(export_geometry(model,
            _geometry_id(_read_json(MODELS/species_id/"creature.json"),species_id)),sort_keys=True).encode()).hexdigest()})
    return stage


def publish_skins(review_path: Path | None = None, ids: list[str] | None = None) -> None:
    staged_skins = []
    for skin_id in ids if ids is not None else SKIN_PLANS:
        staged_skins.append((skin_id, build_skin(skin_id)))
    # Validate every requested skin before modifying any published skin.
    for skin_id, staged in staged_skins:
        errors=validate_staging(skin_id,staged)
        if errors:raise RuntimeError(f'{skin_id}: '+ '; '.join(errors))
        require_review(skin_id, staged, review_path)
        species_id = SKIN_PLANS[skin_id][0]
        geometry = _read_json(MODELS / species_id / "geometry.geo.json")
        actual = hashlib.sha256(json.dumps(geometry,sort_keys=True).encode()).hexdigest()
        if actual != _read_json(staged / "manifest.json")["base_geometry_sha256"]:
            raise RuntimeError(f"{skin_id}: publish the reviewed compatible base geometry first")
    for skin_id, staged in staged_skins:
        _merge_tree(staged, SKINS / skin_id)
        bin_skin = ROOT / "bin" / "models" / "skins" / skin_id
        if bin_skin.parent.is_dir():
            _merge_tree(SKINS / skin_id, bin_skin)
        manifest=_read_json(staged/'manifest.json')
        manifest.update(state='accepted',review_bundle_sha256=bundle_hash(staged))
        write_json(staged/'manifest.json',manifest)


def write_asset_manifest() -> None:
    def entries(ids,stage_root,published_root):
        result=[]
        for species_id in ids:
            staging=stage_root/species_id/'manifest.json'
            if not staging.is_file():
                result.append({'id':species_id,'state':'missing_provenance'})
                continue
            data=_read_json(staging)
            # A file in staging proves export, never artistic acceptance.
            # Check payload against the reviewed/published files as well.
            reviewed = data.get("review_bundle_sha256") or data.get("bounded_release_bundle_sha256")
            if (not reviewed or reviewed != bundle_hash(staging.parent)
                    or not published_payload_matches(staging.parent, published_root / species_id)):
                data["state"] = "needs_visual_review"
            result.append(data)
        return result
    manifest={
        "schema": "creature-asset-manifest.v1",
        "generator": "cubatarium-creaturegen",
        "reference_mode": "authoring-only",
        "species": entries(creature_ids(),STAGING,MODELS),
        "skins":entries(SKIN_PLANS,STAGING/'skins',SKINS),
    }
    write_json(MODELS / "asset_manifest.json",manifest)
    runtime=ROOT/'bin/models/creatures'
    if runtime.is_dir():write_json(runtime/'asset_manifest.json',manifest)


def reference_reads() -> list[str]:
    return [path.as_posix() for path in READS if "_sources" in path.as_posix()]


def isolation_ok() -> bool:
    return not reference_reads()
