"""Build and publish original item meshes from YAML recipes."""

from __future__ import annotations

import json
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
    parts = build_parts(family, material)
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

    write_gltf(staging, item_id, parts, palette)

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
            wear["offset"] = [0, -0.35, 0.05]
        (staging / "wear.json").write_text(
            json.dumps(wear, indent=2) + "\n", encoding="utf-8"
        )

    if publish:
        dest = MODELS / item_id
        dest.mkdir(parents=True, exist_ok=True)
        for name in ("model.gltf", "model.bin", "ATTRIBUTION.json", "LICENSE.txt"):
            src = staging / name
            if src.is_file():
                (dest / name).write_bytes(src.read_bytes())
        wear_src = staging / "wear.json"
        if wear_src.is_file():
            (dest / "wear.json").write_bytes(wear_src.read_bytes())
        _retarget_content(item_id, recipe)
        return dest
    return staging


def _retarget_content(item_id: str, recipe: dict[str, Any]) -> None:
    path = CONTENT / f"{item_id}.json"
    if not path.is_file():
        return
    data = json.loads(path.read_text(encoding="utf-8"))
    data["model"] = f"models/items/{item_id}/model.gltf"
    visual = data.setdefault("visual", {})
    if "wield_scale" in recipe:
        visual["wield_scale"] = recipe["wield_scale"]
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def list_recipe_ids() -> list[str]:
    return sorted(p.stem for p in RECIPES.glob("*.yaml"))


def build_all(*, publish: bool = True) -> list[str]:
    done = []
    for item_id in list_recipe_ids():
        build_item(item_id, publish=publish)
        done.append(item_id)
    return done
