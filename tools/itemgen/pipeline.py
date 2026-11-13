"""Build and publish original item meshes from YAML recipes."""

from __future__ import annotations

import json
import shutil
from pathlib import Path
from typing import Any

import yaml

from itemgen.builders import build_parts
from itemgen.export_gltf import write_gltf

ROOT = Path(__file__).resolve().parents[2]
RECIPES = ROOT / "tools" / "item_recipes"
MATERIALS = ROOT / "tools" / "item_materials.yaml"
MODELS = ROOT / "models" / "items"
CONTENT = ROOT / "content" / "items"
STAGING = ROOT / "assetgen_output" / "items" / "staging"


def _normalize_textures_dir(dest: Path) -> Path:
    """Ensure published atlas lives under lowercase `textures/` (Windows-safe)."""
    desired = dest / "textures"
    for child in list(dest.iterdir()) if dest.is_dir() else []:
        if child.is_dir() and child.name.lower() == "textures" and child.name != "textures":
            tmp = dest / "_textures_case_fix"
            if tmp.is_dir():
                for old in tmp.iterdir():
                    old.unlink()
                tmp.rmdir()
            elif tmp.exists():
                tmp.unlink()
            child.rename(tmp)
            tmp.rename(desired)
            break
    desired.mkdir(parents=True, exist_ok=True)
    return desired


def load_materials() -> dict[str, dict[str, list[int]]]:
    data = yaml.safe_load(MATERIALS.read_text(encoding="utf-8"))
    return {str(k): v for k, v in data.items() if isinstance(v, dict)}


def load_recipe(item_id: str) -> dict[str, Any]:
    path = RECIPES / f"{item_id}.yaml"
    if not path.is_file():
        raise FileNotFoundError(path)
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"bad recipe: {path}")
    data.setdefault("id", item_id)
    return data


def resolve_palette(
    recipe: dict[str, Any], materials: dict[str, dict[str, list[int]]]
) -> dict[str, list[int]]:
    key = str(recipe.get("palette") or recipe.get("material") or "iron")
    if key not in materials:
        raise KeyError(f"unknown material palette: {key}")
    return dict(materials[key])


def build_item(item_id: str, *, publish: bool = True) -> Path:
    materials = load_materials()
    recipe = load_recipe(item_id)
    family = str(recipe["family"])
    material = str(recipe.get("material") or "iron")
    parts = list(recipe["parts"]) if recipe.get("parts") else build_parts(family, material)
    if recipe.get("parts"):
        # Optional authored override (full replace).
        parts = list(recipe["parts"])
    palette = resolve_palette(recipe, materials)

    staging = STAGING / item_id
    if staging.exists():
        for child in staging.iterdir():
            if child.is_file():
                child.unlink()
    staging.mkdir(parents=True, exist_ok=True)

    write_gltf(staging, item_id, parts, palette, material=str(recipe.get("palette") or material))

    attribution = {
        "id": item_id,
        "format": "gltf",
        "license": "CC0-1.0",
        "authoring_source": "cubatarium-recipe",
        "family": family,
        "material": material,
        "recipe_version": recipe.get("recipe_version", 1),
        "notes": "Original Cubatarium box mesh; silhouette familiar, geometry authored.",
    }
    (staging / "ATTRIBUTION.json").write_text(
        json.dumps(attribution, indent=2) + "\n", encoding="utf-8"
    )
    (staging / "LICENSE.txt").write_text(
        "CC0-1.0\nOriginal Cubatarium item mesh generated from item_recipes.\n",
        encoding="utf-8",
    )
    (staging / "recipe_snapshot.yaml").write_text(
        yaml.safe_dump(recipe, sort_keys=False), encoding="utf-8"
    )

    # Optional armor wear.json template
    if family.startswith("armor_"):
        slot = family.replace("armor_", "")
        bone = {
            "head": "hat",
            "chest": "body",
            "legs": "rightLeg",
            "feet": "rightLeg",
        }.get(slot, "body")
        wear = {
            "bones": [bone] if slot != "legs" else ["rightLeg", "leftLeg"],
            "offset": [0, 0, 0],
            "euler_deg": [0, 0, 0],
            "scale": 1.0,
            "fit_mode": "bone_volume",
            "model": "model.gltf",
        }
        if slot == "feet":
            wear["bones"] = ["rightLeg", "leftLeg"]
            # bone_volume already places the sole; an extra legacy offset detaches boots.
            wear["offset"] = [0, 0, 0]
        (staging / "wear.json").write_text(
            json.dumps(wear, indent=2) + "\n", encoding="utf-8"
        )

    if publish:
        return publish_item(item_id)
    return staging


def validate_item(item_id: str) -> dict:
    from itemgen.raster import load_primitives
    primitives=load_primitives(STAGING/item_id)
    return {"id":item_id,"primitives":len(primitives),"validated":True}


def publish_item(item_id: str) -> Path:
    validate_item(item_id)
    recipe=load_recipe(item_id)
    staging=STAGING/item_id
    dest = MODELS / item_id
    dest.mkdir(parents=True, exist_ok=True)
    published = {"model.gltf", "model.bin", "ATTRIBUTION.json", "LICENSE.txt",
                 "textures"}
    for name in ("model.gltf", "model.bin", "ATTRIBUTION.json", "LICENSE.txt"):
        src = staging / name
        if src.is_file():
            (dest / name).write_bytes(src.read_bytes())
    wear_src = staging / "wear.json"
    if wear_src.is_file():
        if not (dest / "wear.json").exists() or recipe.get("replace_wear", False):
            (dest / "wear.json").write_bytes(wear_src.read_bytes())
        published.add("wear.json")
    tex_src = staging / "textures" / "diffuse.png"
    if tex_src.is_file():
        tex_dest = _normalize_textures_dir(dest)
        (tex_dest / "diffuse.png").write_bytes(tex_src.read_bytes())
        # Drop stale texture siblings so the atlas stays the single file.
        for child in tex_dest.iterdir():
            if child.is_file() and child.name == "diffuse.generated-old.png":
                child.unlink()
    # Geometry publication does not own authored grips, worn variants or
    # presentation sidecars. Retain them across deterministic regeneration.
    _retarget_content(item_id, recipe)
    return dest


FAMILY_POSE = {
    "pickaxe": {"fit_axis": "y", "wield_euler_deg": [-8, 0, 90], "wield_offset": [0, -0.12, 0]},
    "axe": {"fit_axis": "y", "wield_euler_deg": [-8, 20, 80], "wield_offset": [0, -0.1, 0]},
    "shovel": {"fit_axis": "y", "wield_euler_deg": [12, 0, 90], "wield_offset": [0, 0.08, 0]},
    "hoe": {"fit_axis": "y", "wield_euler_deg": [-6, 0, 90], "wield_offset": [0, -0.08, 0]},
    "sword": {"fit_axis": "y", "wield_euler_deg": [-18, 0, 8], "wield_offset": [0, -0.05, 0]},
    "spear": {"fit_axis": "y", "wield_euler_deg": [-18, 0, 8], "wield_offset": [0, -0.05, 0]},
    "bow": {"fit_axis": "z", "wield_euler_deg": [0, 0, 90], "wield_offset": [0, 0, 0]},
    "arrow": {"fit_axis": "y", "wield_euler_deg": [0, 0, 90], "wield_offset": [0, 0, 0]},
    "shield": {"fit_axis": "z", "wield_euler_deg": [0, 180, 0], "wield_offset": [0, 0, 0]},
    "torch": {"fit_axis": "y", "wield_euler_deg": [-10, 0, 0], "wield_offset": [0, 0.12, 0]},
    "flint": {"fit_axis": "y", "wield_euler_deg": [-10, 0, 0], "wield_offset": [0, 0.12, 0]},
}


def _pose_for(recipe: dict[str, Any]) -> dict[str, Any]:
    family = str(recipe.get("family", ""))
    return FAMILY_POSE.get(
        family,
        {
            "fit_axis": "longest",
            "wield_euler_deg": [-25, 0, 0],
            "wield_offset": [0, 0, 0],
        },
    )


def _retarget_content(item_id: str, recipe: dict[str, Any]) -> None:
    path = CONTENT / f"{item_id}.json"
    if not path.is_file():
        return
    data = json.loads(path.read_text(encoding="utf-8"))
    data["model"] = f"models/items/{item_id}/model.gltf"
    visual = data.setdefault("visual", {})
    if "wield_scale" in recipe:
        visual["wield_scale"] = recipe["wield_scale"]
    pose = _pose_for(recipe)
    for key in ("fit_axis", "wield_euler_deg", "wield_offset"):
        if key in recipe:
            visual[key] = recipe[key]
        else:
            visual[key] = pose[key]
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def list_recipe_ids() -> list[str]:
    return sorted(p.stem for p in RECIPES.glob("*.yaml"))


def build_all(*, publish: bool = True) -> list[str]:
    """Build every recipe. One failing item does not abort the run: the
    failure is reported and the rest keep building; the summary error is
    raised at the end so CI still fails."""
    done: list[str] = []
    failed: list[str] = []
    for item_id in list_recipe_ids():
        try:
            build_item(item_id, publish=publish)
        except Exception as exc:  # noqa: BLE001 - keep building the rest
            failed.append(item_id)
            print(f"FAIL {item_id}: {type(exc).__name__}: {exc}")
            continue
        done.append(item_id)
    if failed:
        raise RuntimeError(f"{len(failed)} item(s) failed to build: {', '.join(failed)}")
    return done
